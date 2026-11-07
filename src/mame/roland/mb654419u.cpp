// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Fujitsu MB654419U (Roland TVF gate array) + TVF-16 filter chip

    Per-voice filter and level for the SA-16 voices in the S-330 / W-30.
    Register usage from the S-330 system 1.03 and W-30 system 1.10 firmware.
    The filter is a Chamberlin state variable low-pass (12 dB/oct) running
    at the 30 kHz voice rate, whose two coefficients are the cutoff and
    resonance registers in 2.14 fixed point (0x4000 = 1.0).
    Fitted to recordings of a real S-330 (cutoff 40/70/100/127, resonance
    0/32/64/92/127; harmonic levels match within 1.6 dB rms):
        lp += f * bp;  hp = in - lp - q * bp;  bp += f * hp;  out = lp
        f = cutoff / 0x4000   (corner fs/pi * asin(f/2): 0x3bb0 -> ~4.6 kHz,
                               where resonance peaks; with low resonance the
                               top cutoff is nearly flat, -1.6 dB at 10 kHz)
        q = resonance / 0x4000 (Q = 1/q; 0x4000 -> Q 1, 0x196 -> Q ~40)
    Step responses measured on an S-330 through its A/D input ACH7 (the
    s330-hacking tvfstep and tvfres probes) agree: the cutoff within about
    5 % for 0x400-0x2000, and the fitted q within 9 % of resonance / 0x4000
    from 0x4000 down to 0x196. They also show the low-pass saturating at
    full scale: a full-level step's overshoot is cut off flat, while the
    level register can still take the output to twice full scale.
    Cutoff and level changes glide: the output follows a new value
    exponentially (tvfwrite and tvfsmooth probes: tau 3.3-3.8 ms for the
    level in either direction and any step size, about 2.9 ms for the
    cutoff). Parameters 3 and 4 set the rate of the cutoff and level glide
    (tvfparam and tvfglide probes): a one-pole glide, linear in the
    coefficient, in which the distance left is multiplied by
    k = parameter / 0x4000 every 0.44 ms for the level and every 0.55 ms
    for the cutoff. With 0x3800, as the firmware sets both, that is tau
    3.3 ms for the level and 4.1 ms for the cutoff; the level's tau matches
    within about 10 % from 0x2000 to 0x3f00, the cutoff's 50 % times within
    about 15 % from 0x3800 to 0x3f00 (a glide in the log domain would be
    1.5x slower than measured). The glides advance in these steps: a jump
    (k = 0) waits for the next update, so averaged over many writes it
    spreads over 0.46 ms for the level and about 0.6 ms for the cutoff,
    while a change at the TVF's input shows up at once (tvftick probe).
    One step multiplies the distance by the signed k, which also explains
    the other values: 0xffff jumps, 0x3fff and 0x4000 stop the glide,
    0x6000 and 0x7fff make it run away, 0xc000 (-1) flips between two
    values and 0x8000 (-2) does so diverging. Parameter 6 sets where the
    cutoff glide is now: a write makes the filter jump to that cutoff and
    glide on from there; parameter 7 does the same for the level (with the
    level glide frozen, the level stays at the value written).
    Resonance changes take effect at once.
    The meaning of most setup registers and the internal table are unknown.

    Host interface (even bytes):
      f000/f002  register address lo/hi
      f004/f006  register data lo/hi; the f006 write commits the register
      f008-f012  setup registers written once at init (f008=1b f00a=64
                 f00c=07/ff f00e=19 f012=0f, f010 command bytes)
    During init the firmware also streams 64 24-bit table entries through
    f004/f006 without setting an address; those writes are ignored here.

    Register address = parameter * 0x10 + slot, slot 1..16 (slot 16 is
    written as 0x10 of the previous parameter group, e.g. cutoff of slot 16
    at 0x10), i.e. channel = (address - 1) & 15, parameter = (address - 1) >> 4.
    The firmware maps SA-16 voice v to slot v+3 (v = 0..13), 1 (v = 14) and
    2 (v = 15), so voice v is channel (v + 2) & 15: the TVF slots run three
    ahead of the SA-16 voice order.

    Parameters (16-bit):
      0 (+0x00) cutoff: SVF frequency coefficient f (2.14), one semitone
                per firmware step, 10 .. 0x3bb0 (corner ~30 Hz .. 4.6 kHz);
                0x4000 is written when the tone's filter is switched off
      1 (+0x10) level: 0x4000 = unity (the firmware maximum is just below)
      2 (+0x20) resonance: SVF damping coefficient q (2.14), 0x4000 for
                resonance 0 (Q 1), halving every 24 steps of the tone
                parameter down to 0x196 (resonance 127)
      3 (+0x30) cutoff glide coefficient (2.14, signed), 0x3800 at init
      4 (+0x40) level glide coefficient (2.14, signed), 0x3800 at init; the
                firmware writes 0 when it starts setting up a voice (the new
                level takes effect at once) and 0x3800 again at key-on
      6 (+0x60) the gliding cutoff's current value; the firmware writes the
                cutoff here right after the cutoff register at voice start,
                so a new note doesn't glide from the previous note's cutoff
      7 (+0x70) the gliding level's current value, like parameter 6 for the
                cutoff (tvfp57 probe); never written by the firmware
      5 (+0x50) never written by the firmware; writing it alone changed
                neither the level nor the cutoff

    Debugger: the "regs" address space of this device shows the current
    registers as 16-bit words at parameter * 0x10 + channel (chip address
    minus one); view it with 16 words per row to get one parameter per row.
    Writes from the debugger take effect immediately.

    TODO:
    - The setup registers and the internal table.
    - Glide coefficients outside 0..0x4000 (the firmware only uses 0 and
      0x3800): here a runaway value is clamped to 0..0xffff, and a cutoff
      of 0x4000 or more passes the signal through. On an S-330 a runaway
      cutoff makes the output about 3 times louder (the filter oscillating?)
      and a level run away with 0x7fff ends at 0 (wrapping?).
    - Whether all channels step at the same time; the exact periods (0.44
      and 0.55 ms are 13.2 and 16.5 samples at 30 kHz).
    - Parameter 5, and the power-on values of the registers (they read 0).

