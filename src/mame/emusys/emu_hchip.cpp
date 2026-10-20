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
      +$00–$24  W  Filter program, written at note start: pole polynomials
                   at +$00, +$10, +$20, zero polynomials at +$04, +$14, and a
                   gain word at +$24 (see below). +$08/$0C, +$18/$1C,
                   +$28/$2C are never written (probably the filter state).
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
      +$7B      W  Filter mode (EOS: table at 0x050D7E | coefficient bits
                   6-0); bit 7 of the coefficient byte goes to the previous
                   channel's +$7B.
      +$7C      W  Filter cutoff 0-255, the voice's FMORPH plus modulation,
                   written in the previous channel's window (EOS writes +$46
                   = 0 there first).
    Per-chip registers at +$40/$42 of channels 0 and 16 (output setup) and
    +$44/$46 of channel 0 (written 4 at boot).

    Filter, as decoded from EOS's words for all 21 filter types and measured
    on an e6400 (eosdocs/e6400_hardware.md; samplebot session
    2026-10-20_e6400_filter). The sections follow Rossum's ARMAdillo coding
    (US 5,170,369; IEEE ASSP workshop 1991):
    - Each word is one second-order polynomial 1 + b1 z^-1 + b2 z^-2 at two
      morph endpoints: the high half at cutoff 0, the low half at cutoff
      255. Each half is two 8-bit codes, [radius][frequency], interpolated
      linearly in the coded domain by x = cutoff / 255.
    - Codes are 16 per octave: 1 - R = 2^((radius - 252) / 16) and
      V = |1 - p|^2 = 1 + b1 + b2 = 2^((frequency - 206.5) / 16), so
      b2 = R^2 and b1 = (1 - R^2) + V - 2. Radius 0xFF (R = 0) makes the
      polynomial first order; 0xFFCFFFCF is unused (a root near 0).
    - The pole polynomials divide and the zero polynomials multiply, all in
      cascade. The section at +$00 is DC-normalised (numerator gain V, the
      patent's DC mode); those at +$10 and +$20 are too when bit 7 of the
      coefficient byte is 0 (the low-pass and vocal types).
    - The second byte of +$24 is a gain, 0.75 dB per step below 0xF2 (the
      low-pass Q compensation). The steps are smaller at high cutoff and
      larger at low cutoff: -0.75 + 0.224 (x - 0.78) dB each, fitted to the
      passband of the 2-pole Q ladders at cutoff 64-240 and the 4-pole ones
      at cutoff 32-192 (0.46 dB rms; session 2026-10-20_e6400_rung). The
      hardware level follows the Q word rather than this byte: Q 120 and 127
      share Q 112's word and measure the same despite one more step.
    With only the two code constants this predicts the measured shapes to
    0.25-0.6 dB rms for the 2-pole low-pass and 0.4-0.7 dB for high-pass,
    band-pass, swept EQ and the EQ morphs; 0.5-1.4 dB for the flanger and
    phasers below cutoff 208; 1-4 dB for the vocal types, Phaser 2 at high
    cutoff, and the 4-pole and contrary band-pass at high Q.

    Volume, measured (samplebot sessions 2026-10-20_e6400_velocity and
    2026-10-20_e6400_note-2): 0.375 dB per step (16.12 dB over the 43 steps
    between velocities 100 and 8) up to about 219. Above that the amplitude
    is proportional to (level - 196), which joins with the same slope and
    gives 0.28, 0.22 and 0.17 dB per step at 227, 235 and 247; 255 is 5.3 dB
    below the 0.375 dB line (session 2026-10-20_e6400_rung, voice volume
    ladders at centre and hard pan). The level glides down to a lower target
    at about 7/256 step per sample with the latch at 1 (0.32 dB rms on the
    release), and rises within about 1 ms at note start.

    Mix: the chips are chained, each adding its channels to the previous
    chip's mix (CSCDI in, CSCDO out; the last one's DACD feeds the DACs).
    The mix saturates symmetrically (flat tops) at 3.55 times one full-scale
    voice at volume 255 (samplebot session 2026-10-20_e6400_rung: up to four
    unison voices on a 750 Hz sine, master volume at maximum). The filter
    itself stayed linear on a resonant 4-pole low-pass with its output 11 dB
    above a full-scale input.

    TODO:
    - Band-pass (2nd/4th order) and both phasers measure 7 and 14 dB quieter
      at Q 64 and 127 than this model (a flat gain, twice for the 4th order,
      which keeps the band-pass peak near 0 dB); other types with the same
      radius changes don't. The vocal types are 9.6 dB quieter, the swept
      EQs and EQ morphs 3 dB. What selects these gains is not known, nor the
      role of +$24's other bytes.
    - The cutoff is applied at once; the chip interpolates it (probably over
      the approach time the +$46 latch selects, US 5,170,369).
    - Fall rates for the latch values other than 1, and the rise rate, are
      not measured (the 2^n scaling below is assumed).
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
constexpr double LEVEL_LINEAR_ZERO = 196.0;   // above the knee (about 219) the amplitude is proportional to level - this
constexpr u16 RISE_RATE = 5 << 8;   // 8.8 steps per sample: 255 steps in about 1 ms

