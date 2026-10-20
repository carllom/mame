// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu H-chip (IC413) — polyphony board voice filter and amplifier

    On the e6400 the four H-chips share CSHCHIP (0x460000), 0x2000 bytes
    each, selected by A14-A13: A14 = G-chip (0 = G-chip 1), A13 = 0 for the
    odd voices and 1 for the even ones. EOS addresses voice v's channel
    through a table at 0x05082C; within a chip the channel is A12 (bit 1 of
    v) and A10-A7 ((v >> 2) - 1, mod 16), with 0x80 bytes of registers each.

    Per-channel registers (offsets in the channel's window), as EOS 3.00
    uses them:
      +$00–$24  W  Filter program: long words at +$00/$04, +$10/$14,
                   +$20/$24, written at note start by the filter type's
                   setup (0xFFCFFFCF = unused); the high byte of +$24 is a
                   gain, 0xF2 = unity. +$08/$0C, +$18/$1C, +$28/$2C are never
                   written (probably the filter state).
      +$34/$36  R  Current left / right level, 8.8 fixed point in volume
                   steps (EOS uses the larger high byte for its voice level
                   readout and for its "all channels silent" check).
                W  Set the current left / right level (EOS writes 0).
      +$3B      R  Reads back +$7B.
      +$46      W  Latch for the next level write: 1 before volume updates,
                   4 before a mute, 0 before a filter cutoff write.
      +$71/$73  W  Left / right volume target, 0.375 dB per step, 255 =
                   0 dB, 0 = off. EOS steps it in software for the amplitude
                   envelope, about every 10 ms.
      +$7B      W  Filter mode (EOS: table at 0x050D7E | Q bits); its bit 7
                   is written to the previous channel's +$7B.
      +$7C      W  Filter cutoff 0-255, the voice's FMORPH plus modulation,
                   written in the previous channel's window (EOS writes +$46
                   = 0 there first).
    Per-chip registers at +$40/$42 of channels 0 and 16 (output setup) and
    +$44/$46 of channel 0 (written 4 at boot).

    Measured on an e6400 (samplebot sessions 2026-10-20_e6400_velocity and
    2026-10-20_e6400_filter, against EOS's register writes in emulation):
    - Volume: 0.375 dB per step (16.12 dB over the 43 steps between
      velocities 100 and 8). The level glides down to a lower target at
      about 7/256 step per sample with the latch at 1 (the release of
      session 2026-10-20_e6400_note-2 fits to 0.32 dB rms), and rises within
      about 1 ms at note start.
    - Filter: each section is a state-variable filter (Chamberlin:
      L += F B; H = x - L - D B; B += F H) whose low-pass, band-pass and
      high-pass outputs match the 2-pole types to 0.2-0.8 dB rms over the
      whole cutoff range, and the 4- and 6-pole low-passes are 2 and 3
      identical sections. F roughly doubles every 40.6 cutoff steps
      (F(0) = 0.012, F(255) = 1.04), but D at Q 0 varies with the cutoff
      (0.76-1.64), so both come from the measured tables below. Q lowers D
      (about 2^(-1/16) per step of the Q word's high byte) and raises F,
      more so at high cutoffs; the low-pass passband drops with Q through
      +$24 (0.75 dB per step) plus a measured correction.

    The filter is modelled per program EOS writes, recognised from the
    words: low-pass with 1-3 sections (types 0-2), high-pass with 1-2
    (types 3-4), band-pass with 1-2 (types 5-6). Only 2-pole Q 0 is measured
    for high- and band-pass; their Q uses the low-pass relations.

    TODO:
    - The other 14 filter types (contrary band-pass, swept EQs, phasers,
      flanger, vocal and morph filters) pass through unfiltered; their
      words follow a common structure that would need decoding rather than
      a model per type.
    - At high Q the resonance of the 4- and 6-pole low-pass is off by up to
      4 dB in a narrow band (the 2-pole's error, multiplied).
    - Fall rates for the latch values other than 1, and the rise rate, are
      not measured (the 2^n scaling below is assumed). The cutoff is applied
      at once, not glided.
    - Velocity 127 measures 0.6 dB below the 0.375 dB line through the other
      velocities.
    - The output setup registers, and outputs other than the main pair.

******************************************************************************/

#include "emu.h"
#include "emu_hchip.h"

#include <cmath>

#define LOG_REG    (1U << 1)
#define LOG_FILTER (1U << 2)
//#define VERBOSE (LOG_REG | LOG_FILTER)
#define VERBOSE (0)
#include "logmacro.h"
#define LOGREG(...)    LOGMASKED(LOG_REG, __VA_ARGS__)
#define LOGFILTER(...) LOGMASKED(LOG_FILTER, __VA_ARGS__)

namespace {

constexpr double DB_PER_STEP = 0.375;
constexpr u16 RISE_RATE = 5 << 8;   // 8.8 steps per sample: 255 steps in about 1 ms

constexpr u16 fall_rate(u16 latch)
{
	// 7/256 step per sample for latch 1 (measured), scaled by 2 per latch step (assumed)
	return std::max(u16(1), u16((7U << std::min<u16>(latch, 8)) >> 1));
}

constexpr u32 WORD_UNUSED = 0xffcfffcf;

// Filter section F and D at Q 0, fitted to the 2-pole low-pass at cutoffs 0, 4, ..., 252 and 255
constexpr int CUTOFF_POINTS = 65;
constexpr float F_Q0[CUTOFF_POINTS] = {
	0.011940f, 0.013750f, 0.015390f, 0.016940f, 0.018210f, 0.019480f, 0.020580f, 0.021730f,
	0.023390f, 0.025330f, 0.027180f, 0.028900f, 0.030520f, 0.032850f, 0.035620f, 0.038250f,
	0.040670f, 0.042980f, 0.045890f, 0.049910f, 0.053670f, 0.057140f, 0.060380f, 0.064200f,
	0.069900f, 0.075170f, 0.080110f, 0.084790f, 0.089740f, 0.098000f, 0.105560f, 0.112580f,
	0.119240f, 0.125680f, 0.137510f, 0.148450f, 0.158390f, 0.168020f, 0.177160f, 0.193170f,
	0.209020f, 0.223770f, 0.237750f, 0.250680f, 0.272280f, 0.295080f, 0.316150f, 0.336150f,
	0.355240f, 0.383510f, 0.415940f, 0.446660f, 0.475270f, 0.502470f, 0.539990f, 0.587190f,
	0.630820f, 0.671750f, 0.710440f, 0.760000f, 0.826770f, 0.888510f, 0.946880f, 1.001420f,
	1.038740f };
constexpr float D_Q0[CUTOFF_POINTS] = {
	0.8734f, 0.8346f, 0.7906f, 0.7664f, 0.7676f, 0.7612f, 0.7792f, 0.8188f,
	0.8537f, 0.8675f, 0.8862f, 0.8930f, 0.9150f, 0.9067f, 0.8848f, 0.9310f,
	0.9584f, 1.0040f, 1.0333f, 1.0225f, 1.0325f, 1.0396f, 1.0492f, 1.0806f,
	1.1073f, 1.1381f, 1.1591f, 1.2002f, 1.2308f, 1.2168f, 1.2187f, 1.2574f,
	1.3301f, 1.4134f, 1.4717f, 1.4954f, 1.5088f, 1.4962f, 1.5100f, 1.4714f,
	1.5169f, 1.5579f, 1.6001f, 1.6430f, 1.6293f, 1.6083f, 1.5989f, 1.5928f,
	1.5940f, 1.5568f, 1.5069f, 1.4672f, 1.4395f, 1.4188f, 1.3720f, 1.3052f,
	1.2561f, 1.2177f, 1.1866f, 1.1398f, 1.0724f, 1.0208f, 0.9783f, 0.9439f,
	0.9246f };

// Low-pass Q: the high bytes of the Q words EOS uses (DWORD_ARRAY_0007BEE8, Q >> 3; 0x52 serves Q 112-127),
// and per Q step at cutoffs 64, 128, 192, 240 (2-pole Q ladders): F and D relative to Q 0, the passband
// correction per section beyond the +$24 gain (dB), and the extra gain of each further section (4-pole, dB)
constexpr int Q_STEPS = 15;
constexpr int Q_CUTOFFS = 4;
constexpr u8 LP_Q_HIGH_BYTE[Q_STEPS] = { 0x84, 0x80, 0x7d, 0x7a, 0x77, 0x73, 0x70, 0x6d, 0x6a, 0x67, 0x63, 0x60, 0x5d, 0x5a, 0x52 };
constexpr float Q_CUTOFF[Q_CUTOFFS] = { 64.0f, 128.0f, 192.0f, 240.0f };
constexpr float LP_Q_F[Q_STEPS][Q_CUTOFFS] = {
	{ 1.0000f, 1.0000f, 1.0000f, 1.0000f }, { 1.0101f, 1.0169f, 1.0263f, 1.0440f }, { 1.0231f, 1.0420f, 1.0929f, 1.1058f },
	{ 1.0317f, 1.0646f, 1.1336f, 1.1435f }, { 1.0452f, 1.1122f, 1.1934f, 1.2011f }, { 1.0529f, 1.1423f, 1.2304f, 1.2355f },
	{ 1.0649f, 1.1862f, 1.2854f, 1.3308f }, { 1.0600f, 1.2008f, 1.3152f, 1.3925f }, { 1.0730f, 1.2430f, 1.3668f, 1.4840f },
	{ 1.0853f, 1.2835f, 1.4166f, 1.5677f }, { 1.1020f, 1.3102f, 1.4778f, 1.6218f }, { 1.1020f, 1.3359f, 1.5593f, 1.6951f },
	{ 1.1200f, 1.3616f, 1.6187f, 1.7455f }, { 1.1200f, 1.3868f, 1.6938f, 1.8119f }, { 1.1470f, 1.4232f, 1.7731f, 1.8830f } };
constexpr float LP_Q_D[Q_STEPS][Q_CUTOFFS] = {
	{ 1.0000f, 1.0000f, 1.0000f, 1.0000f }, { 0.7954f, 0.8071f, 0.8623f, 0.8889f }, { 0.7178f, 0.7107f, 0.7300f, 0.7878f },
	{ 0.6316f, 0.6202f, 0.6259f, 0.7153f }, { 0.5542f, 0.5133f, 0.5204f, 0.6334f }, { 0.4591f, 0.4218f, 0.4266f, 0.5723f },
	{ 0.3834f, 0.3496f, 0.3574f, 0.4864f }, { 0.3496f, 0.3119f, 0.3157f, 0.4250f }, { 0.3127f, 0.2696f, 0.2700f, 0.3592f },
	{ 0.2777f, 0.2279f, 0.2290f, 0.3047f }, { 0.2362f, 0.1881f, 0.1862f, 0.2601f }, { 0.2037f, 0.1551f, 0.1513f, 0.2184f },
	{ 0.1862f, 0.1349f, 0.1317f, 0.1811f }, { 0.1701f, 0.1191f, 0.1122f, 0.1478f }, { 0.1287f, 0.0926f, 0.0891f, 0.1220f } };
constexpr float LP_Q_GAIN_DB[Q_STEPS][Q_CUTOFFS] = {
	{ 0.0000f, 0.0000f, 0.0000f, 0.0000f }, { -0.5423f, -0.4818f, -0.3990f, -0.4012f }, { 0.1217f, 0.1744f, 0.3338f, 0.3806f },
	{ -0.1548f, -0.0288f, 0.2102f, 0.2969f }, { -1.0130f, -0.9181f, -0.5426f, -0.4493f }, { -0.4085f, -0.2408f, 0.1943f, 0.3363f },
	{ -0.3515f, -0.1173f, 0.3623f, 0.5535f }, { -0.6334f, -0.3736f, 0.1707f, 0.4079f }, { -0.0225f, 0.2810f, 0.8860f, 1.1794f },
	{ -0.0679f, 0.2345f, 0.9487f, 1.2986f }, { -0.5565f, -0.2156f, 0.5635f, 0.9457f }, { -0.7890f, -0.4110f, 0.4502f, 0.9127f },
	{ -1.5872f, -1.2596f, -0.2480f, 0.2479f }, { -1.8048f, -1.4015f, -0.3360f, 0.2465f }, { -2.1194f, -1.6117f, -0.5014f, 0.1055f } };
constexpr float LP_Q_SECTION_DB[Q_STEPS] = { 0.0f, 1.1f, 0.2f, 0.5f, 2.0f, 1.3f, 1.3f, 0.9f, 0.1f, -0.1f, 1.0f, 0.5f, 1.4f, 1.3f, 1.0f };

// Exponent of F's rise with Q against D's fall, F = F0 (D / D0)^-p, at cutoffs 0, 64, 128, 192, 240, 255
constexpr float Q_F_EXPONENT[6][2] = { { 0.0f, 0.065f }, { 64.0f, 0.065f }, { 128.0f, 0.156f }, { 192.0f, 0.238f }, { 240.0f, 0.337f }, { 255.0f, 0.337f } };

// Q-word high bytes at Q 0 for the high- and band-pass programs
constexpr u8 HP_Q0_HIGH_BYTE = 0x80;
constexpr u8 BP_Q0_HIGH_BYTE = 0x88;
constexpr float HP_GAIN_DB = 0.5f;

// The band-pass against the low-pass tables at cutoffs 0, 16, ..., 240 and 255 (2-pole band-pass ladder, Q 0):
// F and D ratios, and the gain of its band-pass output (dB)
constexpr int BP_POINTS = 17;
constexpr float BP_CUTOFF[BP_POINTS] = { 0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240, 255 };
constexpr float BP_F[BP_POINTS] = {
	0.9137f, 0.9440f, 0.9397f, 0.9528f, 0.9491f, 0.9378f, 0.9199f, 0.9340f, 0.9263f,
	0.9033f, 0.8718f, 0.8639f, 0.8692f, 0.8389f, 0.8076f, 0.7941f, 0.8259f };
constexpr float BP_D[BP_POINTS] = {
	1.4873f, 1.3512f, 1.3595f, 1.2608f, 1.2600f, 1.1939f, 1.1821f, 1.1141f, 1.0278f,
	0.9797f, 0.9819f, 1.0031f, 1.0193f, 1.0757f, 1.1414f, 1.1975f, 1.1842f };
constexpr float BP_GAIN_DB[BP_POINTS] = {
	1.71f, -0.29f, 0.39f, 0.23f, 0.20f, 0.46f, 0.29f, 0.90f, 0.71f,
	0.96f, 1.36f, 1.12f, 1.68f, 1.65f, 2.10f, 2.40f, 1.91f };

// Linear interpolation of a table over cutoff points
template <int N>
float interp_cutoff(float cutoff, const float (&x)[N], const float (&y)[N])
{
	if (cutoff <= x[0])
		return y[0];
	for (int i = 1; i < N; i++)
		if (cutoff <= x[i])
			return y[i - 1] + (y[i] - y[i - 1]) * (cutoff - x[i - 1]) / (x[i] - x[i - 1]);
	return y[N - 1];
}

float table_at_cutoff(const float (&t)[CUTOFF_POINTS], int cutoff, bool logarithmic)
{
	// points every 4 steps, the last at 255
	const float x = (cutoff >= 252) ? 63.0f + (cutoff - 252) / 3.0f : cutoff / 4.0f;
	const int i = std::min(int(x), CUTOFF_POINTS - 2);
	const float a = x - i;
	if (logarithmic)
		return t[i] * std::pow(t[i + 1] / t[i], a);
	return t[i] + (t[i + 1] - t[i]) * a;
}

float q_table_at(const float (&row)[Q_CUTOFFS], int cutoff)
{
	return interp_cutoff(float(cutoff), Q_CUTOFF, row);
}

float q_f_exponent(int cutoff)
{
	float x[6], y[6];
	for (int i = 0; i < 6; i++)
	{
		x[i] = Q_F_EXPONENT[i][0];
		y[i] = Q_F_EXPONENT[i][1];
	}
	return interp_cutoff(float(cutoff), x, y);
}

} // anonymous namespace

DEFINE_DEVICE_TYPE(EMU_HCHIP, emu_hchip_device, "emu_hchip", "E-mu H-chip Voice Filter")

emu_hchip_device::emu_hchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, EMU_HCHIP, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_stream(nullptr)
	, m_unknown_logged(0)
{
}

void emu_hchip_device::device_start()
{
	m_stream = stream_alloc(NUM_CHANNELS, 2, clock() / 1024);
	build_gain_table();

	save_item(NAME(m_regs));
	save_item(NAME(m_level));
	save_item(NAME(m_target));
	save_item(NAME(m_fall));
	save_item(NAME(m_low));
	save_item(NAME(m_band));
}

void emu_hchip_device::device_reset()
{
	std::fill(std::begin(m_regs), std::end(m_regs), u16(0));
	for (int ch = 0; ch < NUM_CHANNELS; ch++)
	{
		for (int s = 0; s < 2; s++)
		{
			m_level[ch][s] = 0;
			m_target[ch][s] = 0;
			m_fall[ch][s] = fall_rate(1);
		}
		std::fill(std::begin(m_low[ch]), std::end(m_low[ch]), 0.0f);
		std::fill(std::begin(m_band[ch]), std::end(m_band[ch]), 0.0f);
		m_filter[ch] = filter_coefs();
	}
	m_unknown_logged = 0;
}

void emu_hchip_device::device_clock_changed()
{
	// the level ramps are per output sample, at the G-chips' rate
	m_stream->set_sample_rate(clock() / 1024);
}

void emu_hchip_device::device_post_load()
{
	for (int ch = 0; ch < NUM_CHANNELS; ch++)
		update_filter(ch);
}

void emu_hchip_device::build_gain_table()
{
	// level 0 is off; above it, 0.375 dB per step below 0 dB at 255
	m_gain[0] = 0.0f;
	for (int i = 1; i < (1 << GAIN_BITS); i++)
	{
		const double steps = double(i) / (1 << (GAIN_BITS - 8));
		m_gain[i] = float(std::pow(10.0, (steps - 255.0) * DB_PER_STEP / 20.0));
	}
}

void emu_hchip_device::update_filter(int ch)
{
	// recognise the program from the words EOS writes for each filter type (traced in emulation)
	const u32 w00 = reg32(ch, 0x00), w04 = reg32(ch, 0x04), w10 = reg32(ch, 0x10);
	const u32 w14 = reg32(ch, 0x14), w20 = reg32(ch, 0x20), w24 = reg32(ch, 0x24);
	filter_coefs &fc = m_filter[ch];
	fc = filter_coefs();
	u32 qword = 0;
	if (w04 == WORD_UNUSED && w14 == WORD_UNUSED && w00 != WORD_UNUSED)
	{
		// low-pass: the same Q word in 1-3 sections (types 0-2)
		fc.program = PROGRAM_LOWPASS;
		qword = w00;
		fc.sections = 1 + (w10 == w00) + (w10 == w00 && w20 == w00);
	}
	else if (w00 == WORD_UNUSED && w04 == 0x80048004 && w10 != WORD_UNUSED)
	{
		// high-pass: Q word at +$10, and again at +$20 for the 4th order (types 3-4)
		fc.program = PROGRAM_HIGHPASS;
		qword = w10;
		fc.sections = (w14 == 0x80048004 && w20 == w10) ? 2 : 1;
	}
	else if (w00 == 0xff68ffcf && w04 == 0xff68ffcf && w14 == 0xff04ff90)
	{
		// 2nd order band-pass: Q word at +$20 (type 5)
		fc.program = PROGRAM_BANDPASS;
		qword = w20;
		fc.sections = 1;
	}
	else if (w04 == 0xff04ff90 && w14 == 0xff04ff90 && w20 == w00)
	{
		// 4th order band-pass: Q word at +$00 and +$20 (type 6)
		fc.program = PROGRAM_BANDPASS;
		qword = w00;
		fc.sections = 2;
	}
	else
	{
		if (!BIT(m_unknown_logged, ch) && w00 != WORD_UNUSED)
		{
			LOGFILTER("ch%02d unrecognised filter program %08x %08x %08x %08x %08x %08x (passed through)\n", ch, w00, w04, w10, w14, w20, w24);
			m_unknown_logged |= 1U << ch;
		}
		return;
	}
	m_unknown_logged &= ~(1U << ch);

	// the cutoff is in the previous channel's window
	const int prev = (ch & 0x10) | ((ch - 1) & 0x0f);
	const int cutoff = m_regs[window(prev) | (0x7c >> 1)] >> 8;
	const u8 qhigh = qword >> 24;
	float f = table_at_cutoff(F_Q0, cutoff, true);
	float d = table_at_cutoff(D_Q0, cutoff, false);
	float gain_db = (int((w24 >> 16) & 0xff) - 0xf2) * 0.75f;

	if (fc.program == PROGRAM_LOWPASS)
	{
		// the measured Q steps, interpolated between the high bytes EOS uses
		int q = 0;
		while (q < Q_STEPS - 1 && qhigh <= LP_Q_HIGH_BYTE[q + 1])
			q++;
		float a = 0.0f;
		if (q < Q_STEPS - 1 && qhigh < LP_Q_HIGH_BYTE[q])
			a = float(LP_Q_HIGH_BYTE[q] - qhigh) / (LP_Q_HIGH_BYTE[q] - LP_Q_HIGH_BYTE[q + 1]);
		const int q2 = std::min(q + 1, Q_STEPS - 1);
		const auto mix = [&] (const float (&t)[Q_STEPS][Q_CUTOFFS]) { return q_table_at(t[q], cutoff) * (1.0f - a) + q_table_at(t[q2], cutoff) * a; };
		f *= mix(LP_Q_F);
		d *= mix(LP_Q_D);
		gain_db += fc.sections * mix(LP_Q_GAIN_DB) + (fc.sections - 1) * (LP_Q_SECTION_DB[q] * (1.0f - a) + LP_Q_SECTION_DB[q2] * a);
	}
	else
	{
		// D falls 2^(-1/16) per step of the Q word's high byte, and F rises against it as for the low-pass
		const float r = std::pow(2.0f, (int(qhigh) - (fc.program == PROGRAM_HIGHPASS ? HP_Q0_HIGH_BYTE : BP_Q0_HIGH_BYTE)) / 16.0f);
		f *= std::pow(std::min(r, 1.0f), -q_f_exponent(cutoff));
		d *= r;
		if (fc.program == PROGRAM_HIGHPASS)
		{
			gain_db += HP_GAIN_DB;
		}
		else
		{
			f *= interp_cutoff(float(cutoff), BP_CUTOFF, BP_F);
			d *= interp_cutoff(float(cutoff), BP_CUTOFF, BP_D);
			gain_db += fc.sections * interp_cutoff(float(cutoff), BP_CUTOFF, BP_GAIN_DB);
		}
	}

	// keep the section stable: the poles stay inside the unit circle while F^2 + 2 F D < 4
	const float limit = 3.9f;
	if (f * f + 2.0f * f * d > limit)
		f = -d + std::sqrt(d * d + limit);
	fc.f = f;
	fc.d = d;
	fc.gain = std::pow(10.0f, gain_db / 20.0f);
	LOGFILTER("ch%02d program %d x%d cutoff %d Q byte %02x: F %.4f D %.4f gain %.2f dB\n", ch, fc.program, fc.sections, cutoff, qhigh, f, d, gain_db);
}

u16 emu_hchip_device::read(offs_t offset, u16 mem_mask)
{
	const offs_t byte_off = offset << 1;
	const int ch = channel(byte_off);
	switch (byte_off & 0x7e)
	{
	case 0x34: // current level
	case 0x36:
		m_stream->update();
		return m_level[ch][BIT(byte_off, 1)];

	case 0x3a: // +$3B reads back +$7B
		return m_regs[(offset & ~0x3f) | (0x7a >> 1)];
	}
	return m_regs[offset & (NUM_REGS - 1)];
}

void emu_hchip_device::write(offs_t offset, u16 data, u16 mem_mask)
{
	const offs_t byte_off = offset << 1;
	const int ch = channel(byte_off);
	m_stream->update();
	COMBINE_DATA(&m_regs[offset & (NUM_REGS - 1)]);
	const u16 value = m_regs[offset & (NUM_REGS - 1)];

	switch (byte_off & 0x7e)
	{
	case 0x00: case 0x02: case 0x04: case 0x06: // filter program
	case 0x10: case 0x12: case 0x14: case 0x16:
	case 0x20: case 0x22: case 0x24: case 0x26:
		update_filter(ch);
		break;

	case 0x34: // set the current level
	case 0x36:
		m_level[ch][BIT(byte_off, 1)] = value;
		break;

	case 0x70: // +$71 left volume target
	case 0x72: // +$73 right
		if (ACCESSING_BITS_0_7)
		{
			const int side = BIT(byte_off, 1);
			m_target[ch][side] = value & 0xff;
			m_fall[ch][side] = fall_rate(m_regs[(offset & ~0x3f) | (0x46 >> 1)]);
			LOGREG("ch%02d %c volume %02x (latch %d)\n", ch, side ? 'R' : 'L', value & 0xff, m_regs[(offset & ~0x3f) | (0x46 >> 1)]);
		}
		break;

	case 0x7c: // filter cutoff of the next channel
		if (ACCESSING_BITS_8_15)
			update_filter((ch & 0x10) | ((ch + 1) & 0x0f));
		break;
	}
}

void emu_hchip_device::sound_stream_update(sound_stream &stream)
{
	for (int ch = 0; ch < NUM_CHANNELS; ch++)
	{
		u16 level[2] = { m_level[ch][0], m_level[ch][1] };
		const u16 target[2] = { u16(u16(m_target[ch][0]) << 8), u16(u16(m_target[ch][1]) << 8) };
		if (!level[0] && !level[1] && !target[0] && !target[1])
			continue;

		const filter_coefs &fc = m_filter[ch];
		float *const low = m_low[ch];
		float *const band = m_band[ch];
		for (int i = 0; i < stream.samples(); i++)
		{
			float x = stream.get(ch, i);
			if (fc.program != PROGRAM_BYPASS)
			{
				for (int s = 0; s < fc.sections; s++)
				{
					low[s] += fc.f * band[s];
					const float high = x - low[s] - fc.d * band[s];
					band[s] += fc.f * high;
					x = (fc.program == PROGRAM_LOWPASS) ? low[s] : (fc.program == PROGRAM_HIGHPASS) ? high : band[s];
				}
				x *= fc.gain;
			}

			for (int s = 0; s < 2; s++)
			{
				if (level[s] < target[s])
					level[s] = std::min<u32>(level[s] + RISE_RATE, target[s]);
				else if (level[s] > target[s])
					level[s] = std::max<int>(level[s] - m_fall[ch][s], target[s]);
				stream.add(s, i, x * m_gain[level[s] >> (16 - GAIN_BITS)]);
			}
		}
		m_level[ch][0] = level[0];
		m_level[ch][1] = level[1];
	}
}
