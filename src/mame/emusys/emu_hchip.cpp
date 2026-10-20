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
      +$00–$24  W  Long words written at note start by the filter setup
                   (0xFFCFFFCF pairs at +$00/$04, +$10/$14, +$20/$24, also
                   used to clear a channel; 0x8408FFCF and 0x00F200F2 for
                   filter type 1). Probably filter state and coefficients.
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
      +$7C      W  Filter cutoff, written to the previous channel's window
                   (EOS writes +$46 = 0 there first).
    Per-chip registers at +$40/$42 of channels 0 and 16 (output setup) and
    +$44/$46 of channel 0 (written 4 at boot).

    Measured on an e6400 (samplebot session 2026-10-20_e6400_velocity, a
    441 Hz sine at velocities 127..8, against the volume bytes EOS writes in
    emulation): 16.12 dB over 43 steps between velocities 100 and 8, i.e.
    0.375 dB per step. The level glides down to a lower target at about
    7/256 step per sample with the latch at 1 (the release of session
    2026-10-20_e6400_note-2 fits to 0.32 dB rms), and rises within about
    1 ms at note start.

    TODO:
    - Filters: everything but volume is passed through. EOS's default voice
      sets filter type 1 with cutoff 255, which this matches.
    - Fall rates for the latch values other than 1, and the rise rate, are
      not measured (the 2^n scaling below is assumed).
    - Velocity 127 measures 0.6 dB below the 0.375 dB line through the other
      velocities.
    - The output setup registers, and outputs other than the main pair.

******************************************************************************/

#include "emu.h"
#include "emu_hchip.h"

#include <cmath>

#define LOG_REG (1U << 1)
//#define VERBOSE (LOG_REG)
#define VERBOSE (0)
#include "logmacro.h"
#define LOGREG(...) LOGMASKED(LOG_REG, __VA_ARGS__)

namespace {

constexpr double DB_PER_STEP = 0.375;
constexpr u16 RISE_RATE = 5 << 8;   // 8.8 steps per sample: 255 steps in about 1 ms

constexpr u16 fall_rate(u16 latch)
{
	// 7/256 step per sample for latch 1 (measured), scaled by 2 per latch step (assumed)
	return std::max(u16(1), u16((7U << std::min<u16>(latch, 8)) >> 1));
}

} // anonymous namespace

DEFINE_DEVICE_TYPE(EMU_HCHIP, emu_hchip_device, "emu_hchip", "E-mu H-chip Voice Filter")

emu_hchip_device::emu_hchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, EMU_HCHIP, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, m_stream(nullptr)
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
	}
}

void emu_hchip_device::device_clock_changed()
{
	// the level ramps are per output sample, at the G-chips' rate
	m_stream->set_sample_rate(clock() / 1024);
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
	}
}

void emu_hchip_device::sound_stream_update(sound_stream &stream)
{
	for (int ch = 0; ch < NUM_CHANNELS; ch++)
	{
		for (int s = 0; s < 2; s++)
		{
			u16 level = m_level[ch][s];
			const u16 target = u16(m_target[ch][s]) << 8;
			if (!level && !target)
				continue;

			const u16 fall = m_fall[ch][s];
			for (int i = 0; i < stream.samples(); i++)
			{
				if (level < target)
					level = std::min<u32>(level + RISE_RATE, target);
				else if (level > target)
					level = std::max<int>(level - fall, target);
				stream.add(s, i, stream.get(ch, i) * m_gain[level >> (16 - GAIN_BITS)]);
			}
			m_level[ch][s] = level;
		}
	}
}