constexpr u16 fall_rate(u16 latch)
{
	// 7/256 step per sample for latch 1 (measured), scaled by 2 per latch step (assumed)
	return std::max(u16(1), u16((7U << std::min<u16>(latch, 8)) >> 1));
}

constexpr u32 WORD_UNUSED = 0xffcfffcf;

// ARMAdillo code decoding, 16 codes per octave (fitted to the 2-pole low-pass ladders)
constexpr double RADIUS_CODE_UNITY = 252.0;     // 1 - R = 2^((code - this) / 16)
constexpr double FREQUENCY_CODE_UNITY = 206.5;  // V = |1 - p|^2 = 2^((code - this) / 16)
constexpr double CODES_PER_OCTAVE = 16.0;

constexpr u8 GAIN_UNITY = 0xf2;                 // +$24 second byte
constexpr double GAIN_DB_PER_STEP = 0.75;
constexpr double GAIN_CUTOFF_SLOPE = 0.224;     // dB per step per unit of cutoff, from the pivot below
constexpr double GAIN_CUTOFF_PIVOT = 0.78;

struct polynomial { double b1, b2, v; };

// The polynomial 1 + b1 z^-1 + b2 z^-2 of a word at cutoff x (0-1), and its value V at z = 1
polynomial decode(u32 word, double x)
{
	const double r0 = (word >> 24) & 0xff, f0 = (word >> 16) & 0xff, r1 = (word >> 8) & 0xff, f1 = word & 0xff;
	const double radius = r0 + x * (r1 - r0), frequency = f0 + x * (f1 - f0);
	const double r = std::max(0.0, 1.0 - std::pow(2.0, (radius - RADIUS_CODE_UNITY) / CODES_PER_OCTAVE));
	const double v = std::pow(2.0, (frequency - FREQUENCY_CODE_UNITY) / CODES_PER_OCTAVE);
	const double b2 = r * r;
	return polynomial{ (1.0 - b2) + v - 2.0, b2, v };
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
	m_stream = stream_alloc(NUM_CHANNELS + 2, 2, clock() / 1024);
	build_gain_table();

	save_item(NAME(m_regs));
	save_item(NAME(m_level));
	save_item(NAME(m_target));
	save_item(NAME(m_fall));
	save_item(NAME(m_state));
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
		for (int s = 0; s < SECTIONS; s++)
			m_section[ch][s] = section_coefs();
		std::fill(std::begin(m_state[ch]), std::end(m_state[ch]), 0.0);
		m_filter_gain[ch] = 1.0f;
	}
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
	// level 0 is off; up to LEVEL_KNEE, 0.375 dB per step; above it the amplitude is linear in
	// (level - LEVEL_LINEAR_ZERO), joining with the same slope; 1.0 at 255
	const double knee = LEVEL_LINEAR_ZERO + 20.0 / (DB_PER_STEP * std::log(10.0));
	const auto curve = [knee] (double steps)
	{
		if (steps >= knee)
			return (steps - LEVEL_LINEAR_ZERO) / (knee - LEVEL_LINEAR_ZERO);
		return std::pow(10.0, (steps - knee) * DB_PER_STEP / 20.0);
	};
	const double top = curve(255.0);
	m_gain[0] = 0.0f;
	for (int i = 1; i < (1 << GAIN_BITS); i++)
		m_gain[i] = float(curve(double(i) / (1 << (GAIN_BITS - 8))) / top);
}