***************************************************************************/

#include "emu.h"
#include "mb654419u.h"

#include <algorithm>
#include <cmath>
#include <iterator>


#define VERBOSE 0
#include "logmacro.h"

DEFINE_DEVICE_TYPE(MB654419U, mb654419u_device, "mb654419u", "Fujitsu MB654419U TVF")

namespace {

// the level and cutoff glides keep k of the remaining distance once per these
// periods, in seconds (fitted to an S-330: k = 0x3800 gives a level tau of
// 3.3 ms; a jump with k = 0 is spread over the same period)
constexpr double LEVEL_GLIDE_PERIOD = 0.00044;
constexpr double CUTOFF_GLIDE_PERIOD = 0.00055;

// largest value of a gliding coefficient (a 16-bit register in 2.14)
constexpr double GLIDE_MAX = double(0xffff) / 0x4000;

// one glide update: keep k (2.14, signed) of the distance to the target
double glide_step(double target, double now, u16 k)
{
	const double next = target + (now - target) * (double(s16(k)) / 0x4000);
	return (std::abs(next - target) < 1e-6) ? target : std::clamp(next, 0.0, GLIDE_MAX);
}

} // anonymous namespace

mb654419u_device::mb654419u_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, MB654419U, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_memory_interface(mconfig, *this)
	, m_regs_config("regs", ENDIANNESS_LITTLE, 16, 7, -1, address_map_constructor(FUNC(mb654419u_device::regs_map), this))
	, m_stream(nullptr)
	, m_address(0)
	, m_data(0)
	, m_address_set(false)
	, m_level_phase(0.0)
	, m_cutoff_phase(0.0)
	, m_output_level(0.0)
{
}

device_memory_interface::space_config_vector mb654419u_device::memory_space_config() const
{
	return space_config_vector{ std::make_pair(0, &m_regs_config) };
}

void mb654419u_device::regs_map(address_map &map)
{
	map(0x00, 0x7f).lrw16(
		NAME([this] (offs_t offset) -> u16 { return m_regs[offset >> 4][offset & 0x0f]; }),
		NAME([this] (offs_t offset, u16 data) { register_w(offset + 1, data); }));
}

void mb654419u_device::device_start()
{
	// one input per SA-16 voice, mono output
	m_stream = stream_alloc(NUM_CHANNELS, 1, SAMPLE_RATE_INPUT_ADAPTIVE);

	save_item(NAME(m_address));
	save_item(NAME(m_data));
	save_item(NAME(m_address_set));
	save_item(NAME(m_setup));
	save_item(NAME(m_regs));
	save_item(STRUCT_MEMBER(m_channel, lp));
	save_item(STRUCT_MEMBER(m_channel, bp));
	save_item(STRUCT_MEMBER(m_channel, f_cur));
	save_item(STRUCT_MEMBER(m_channel, level_cur));
	save_item(NAME(m_level_phase));
	save_item(NAME(m_cutoff_phase));
	save_item(NAME(m_output_level));
}

void mb654419u_device::device_post_load()
{
	// the filter coefficients are derived from the registers
	for (auto &ch : m_channel)
		ch.dirty = true;
}

void mb654419u_device::device_reset()
{
	m_address = 0;
	m_data = 0;
	m_address_set = false;
	std::fill(std::begin(m_setup), std::end(m_setup), 0);
	m_level_phase = m_cutoff_phase = 0.0;
	for (auto &param : m_regs)
		std::fill(std::begin(param), std::end(param), 0);
	for (int c = 0; c < NUM_CHANNELS; c++)
	{
		m_regs[PARAM_CUTOFF][c] = 0x4000;
		m_regs[PARAM_RESONANCE][c] = 0x4000;
		m_channel[c].lp = m_channel[c].bp = 0.0;
		m_channel[c].f_cur = 1.0;   // at the reset values, no glide
		m_channel[c].level_cur = 0.0;
		m_channel[c].dirty = true;
	}
}

u8 mb654419u_device::read(offs_t offset)
{
	// the firmware never reads the TVF
	return 0xff;
}

