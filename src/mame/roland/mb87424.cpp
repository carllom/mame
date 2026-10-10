// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Roland MB87424 TVF (time variant filter)

    Per-voice filter and level for the 32 voices of the LP-1/LP-2 (MB87419/
    MB87420) in the MV-30.  The LP-2 feeds it the voices one by one; it
    filters each voice and applies its level, and the voices go on to the
    RCC (TC23SC104AF-007), which pans and mixes them.  Here each voice has
    its own input and output, so the mixing is left to the driver.

    Register usage is from the MV-30 system 1.09 firmware, which runs its
    own TVF envelope and LFO and writes the resulting cutoff about every
    10 ms.  Word registers, 7-bit address; 32-bit values are register pairs
    written high word first:

      08/09  voice enable mask (0 during init, then ffff/ffff)
      0a     001f (number of voices - 1?)
      0b     0000
      0c     level ramp for the following level writes: 0016 normally,
             0003 around the level write that cuts off a stolen voice
      20     voice select for the per-voice registers
      32     readback select: latches the voice's current level for 02/03

    Per voice:
      00/01  filter state? (zeroed at key-on)
      02/03  current level (write: zeroed at key-on; read: see 32).  Before
             reusing a voice the firmware polls it until bits 4-19 are 0.
      04     damping: 1000 - 32 * resonance (0..127), 1000 with the filter off
      05     filter type in the high byte: 00, 04, 02 for tone filter types
             1, 2, 3 (probably LPF, BPF, HPF)
      06/07  filter state? (zeroed at key-on)
      10-17  only ever zeroed
      18     cutoff, one semitone per firmware step: 000c (step 0), 1000
             (100), 4000 (124, also written with the filter off), 4c1c (127)
      1a     level, 0 .. 7fff (tone level * part volume / CC7, linear).
             The current level follows it with the ramp set by 0c at the
             time of the write.
    Bit 15 of 18 and 1a is taken to mean "jump to the new value" rather
    than ramp to it.  The firmware sets it on the first writes at key-on,
    and on cutoff writes while its TVF envelope is not moving; its level
    updates (part volume, CC7) leave it clear, and the hardware ramps them.

    The filter is the Chamberlin state variable filter that fits S-330
    recordings of its TVF (MB654419U), whose cutoff register uses the same
    semitone scale with 4000 = 1.0:
        lp += f * bp;  hp = in - lp - q * bp;  bp += f * hp
        f = cutoff / 0x4000,  q = damping / 0x1000 (Q = 1/q)

    MV-30 recordings of a sawtooth (C3, 120 harmonics, filter types 1-3,
    cutoff 000c..4c1c, damping 0020..1000) match this within 0.1 dB rms on
    almost every setting and 0.5 dB on the rest, measured as the change
    from the unfiltered sawtooth.  The firmware's "filter off" values make
    the filter a one-sample delay, so it is exactly transparent, and the
    boost near Nyquist at the highest cutoffs is on the hardware too.

    Level ramps, from MV-30 recordings:
    - 0c = 0016: one-pole, time constant about 6 ms (CC7 jumps).
    - 0c = 0003: fast.  When the firmware steals a sounding voice it cuts
      the level to 0 this way and then polls the readback until it is 0;
      the new note starts only 2-3 ms later than an unstolen one.  Modelled
      as a linear ramp over 1.5 ms.  With the 6 ms ramp the poll took about
      60 ms per steal and stalled the sequencer.

    TODO:
    - The encoding of 0c (only 0016 and 0003 are used); whether cutoff
      changes are smoothed too.
    - Registers 00/01, 06/07, 0a, 0b and 10-17, the readback format.

***************************************************************************/

#include "emu.h"
#include "mb87424.h"

#include <algorithm>

#define LOG_UNKNOWN (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"

#define LOGUNKNOWN(...) LOGMASKED(LOG_UNKNOWN, __VA_ARGS__)


namespace {

// the current level is 15.5 fixed point, so 0x7fff is full scale
constexpr u32 LEVEL_SHIFT = 5;
constexpr u32 LEVEL_MAX = 0x7fff << LEVEL_SHIFT;

// The level follows its target with a one-pole ramp, 21/4096 of the
// distance per sample (time constant 195 samples, 6.1 ms).  Fitted to
// MV-30 recordings of CC7 jumps between 0 and 127 (6.0 ms falling, 6.4 ms
// rising).
constexpr s64 LEVEL_SMOOTH = 21;

// With register 0c = 0003 the level ramps linearly from full scale to 0 in
// 48 samples (1.5 ms), fitted to the start delay of notes that steal a voice.
constexpr u16 RAMP_FAST = 0x0003;
constexpr u32 LEVEL_FAST_STEP = LEVEL_MAX / 48;

} // anonymous namespace


DEFINE_DEVICE_TYPE(MB87424, mb87424_device, "mb87424", "Roland MB87424 TVF")

mb87424_device::mb87424_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MB87424, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_stream(nullptr)
	, m_select(0)
	, m_readback(0)
	, m_readback_level(0)
	, m_enable(0)
	, m_ramp(0)
	, m_latch_hi(0)
{
}