void emu_hchip_device::update_filter(int ch)
{
	// the cutoff and bit 7 of the coefficient byte are in the previous channel's window
	const int prev = prev_channel(ch);
	const int cutoff = reg8(prev, 0x7c);
	const bool normalise_all = !BIT(reg8(prev, 0x7b), 7);
	const double x = cutoff / 255.0;

	for (int s = 0; s < SECTIONS; s++)
	{
		const u32 poles = reg32(ch, s << 4), zeros = (s < SECTIONS - 1) ? reg32(ch, (s << 4) | 4) : WORD_UNUSED;
		section_coefs &sc = m_section[ch][s];
		sc = section_coefs();
		if (poles == WORD_UNUSED && zeros == WORD_UNUSED)
			continue;
		sc.active = true;

		double gain = 1.0;
		if (poles != WORD_UNUSED)
		{
			polynomial p = decode(poles, x);
			// keep the poles inside the unit circle: a real root stays above -1 while V < 2 (1 + b2)
			const double vmax = 1.999 * (1.0 + p.b2);
			if (p.v > vmax)
				p = polynomial{ (1.0 - p.b2) + vmax - 2.0, p.b2, vmax };
			sc.b1 = p.b1;
			sc.b2 = p.b2;
			if (s == 0 || normalise_all)
				gain = p.v;
		}
		if (zeros != WORD_UNUSED)
		{
			const polynomial z = decode(zeros, x);
			sc.n0 = gain;
			sc.n1 = gain * z.b1;
			sc.n2 = gain * z.b2;
		}
		else
		{
			sc.n0 = gain;
		}
	}

	const u32 gain_word = reg32(ch, 0x24);
	const int gain_code = (gain_word == WORD_UNUSED) ? GAIN_UNITY : int((gain_word >> 16) & 0xff);
	const int gain_steps = GAIN_UNITY - gain_code;
	const double gain_db = -gain_steps * (GAIN_DB_PER_STEP - GAIN_CUTOFF_SLOPE * (x - GAIN_CUTOFF_PIVOT));
	m_filter_gain[ch] = float(std::pow(10.0, gain_db / 20.0));

	LOGFILTER("ch%02d cutoff %d%s gain %d: %08x %08x %08x %08x %08x %08x\n", ch, cutoff, normalise_all ? " (DC all)" : "", gain_code - GAIN_UNITY,
			reg32(ch, 0x00), reg32(ch, 0x04), reg32(ch, 0x10), reg32(ch, 0x14), reg32(ch, 0x20), gain_word);
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

	case 0x7a: // +$7B: bit 7 is the next channel's
		if (ACCESSING_BITS_0_7)
			update_filter(next_channel(ch));
		break;

	case 0x7c: // filter cutoff of the next channel
		if (ACCESSING_BITS_8_15)
			update_filter(next_channel(ch));
		break;
	}
}

void emu_hchip_device::sound_stream_update(sound_stream &stream)
{
	stream.copy(0, CASCADE_INPUT);
	stream.copy(1, CASCADE_INPUT + 1);

	for (int ch = 0; ch < NUM_CHANNELS; ch++)
	{
		u16 level[2] = { m_level[ch][0], m_level[ch][1] };
		const u16 target[2] = { u16(u16(m_target[ch][0]) << 8), u16(u16(m_target[ch][1]) << 8) };
		if (!level[0] && !level[1] && !target[0] && !target[1])
			continue;

		const section_coefs *const sc = m_section[ch];
		const float filter_gain = m_filter_gain[ch];
		for (int i = 0; i < stream.samples(); i++)
		{
			double x = stream.get(ch, i);
			for (int s = 0; s < SECTIONS; s++)
			{
				if (!sc[s].active)
					continue;
				double *const st = &m_state[ch][s << 1];
				const double y = sc[s].n0 * x + st[0];
				st[0] = sc[s].n1 * x - sc[s].b1 * y + st[1];
				st[1] = sc[s].n2 * x - sc[s].b2 * y;
				x = y;
			}
			const float out = float(x) * filter_gain;

			for (int s = 0; s < 2; s++)
			{
				if (level[s] < target[s])
					level[s] = std::min<u32>(level[s] + RISE_RATE, target[s]);
				else if (level[s] > target[s])
					level[s] = std::max<int>(level[s] - m_fall[ch][s], target[s]);
				stream.add(s, i, out * m_gain[level[s] >> (16 - GAIN_BITS)]);
			}
		}
		m_level[ch][0] = level[0];
		m_level[ch][1] = level[1];
	}

	for (int s = 0; s < 2; s++)
		for (int i = 0; i < stream.samples(); i++)
			stream.put(s, i, std::clamp<sound_stream::sample_t>(stream.get_output(s, i), -MIX_FULL_SCALE, MIX_FULL_SCALE));
}
