// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu G-chip (IC402) — polyphony board sound engine

    Register interface (offsets from chip base, e.g. CSG1CHIP = 0x420000):

    Per-voice registers (64 voices × 0x40-byte stride, voice N base = N × 0x40).
    32-bit registers are written high word first (move.l on the 16-bit bus).
      +$00  R/W  Control/status. Bit 11 = running (0x1800 at voice start,
                 0x1000 stopped). Bit 12 = loop-end buffer in use (0 = +$08,
                 1 = +$0C); the firmware reads it to pick which buffer to
                 rewrite.
      +$04  R/W  Bits 31-16 = rate: the voice steps rate / 4096 source words
                 per output sample (EOS also reads it back, to sum the load
                 of all voices). Bits 9-0 are written when EOS frees a voice
                 (meaning unknown).
      +$08  R/W  Loop end, buffer A: bits 25-0 = ~word address. Bits 26-28 =
                 interpolation kernel select, written by EOS from the rate.
      +$0C  W    Loop end, buffer B: ~word address (bits 25-0).
      +$10  R/W  Bits 31-16 = the rate again while EOS has the voice in use;
                 EOS clears them when it frees the voice (0x000000A8) after
                 the release. Bits 15-0: mode (0x98 one-shot; unknown).
      +$14  W    Loop start word address (bits 25-0): the first word played
                 after a wrap. Bit 26 = loop-end buffer to use after the next
                 wrap (1 = +$0C). EOS parks one-shot voices in a silent loop
                 at words 8..0x1C.
      +$18  W    Start word address; writing it starts the voice from there.
      +$20–$2E   8 words, cleared at voice start (not used by the oscillator).

    Chip-wide registers (mirrored at every voice window, accessed via & 0x3f):
      +$1C  R   Sample data read through a one-word pipeline: each read
                returns the latch, then loads the word at the read address
                and increments it, so the first read after setting the
                address is stale (EOS reads twice, or discards one before a
                block copy)
      +$1E  W   Sample data write to the write address, which increments
      +$30  W   Read address — word-addressed (firmware does asr.l #1, addr;
                bit 29 is EOS's sound-memory pointer tag)
      +$34  W   Write address — word-addressed
      Note: The firmware's G-chip base pointer is NOT always at byte offset 0.
            For gchip1, the base is at ~0x1F000 within the device window.

    Global SIMM configuration registers:
      +$43E W   SIMM config A (timing)
      +$83E W   SIMM config B (bank/size; bit 8 = bank select)
      +$C3E W   SIMM type code

    During SIMM detection, the firmware:
    1. Programs config registers via sub_224E4
    2. Writes test pattern: voice N writes (N × 0x7531) to word address ((N-1) << 20 >> 1)
       via +$34 (write addr) and +$1E (write data), for N = 1..96
    3. Reads back via +$30 (read addr) and +$1C (read data, read twice for pipeline)
    4. Matching patterns determine SIMM size; largest valid config wins

    Voice playback, as measured on an e6400 (eosdocs/e6400_hardware.md,
    eosdocs/e6400_interpolation_measurement.md):
    - The output rate is the audio clock / 1024 (44.1 or 48 kHz).
    - Each output sample is a 6-tap FIR over the words around the read
      position, with one of seven measured low-pass kernels. The kernels do
      not pass the words through unchanged at integer positions.
    - The voice loops from the +$14 word through the loop-end word
      inclusive, and the fractional position carries across the wrap.
    - In the output sample whose step crosses the loop end, every word fetched
      in that step is taken one loop length earlier, so a 2-word step that
      skips the loop-end word reads that word from before the loop.

    TODO:
    - Volume, pan and filtering are the H-chip's, which isn't emulated. As a
      stand-in, a voice is muted while +$10 bits 31-16 are 0, i.e. after EOS
      has freed it at the end of its release; on the hardware it keeps
      running, silenced by the H-chip.
    - Which tap the fetch pointer is (the lead tap is assumed), and the exact
      position of the first output sample after a start.
    - Registers +$04 bits 15-0, +$10 bits 15-0, and the +$20–$2E words.
    - Reverse playback (+$3E DMA modes) and the AES/EBU receiver window.

******************************************************************************/

#include "emu.h"
#include "emu_gchip.h"

#define LOG_SAMPLE (1U << 1)
#define LOG_CONFIG (1U << 2)
#define LOG_VOICE  (1U << 3)
//#define VERBOSE (LOG_SAMPLE | LOG_CONFIG | LOG_VOICE)
#define VERBOSE (0)
#include "logmacro.h"
#define LOGSAMPLE(...) LOGMASKED(LOG_SAMPLE, __VA_ARGS__)
#define LOGCONFIG(...) LOGMASKED(LOG_CONFIG, __VA_ARGS__)
#define LOGVOICE(...)  LOGMASKED(LOG_VOICE, __VA_ARGS__)

namespace {

// The seven interpolation kernels measured on an e6400 (eosdocs/measurements/g_chip_kernel*.csv),
// symmetric, as h(t) for t = 0, 1/32, ..., 3 source words. Select 0 is scaled to unit area (DC
// gain); the others keep their measured gain relative to it. Select 1 is select 0 stretched 1.08x
// in time: the fast-impulse extraction of select 1 gives a single word's area a 5% ripple with phase
// at step 1.30, where the hardware shows 0.8%; the stretched kernel matches it there (0.5%) and at
// step 1.55 (13.9% against 14.4%).
constexpr int KERNEL_POINTS = 97;
constexpr int KERNEL_STEPS_PER_WORD = 32;

constexpr float KERNEL_HALF[7][KERNEL_POINTS] = {
	{ // select 0
		+1.09129f, +1.08855f, +1.08073f, +1.06833f, +1.05086f, +1.02980f, +1.00362f, +0.97274f,
		+0.93777f, +0.89903f, +0.85716f, +0.81257f, +0.76500f, +0.71560f, +0.66396f, +0.61140f,
		+0.55824f, +0.50416f, +0.44982f, +0.39575f, +0.34263f, +0.29044f, +0.23982f, +0.19092f,
		+0.14434f, +0.10105f, +0.05957f, +0.02060f, -0.01525f, -0.04780f, -0.07702f, -0.10248f,
		-0.12278f, -0.14207f, -0.15787f, -0.17042f, -0.17958f, -0.18562f, -0.18881f, -0.18934f,
		-0.18734f, -0.18304f, -0.17660f, -0.16804f, -0.15810f, -0.14643f, -0.13396f, -0.12054f,
		-0.10826f, -0.09320f, -0.07814f, -0.06315f, -0.04806f, -0.03344f, -0.01919f, -0.00556f,
		+0.00744f, +0.01958f, +0.03057f, +0.04084f, +0.04994f, +0.05784f, +0.06462f, +0.07053f,
		+0.07524f, +0.07869f, +0.08135f, +0.08283f, +0.08357f, +0.08340f, +0.08248f, +0.08073f,
		+0.07851f, +0.07574f, +0.07250f, +0.06883f, +0.06489f, +0.06070f, +0.05669f, +0.05272f,
		+0.04717f, +0.04269f, +0.03858f, +0.03436f, +0.03047f, +0.02662f, +0.02327f, +0.01987f,
		+0.01681f, +0.01415f, +0.01150f, +0.00918f, +0.00708f, +0.00541f, +0.00393f, +0.00251f,
		+0.00158f,
	},
	{ // select 1: select 0 stretched 1.08x (see above)
		+1.03672f, +1.03439f, +1.02807f, +1.01810f, +1.00373f, +0.98618f, +0.96495f, +0.93970f,
		+0.91076f, +0.87894f, +0.84408f, +0.80672f, +0.76709f, +0.72504f, +0.68163f, +0.63626f,
		+0.59019f, +0.54334f, +0.49615f, +0.44836f, +0.40062f, +0.35348f, +0.30696f, +0.26170f,
		+0.21730f, +0.17459f, +0.13391f, +0.09599f, +0.05940f, +0.02490f, -0.00709f, -0.03655f,
		-0.06349f, -0.08704f, -0.10716f, -0.12439f, -0.14035f, -0.15323f, -0.16368f, -0.17148f,
		-0.17652f, -0.17932f, -0.17997f, -0.17851f, -0.17510f, -0.17004f, -0.16317f, -0.15493f,
		-0.14539f, -0.13490f, -0.12345f, -0.11159f, -0.10072f, -0.08749f, -0.07423f, -0.06101f,
		-0.04776f, -0.03490f, -0.02214f, -0.00995f, +0.00175f, +0.01268f, +0.02299f, +0.03240f,
		+0.04101f, +0.04894f, +0.05573f, +0.06161f, +0.06681f, +0.07105f, +0.07427f, +0.07678f,
		+0.07826f, +0.07930f, +0.07937f, +0.07907f, +0.07786f, +0.07617f, +0.07379f, +0.07143f,
		+0.06861f, +0.06539f, +0.06192f, +0.05831f, +0.05463f, +0.05136f, +0.04671f, +0.04243f,
		+0.03849f, +0.03496f, +0.03136f, +0.02800f, +0.02465f, +0.02175f, +0.01876f, +0.01608f,
		+0.01371f,
	},
	{ // select 2
		+0.82617f, +0.82506f, +0.82194f, +0.81662f, +0.80932f, +0.79989f, +0.78854f, +0.77517f,
		+0.75999f, +0.74293f, +0.72423f, +0.70383f, +0.68198f, +0.65868f, +0.63415f, +0.60842f,
		+0.58174f, +0.55415f, +0.52586f, +0.49697f, +0.46765f, +0.43802f, +0.40823f, +0.37839f,
		+0.34865f, +0.31914f, +0.28993f, +0.26118f, +0.23296f, +0.20542f, +0.17858f, +0.15262f,
		+0.12754f, +0.10350f, +0.08051f, +0.05870f, +0.03806f, +0.01872f, +0.00064f, -0.01604f,
		-0.03142f, -0.04537f, -0.05800f, -0.06923f, -0.07916f, -0.08775f, -0.09513f, -0.10126f,
		-0.10630f, -0.11022f, -0.11317f, -0.11514f, -0.11628f, -0.11658f, -0.11618f, -0.11508f,
		-0.11338f, -0.11111f, -0.10834f, -0.10511f, -0.10147f, -0.09747f, -0.09316f, -0.08857f,
		-0.08377f, -0.07878f, -0.07368f, -0.06850f, -0.06328f, -0.05810f, -0.05297f, -0.04797f,
		-0.04311f, -0.03845f, -0.03400f, -0.02980f, -0.02587f, -0.02222f, -0.01885f, -0.01578f,
		-0.01297f, -0.01045f, -0.00818f, -0.00617f, -0.00438f, -0.00281f, -0.00144f, -0.00025f,
		+0.00077f, +0.00164f, +0.00236f, +0.00296f, +0.00343f, +0.00378f, +0.00403f, +0.00418f,
		+0.00425f,
	},
	{ // select 3
		+0.68164f, +0.68101f, +0.67921f, +0.67617f, +0.67198f, +0.66659f, +0.66012f, +0.65251f,
		+0.64388f, +0.63419f, +0.62357f, +0.61196f, +0.59950f, +0.58615f, +0.57201f, +0.55706f,
		+0.54140f, +0.52502f, +0.50801f, +0.49037f, +0.47220f, +0.45352f, +0.43441f, +0.41492f,
		+0.39514f, +0.37512f, +0.35496f, +0.33472f, +0.31449f, +0.29434f, +0.27435f, +0.25459f,
		+0.23511f, +0.21600f, +0.19728f, +0.17902f, +0.16123f, +0.14399f, +0.12727f, +0.11115f,
		+0.09558f, +0.08065f, +0.06630f, +0.05260f, +0.03952f, +0.02712f, +0.01534f, +0.00426f,
		-0.00617f, -0.01588f, -0.02492f, -0.03324f, -0.04088f, -0.04778f, -0.05403f, -0.05955f,
		-0.06443f, -0.06863f, -0.07223f, -0.07520f, -0.07761f, -0.07946f, -0.08082f, -0.08168f,
		-0.08212f, -0.08214f, -0.08178f, -0.08108f, -0.08006f, -0.07875f, -0.07718f, -0.07537f,
		-0.07335f, -0.07114f, -0.06878f, -0.06627f, -0.06364f, -0.06092f, -0.05813f, -0.05529f,
		-0.05242f, -0.04953f, -0.04667f, -0.04383f, -0.04104f, -0.03831f, -0.03564f, -0.03305f,
		-0.03055f, -0.02814f, -0.02582f, -0.02361f, -0.02149f, -0.01949f, -0.01758f, -0.01578f,
		-0.01409f,
	},
	{ // select 4
		+0.48451f, +0.48420f, +0.48336f, +0.48192f, +0.47994f, +0.47740f, +0.47432f, +0.47071f,
		+0.46660f, +0.46198f, +0.45691f, +0.45136f, +0.44538f, +0.43896f, +0.43216f, +0.42496f,
		+0.41741f, +0.40949f, +0.40127f, +0.39271f, +0.38387f, +0.37475f, +0.36538f, +0.35576f,
		+0.34594f, +0.33593f, +0.32575f, +0.31543f, +0.30500f, +0.29448f, +0.28390f, +0.27329f,
		+0.26267f, +0.25207f, +0.24153f, +0.23107f, +0.22070f, +0.21046f, +0.20035f, +0.19041f,
		+0.18063f, +0.17105f, +0.16165f, +0.15247f, +0.14349f, +0.13474f, +0.12620f, +0.11790f,
		+0.10983f, +0.10200f, +0.09441f, +0.08708f, +0.08000f, +0.07319f, +0.06666f, +0.06043f,
		+0.05448f, +0.04885f, +0.04353f, +0.03855f, +0.03388f, +0.02955f, +0.02555f, +0.02189f,
		+0.01855f, +0.01553f, +0.01279f, +0.01035f, +0.00815f, +0.00621f, +0.00447f, +0.00292f,
		+0.00152f, +0.00027f, -0.00088f, -0.00194f, -0.00294f, -0.00387f, -0.00478f, -0.00564f,
		-0.00648f, -0.00728f, -0.00805f, -0.00876f, -0.00941f, -0.00999f, -0.01047f, -0.01084f,
		-0.01109f, -0.01120f, -0.01117f, -0.01098f, -0.01064f, -0.01014f, -0.00951f, -0.00873f,
		-0.00785f,
	},
	{ // select 5
		+0.41310f, +0.41290f, +0.41236f, +0.41143f, +0.41014f, +0.40847f, +0.40646f, +0.40409f,
		+0.40138f, +0.39831f, +0.39494f, +0.39123f, +0.38722f, +0.38289f, +0.37828f, +0.37338f,
		+0.36822f, +0.36280f, +0.35713f, +0.35121f, +0.34509f, +0.33874f, +0.33220f, +0.32547f,
		+0.31857f, +0.31150f, +0.30429f, +0.29695f, +0.28949f, +0.28192f, +0.27426f, +0.26653f,
		+0.25874f, +0.25090f, +0.24304f, +0.23517f, +0.22730f, +0.21945f, +0.21164f, +0.20387f,
		+0.19617f, +0.18855f, +0.18102f, +0.17359f, +0.16628f, +0.15910f, +0.15205f, +0.14514f,
		+0.13839f, +0.13181f, +0.12537f, +0.11911f, +0.11302f, +0.10712f, +0.10137f, +0.09581f,
		+0.09042f, +0.08521f, +0.08018f, +0.07533f, +0.07064f, +0.06613f, +0.06179f, +0.05762f,
		+0.05361f, +0.04978f, +0.04611f, +0.04261f, +0.03926f, +0.03609f, +0.03307f, +0.03021f,
		+0.02752f, +0.02498f, +0.02259f, +0.02037f, +0.01828f, +0.01636f, +0.01456f, +0.01292f,
		+0.01141f, +0.01003f, +0.00877f, +0.00764f, +0.00662f, +0.00571f, +0.00489f, +0.00417f,
		+0.00353f, +0.00296f, +0.00248f, +0.00205f, +0.00168f, +0.00136f, +0.00109f, +0.00085f,
		+0.00065f,
	},
	{ // select 6
		+0.34379f, +0.34368f, +0.34336f, +0.34281f, +0.34207f, +0.34110f, +0.33994f, +0.33857f,
		+0.33699f, +0.33522f, +0.33325f, +0.33108f, +0.32874f, +0.32621f, +0.32351f, +0.32062f,
		+0.31758f, +0.31437f, +0.31101f, +0.30748f, +0.30382f, +0.30000f, +0.29606f, +0.29198f,
		+0.28777f, +0.28343f, +0.27898f, +0.27442f, +0.26975f, +0.26498f, +0.26011f, +0.25515f,
		+0.25011f, +0.24500f, +0.23981f, +0.23456f, +0.22926f, +0.22391f, +0.21852f, +0.21310f,
		+0.20766f, +0.20220f, +0.19674f, +0.19128f, +0.18582f, +0.18038f, +0.17498f, +0.16961f,
		+0.16427f, +0.15898f, +0.15375f, +0.14857f, +0.14346f, +0.13842f, +0.13344f, +0.12855f,
		+0.12373f, +0.11900f, +0.11436f, +0.10980f, +0.10532f, +0.10095f, +0.09665f, +0.09245f,
		+0.08834f, +0.08432f, +0.08040f, +0.07657f, +0.07283f, +0.06919f, +0.06564f, +0.06220f,
		+0.05884f, +0.05558f, +0.05242f, +0.04936f, +0.04640f, +0.04355f, +0.04079f, +0.03814f,
		+0.03559f, +0.03315f, +0.03081f, +0.02858f, +0.02646f, +0.02444f, +0.02252f, +0.02071f,
		+0.01900f, +0.01738f, +0.01587f, +0.01444f, +0.01312f, +0.01188f, +0.01072f, +0.00965f,
		+0.00865f,
	},
};

} // anonymous namespace

DEFINE_DEVICE_TYPE(EMU_GCHIP, emu_gchip_device, "emu_gchip", "E-mu G-chip Sound Engine")

emu_gchip_device::emu_gchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, EMU_GCHIP, tag, owner, clock)
	, device_memory_interface(mconfig, *this)
	, device_sound_interface(mconfig, *this)
	, m_sample_space_config("sample", ENDIANNESS_BIG, 16, 27, 0) // 16-bit data, 27-bit byte address = 128 MB (64M words)
	, m_stream(nullptr)
{
}