void mb87424_device::device_start()
{
	// one input and one output per voice, at the LP's sample rate
	m_stream = stream_alloc(NUM_VOICES, NUM_VOICES, SAMPLE_RATE_INPUT_ADAPTIVE);

	save_item(NAME(m_select));
	save_item(NAME(m_readback));
	save_item(NAME(m_readback_level));
	save_item(NAME(m_enable));
	save_item(NAME(m_ramp));
	save_item(NAME(m_latch_hi));
	save_item(STRUCT_MEMBER(m_voice, damping));
	save_item(STRUCT_MEMBER(m_voice, mode));
	save_item(STRUCT_MEMBER(m_voice, cutoff));
	save_item(STRUCT_MEMBER(m_voice, level));
	save_item(STRUCT_MEMBER(m_voice, ramp));
	save_item(STRUCT_MEMBER(m_voice, cur_level));
	save_item(STRUCT_MEMBER(m_voice, lp));
	save_item(STRUCT_MEMBER(m_voice, bp));
}

void mb87424_device::device_reset()
{
	m_select = 0;
	m_readback = 0;
	m_readback_level = 0;
	m_enable = 0;
	m_ramp = 0;
	m_latch_hi = 0;
	for (voice_t &voice : m_voice)
	{
		voice.damping = 0x1000;
		voice.mode = 0;
		voice.cutoff = 0x4000;
		voice.level = 0;
		voice.ramp = 0x0016;
		voice.cur_level = 0;
		voice.lp = voice.bp = 0.0;
	}
}

u16 mb87424_device::read(offs_t offset)
{
	switch (offset)
	{
	case 0x02: return m_readback_level & 0xffff;
	case 0x03: return m_readback_level >> 16;
	default:
		if (!machine().side_effects_disabled())
			LOGUNKNOWN("%s: read %02x\n", machine().describe_context(), offset);
		return 0;
	}
}

void mb87424_device::write(offs_t offset, u16 data, u16 mem_mask)
{
	m_stream->update();

	switch (offset)
	{
	case 0x08: m_enable = (m_enable & 0xffff0000) | (data & mem_mask); break;
	case 0x09: m_enable = (m_enable & 0x0000ffff) | (u32(data & mem_mask) << 16); break;
	case 0x0c: m_ramp = data; break;
	case 0x20: m_select = data & (NUM_VOICES - 1); break;

	case 0x32:
		m_readback = data & (NUM_VOICES - 1);
		m_readback_level = m_voice[m_readback].cur_level;
		break;

	case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: case 0x1f:
		voice_w(m_voice[m_select], offset, data);
		break;

	default:
		LOGUNKNOWN("%s: write %02x = %04x\n", machine().describe_context(), offset, data);
		break;
	}
}

void mb87424_device::voice_w(voice_t &voice, offs_t offset, u16 data)
{
	switch (offset)
	{
	case 0x01: case 0x03: case 0x07:
		m_latch_hi = data;
		break;

	case 0x00:
	case 0x06:
		// the firmware only writes 0 here, at key-on
		if (data || m_latch_hi)
			LOGUNKNOWN("voice %d: state %02x = %04x%04x\n", m_select, offset, m_latch_hi, data);
		(offset == 0x00 ? voice.lp : voice.bp) = 0.0;
		break;

	case 0x02:
		voice.cur_level = std::min<u32>((u32(m_latch_hi) << 16) | data, LEVEL_MAX);
		break;

	case 0x04: voice.damping = data; break;
	case 0x05: voice.mode = data; break;
	case 0x18: voice.cutoff = data & 0x7fff; break;

	case 0x1a:
		voice.level = data & 0x7fff;
		voice.ramp = m_ramp;
		if (m_ramp != RAMP_FAST && m_ramp != 0x0016)
			LOGUNKNOWN("voice %d: level ramp %04x\n", m_select, m_ramp);
		if (BIT(data, 15))
			voice.cur_level = voice.level << LEVEL_SHIFT;
		break;

	default:
		if (data)
			LOGUNKNOWN("voice %d: write %02x = %04x\n", m_select, offset, data);
		break;
	}
}

void mb87424_device::sound_stream_update(sound_stream &stream)
{
	for (unsigned v = 0; v < NUM_VOICES; v++)
	{
		voice_t &voice = m_voice[v];
		if (!BIT(m_enable, v))
			continue;

		const double f = double(voice.cutoff) / 0x4000;
		const double q = double(voice.damping) / 0x1000;
		const u32 target = voice.level << LEVEL_SHIFT;

		for (int i = 0; i < stream.samples(); i++)
		{
			// at least one step, so that the level reaches its target exactly
			// (the firmware waits for 0 before reusing a voice)
			const s64 diff = s64(target) - s64(voice.cur_level);
			if (diff && voice.ramp == RAMP_FAST)
			{
				voice.cur_level += std::clamp<s64>(diff, -s64(LEVEL_FAST_STEP), LEVEL_FAST_STEP);
			}
			else if (diff)
			{
				const s64 step = diff * LEVEL_SMOOTH / 4096;
				voice.cur_level += step ? step : (diff > 0 ? 1 : -1);
			}

			voice.lp += f * voice.bp;
			const double hp = stream.get(v, i) - voice.lp - q * voice.bp;
			voice.bp += f * hp;

			double out;
			if (BIT(voice.mode, 10))
				out = voice.bp;
			else if (BIT(voice.mode, 9))
				out = hp;
			else
				out = voice.lp;

			stream.put(v, i, out * voice.cur_level / LEVEL_MAX);
		}
	}
}