void mb654419u_device::write(offs_t offset, u8 data)
{
	switch (offset)
	{
	case 0: m_address = (m_address & 0xff00) | data; m_address_set = true; break;
	case 1: m_address = (m_address & 0x00ff) | (data << 8); m_address_set = true; break;
	case 2: m_data = (m_data & 0xff00) | data; break;
	case 3:
		m_data = (m_data & 0x00ff) | (data << 8);
		if (m_address_set)
			register_w(m_address, m_data);
		else
			LOG("table data %04x\n", m_data);
		m_address_set = false;
		break;
	case 4: case 5: case 6: case 7: case 8: case 9:
		LOG("setup %02x <= %02x\n", 0x08 + 2 * (offset - 4), data);
		m_setup[offset - 4] = data;
		break;
	default:
		LOG("write %02x <= %02x\n", offset * 2, data);
		break;
	}
}

void mb654419u_device::register_w(u16 address, u16 data)
{
	if (address == 0)
	{
		LOG("reg 00 <= %04x\n", data);
		return;
	}
	const int channel = (address - 1) & (NUM_CHANNELS - 1);
	const int param = ((address - 1) >> 4) & (NUM_PARAMS - 1);

	m_stream->update();
	m_regs[param][channel] = data;
	if (param == PARAM_CUTOFF || param == PARAM_LEVEL || param == PARAM_RESONANCE)
		m_channel[channel].dirty = true;
	else if (param == PARAM_CUTOFF_NOW)
		m_channel[channel].f_cur = double(data) / 0x4000; // the glide goes on from here
	else if (param == PARAM_LEVEL_NOW)
		m_channel[channel].level_cur = double(data) / 0x4000;
	LOG("%.4f param %d channel %2d <= %04x\n", machine().time().as_double(), param, channel, data);
}

void mb654419u_device::update_coefficients(int channel)
{
	channel_t &ch = m_channel[channel];
	ch.dirty = false;
	// 2.14 fixed-point coefficients of the Chamberlin SVF, and the level
	// (0x4000 = unity); the stream update glides towards f and level
	ch.f = double(m_regs[PARAM_CUTOFF][channel]) / 0x4000;
	ch.q = double(m_regs[PARAM_RESONANCE][channel]) / 0x4000;
	ch.level = double(m_regs[PARAM_LEVEL][channel]) / 0x4000;
}

void mb654419u_device::sound_stream_update(sound_stream &stream)
{
	// The coefficients are per sample: the stream must run at the SA-16 voice
	// rate (30 kHz), which SAMPLE_RATE_INPUT_ADAPTIVE gives us.
	for (int c = 0; c < NUM_CHANNELS; c++)
		if (m_channel[c].dirty)
			update_coefficients(c);

	// The cutoff and level glides advance in steps, once per glide period,
	// by the coefficients in parameters 3 and 4
	const double level_rate = 1.0 / (stream.sample_rate() * LEVEL_GLIDE_PERIOD);
	const double cutoff_rate = 1.0 / (stream.sample_rate() * CUTOFF_GLIDE_PERIOD);

	for (int i = 0; i < stream.samples(); i++)
	{
		m_level_phase += level_rate;
		const bool level_step = m_level_phase >= 1.0;
		if (level_step)
			m_level_phase -= 1.0;
		m_cutoff_phase += cutoff_rate;
		const bool cutoff_step = m_cutoff_phase >= 1.0;
		if (cutoff_step)
			m_cutoff_phase -= 1.0;

		double mix = 0.0;
		for (int v = 0; v < NUM_CHANNELS; v++)
		{
			// SA-16 voice v arrives in TVF channel (v + 2) & 15
			const int c = (v + 2) & (NUM_CHANNELS - 1);
			channel_t &ch = m_channel[c];
			double out = stream.get(v, i);

			if (cutoff_step)
				ch.f_cur = glide_step(ch.f, ch.f_cur, m_regs[PARAM_CUTOFF_GLIDE][c]);
			if (level_step)
				ch.level_cur = glide_step(ch.level, ch.level_cur, m_regs[PARAM_LEVEL_GLIDE][c]);

			// 0x4000 is what the firmware writes for a tone with the filter
			// switched off; passing through differs from running the SVF by
			// < 0.5 dB. While passing through, the state follows the input, so
			// that a glide down from 0x4000 starts without a jump.
			if (ch.f_cur >= 1.0)
			{
				ch.lp = out;
				ch.bp = 0.0;
			}
			else
			{
				// The low-pass state saturates at full scale: an overshoot past it
				// comes out flat, the undershoot that follows doesn't
				ch.lp = std::clamp(ch.lp + ch.f_cur * ch.bp, -1.0, 1.0);
				const double hp = out - ch.lp - ch.q * ch.bp;
				ch.bp += ch.f_cur * hp;
				out = ch.lp;
			}

			mix += out * ch.level_cur;
		}
		stream.put(0, i, mix);
		m_output_level = mix;
	}
}

double mb654419u_device::output_level()
{
	m_stream->update();
	return m_output_level;
}