device_memory_interface::space_config_vector emu_gchip_device::memory_space_config() const
{
	return space_config_vector{ std::make_pair(0, &m_sample_space_config) };
}

void emu_gchip_device::device_start()
{
	space(0).specific(m_sample_space);
	m_stream = stream_alloc(0, NUM_VOICES, clock() / 1024);
	build_kernels();

	save_item(NAME(m_regs));
	save_item(NAME(m_read_addr));
	save_item(NAME(m_write_addr));
	save_item(NAME(m_read_pipeline));
	save_item(NAME(m_simm_config_a));
	save_item(NAME(m_simm_config_b));
	save_item(NAME(m_simm_type));
	save_item(NAME(m_ctrl));
	save_item(NAME(m_rate));
	save_item(NAME(m_in_use));
	save_item(NAME(m_kernel));
	save_item(NAME(m_loop_end));
	save_item(NAME(m_loop_start));
	save_item(NAME(m_next_b));
	save_item(NAME(m_cur_b));
	save_item(NAME(m_lead));
	save_item(NAME(m_frac));
	save_item(NAME(m_taps));
	save_item(NAME(m_tap_pos));
}

void emu_gchip_device::device_reset()
{
	m_read_addr = 0;
	m_write_addr = 0;
	m_read_pipeline = 0;
	m_simm_config_a = 0;
	m_simm_config_b = 0;
	m_simm_type = 0;

	std::fill(std::begin(m_regs), std::end(m_regs), u16(0));

	for (int v = 0; v < NUM_VOICES; v++)
	{
		m_ctrl[v] = 0;
		m_rate[v] = 0;
		m_in_use[v] = false;
		m_kernel[v] = 0;
		m_loop_end[v][0] = m_loop_end[v][1] = 0;
		m_loop_start[v] = 0;
		m_next_b[v] = false;
		m_cur_b[v] = false;
		m_lead[v] = 0;
		m_frac[v] = 0;
		std::fill(std::begin(m_taps[v]), std::end(m_taps[v]), s16(0));
		m_tap_pos[v] = 0;
	}
}

void emu_gchip_device::device_clock_changed()
{
	m_stream->set_sample_rate(clock() / 1024);
}

void emu_gchip_device::build_kernels()
{
	// m_coef[k][p][j] = h(j - 2 - f) for the read position n + f, with f at the centre of phase p
	for (int k = 0; k < NUM_KERNELS; k++)
	{
		for (int p = 0; p < (1 << PHASE_BITS); p++)
		{
			const double f = (p + 0.5) / (1 << PHASE_BITS);
			for (int j = 0; j < NUM_TAPS; j++)
			{
				const double x = std::abs(j - 2 - f) * KERNEL_STEPS_PER_WORD;
				const int i = int(x);
				float h = 0.0f;
				if (i < KERNEL_POINTS - 1)
					h = KERNEL_HALF[k][i] + float(x - i) * (KERNEL_HALF[k][i + 1] - KERNEL_HALF[k][i]);
				else if (i == KERNEL_POINTS - 1)
					h = KERNEL_HALF[k][i];
				m_coef[k][p][j] = h;
			}
		}
	}
}

//
// Host CPU register interface
//
// The CSG1CHIP window is 0x20000 bytes (128 KB = 0x10000 words).
// Offsets are byte addresses from the chip select base.
//
// Special register offsets have side-effects (sample memory access,
// SIMM config). Everything else is plain read/write into m_regs[].
//
// The firmware accesses registers throughout the full 128 KB window,
// including high offsets for G-chip 2 detection (sub_23FB6).
//

u16 emu_gchip_device::read(offs_t offset)
{
	const u32 byte_off = offset * 2;

	// Sample data read register at +$1C within any voice window: returns the
	// pipeline latch, then loads the word at the read address and increments it.
	// The firmware may access this through any voice window (e.g. gchip1
	// uses a base at ~0x1F000), so we mask to the voice-relative offset.
	if ((byte_off & 0x3f) == 0x1c)
	{
		const u16 result = m_read_pipeline;
		if (!machine().side_effects_disabled())
		{
			m_read_pipeline = fetch(m_read_addr);
			LOGSAMPLE("read +$1C: latch=%04x, next from word_addr=%08x -> %04x\n", result, m_read_addr, m_read_pipeline);
			m_read_addr = (m_read_addr & ~ADDR_MASK) | ((m_read_addr + 1) & ADDR_MASK);
		}
		return result;
	}

	// Control low word: bit 12 reports the loop-end buffer the voice is using now
	if ((byte_off & 0x3f) == 0x02)
	{
		const int voice = (byte_off >> 6) & (NUM_VOICES - 1);
		m_stream->update();
		return (m_regs[offset & (NUM_REGS - 1)] & ~0x1000) | (m_cur_b[voice] ? 0x1000 : 0);
	}

	return m_regs[offset & (NUM_REGS - 1)];
}

void emu_gchip_device::write(offs_t offset, u16 data)
{
	const u32 byte_off = offset * 2;
	const int vreg = byte_off & 0x3f;

	// SIMM configuration registers (can appear at +$43E, +$83E, +$C3E and
	// their +$1000 aliases for bank 2: +$143E, +$183E, +$1C3E)
	switch (byte_off & 0xfff)
	{
	case 0x43e:
		LOGCONFIG("SIMM config A = %04x (byte_off=%05x)\n", data, byte_off);
		m_simm_config_a = data;
		break;
	case 0x83e:
		LOGCONFIG("SIMM config B = %04x (byte_off=%05x)\n", data, byte_off);
		m_simm_config_b = data;
		break;
	case 0xc3e:
		LOGCONFIG("SIMM type = %04x (byte_off=%05x)\n", data, byte_off);
		m_simm_type = data;
		break;
	}

	// Sample memory access registers — appear at the same offsets within every
	// voice window.  The firmware's G-chip base pointer can sit anywhere in the
	// 128 KB register window (e.g. gchip1 base is at byte_off ~0x1F000), so we
	// mask to the 0x40-byte voice-relative offset to match.
	switch (vreg)
	{
	case 0x1e: // Sample data write — writes to m_write_addr, which increments
		LOGSAMPLE("write +$1E: data=%04x -> word_addr=%08x\n", data, m_write_addr);
		m_stream->update();
		m_sample_space.write_word((m_write_addr & ADDR_MASK) << 1, data);
		m_write_addr = (m_write_addr & ~ADDR_MASK) | ((m_write_addr + 1) & ADDR_MASK);
		break;

	case 0x30: // Read address high word (word-addressed, 32-bit via move.l)
		m_read_addr = (u32(data) << 16) | (m_read_addr & 0xffff);
		LOGSAMPLE("read addr hi = %04x -> read_addr=%08x\n", data, m_read_addr);
		break;

	case 0x32: // Read address low word
		m_read_addr = (m_read_addr & 0xffff0000) | data;
		LOGSAMPLE("read addr lo = %04x -> read_addr=%08x\n", data, m_read_addr);
		break;

	case 0x34: // Write address high word (word-addressed, 32-bit via move.l)
		m_write_addr = (u32(data) << 16) | (m_write_addr & 0xffff);
		LOGSAMPLE("write addr hi = %04x -> write_addr=%08x\n", data, m_write_addr);
		break;

	case 0x36: // Write address low word
		m_write_addr = (m_write_addr & 0xffff0000) | data;
		LOGSAMPLE("write addr lo = %04x -> write_addr=%08x\n", data, m_write_addr);
		break;
	}

	// Voice registers +$00..+$1A: bring the stream up to date, store, and decode the register pair
	if (vreg < 0x1c)
	{
		m_stream->update();
		m_regs[offset & (NUM_REGS - 1)] = data;
		voice_reg_written((byte_off >> 6) & (NUM_VOICES - 1), offset);
		if (vreg & 2)
			LOGVOICE("V%02d +$%02x=%08x (window +$%05x)\n", (byte_off >> 6) & (NUM_VOICES - 1), vreg & ~3, reg32(offset), byte_off & ~3);
		return;
	}

	// Per-voice filter/volume words (+$20..+$2E): stored only
	if ((vreg & 1) == 0 && vreg >= 0x20 && vreg <= 0x2e)
		LOGVOICE("V%02d +$%02x=%04x\n", (byte_off >> 6) & (NUM_VOICES - 1), vreg, data);

	// Store to backing register file
	m_regs[offset & (NUM_REGS - 1)] = data;
}

void emu_gchip_device::voice_reg_written(int voice, offs_t offset)
{
	// called after each word write to +$00..+$1A; offset is the word written, high word (even) or low (odd)
	const u32 value = reg32(offset);
	switch ((offset << 1) & 0x3c)
	{
	case 0x00:
		m_ctrl[voice] = value;
		if (offset & 1)
			m_cur_b[voice] = BIT(value, 12);
		break;
	case 0x08:
		m_loop_end[voice][0] = ~value & ADDR_MASK;
		m_kernel[voice] = (value >> 26) & 7;
		break;
	case 0x0c:
		m_loop_end[voice][1] = ~value & ADDR_MASK;
		break;
	case 0x04:
		m_rate[voice] = value >> 16;
		break;
	case 0x10:
		m_in_use[voice] = (value >> 16) != 0;
		break;
	case 0x14:
		m_loop_start[voice] = value & ADDR_MASK;
		m_next_b[voice] = BIT(value, 26);
		break;
	case 0x18:
		if (offset & 1)
			start_voice(voice, value & ADDR_MASK);
		break;
	}
}

void emu_gchip_device::start_voice(int voice, u32 start)
{
	// the read position starts at the start word, with the taps filled around it
	m_frac[voice] = 0;
	m_tap_pos[voice] = 0;
	for (int j = 0; j < NUM_TAPS; j++)
		m_taps[voice][j] = fetch(start - 2 + j);
	m_lead[voice] = (start + 3) & ADDR_MASK;
}

void emu_gchip_device::sound_stream_update(sound_stream &stream)
{
	for (int v = 0; v < NUM_VOICES; v++)
	{
		if (!BIT(m_ctrl[v], 11) || !m_in_use[v])
			continue;

		const u32 rate = m_rate[v];
		const auto &coef = m_coef[std::min<int>(m_kernel[v], NUM_KERNELS - 1)];
		u32 lead = m_lead[v];
		u32 frac = m_frac[v];
		int tap_pos = m_tap_pos[v];
		s16 *const taps = m_taps[v];

		for (int i = 0; i < stream.samples(); i++)
		{
			// output: FIR over words n-2 .. n+3 (oldest .. newest tap) at read position n + f
			const float *const c = coef[frac >> (FRAC_BITS - PHASE_BITS)];
			float sum = 0.0f;
			for (int j = 0, t = tap_pos; j < NUM_TAPS; j++, t = (t == NUM_TAPS - 1) ? 0 : t + 1)
				sum += c[j] * taps[t];
			stream.put(v, i, sum * (1.0f / 32768.0f));

			// advance by rate / 4096 and fetch the words passed
			frac += rate;
			const u32 n = frac >> FRAC_BITS;
			frac &= (1U << FRAC_BITS) - 1;
			if (!n)
				continue;

			u32 back = 0;
			u32 end = m_loop_end[v][m_cur_b[v] ? 1 : 0];
			if (lead <= end && lead + n > end && m_loop_start[v] <= end)
			{
				// the step crosses the loop end: every word fetched in it comes from one loop length earlier
				back = end + 1 - m_loop_start[v];
				m_cur_b[v] = m_next_b[v];
			}
			for (u32 k = (n > NUM_TAPS) ? n - NUM_TAPS + 1 : 1; k <= n; k++)
			{
				taps[tap_pos] = fetch(lead + k - back);
				tap_pos = (tap_pos == NUM_TAPS - 1) ? 0 : tap_pos + 1;
			}
			lead = (lead + n - back) & ADDR_MASK;
		}

		m_lead[v] = lead;
		m_frac[v] = frac;
		m_tap_pos[v] = tap_pos;
	}
}
