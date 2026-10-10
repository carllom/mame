// license:BSD-3-Clause
// copyright-holders:Valley Bell

// register write:
//  00/01 - ??
//  02/03 - ROM bank (bits 10-13) / loop mode (bits 14-15)
//  04/05 - frequency (2.14 fixed point, 0x4000 = 32000 Hz)
//  06 - envelope rate (signed float, see below; negative = ramp down)
//  07 - envelope target level (float, see below)
//  08/09 - sample start address, fraction (2.14 fixed point, i.e. 1 byte = 0x4000)
//  0A/0B - sample start address (high word, i.e. address bits 2..17)
//  0C/0D - sample end address (high word)
//  0E/0F - sample loop address (high word)
//  10 - select voice: 02/03 read envelope level bits 0-15
//  12 - select voice: 02/03 read envelope level bits 16-25
//  11/13/15/17 - voice enable mask (11 = least significant 8 bits, 17 = most significant 8 bits)
//  16 - interrupt acknowledge (the voice number read from 00)
//  19/1B/1D - ?? (written at init time)
//  1A - select voice: 02/03 read the playback address (high word, same units as 0C-0F)
//  1F - voice select
//
// register read:
//  00 - voice with a pending envelope event; also selects it for 02/03
//  01 - last sample data (used by main CPU to read sample table from PCM ROM)
//  02/03 - envelope level or playback address, as selected by 10/12/1A, or
//          after reading 00 the event voice's rate (02) and target (03)
//
// Envelope (MV-30 firmware and hardware recordings):
//  Rate and target are 8-bit floats e<<4|m.  The level is a 26-bit linear
//  amplitude; target code c means level (16+m) << (6+e), so 0xFF is 31 << 21
//  and each 0x10 step is one octave (6 dB).  The rate is signed: the firmware
//  negates it (0x81 = -0x7F) when the target is below the level.  The level
//  ramps linearly towards the target by (8 + (r & 7)) << ((r >> 3) + 3) per
//  sample, where r is the rate's magnitude: every 8 steps of r double the
//  speed.
//
// Envelope events:
//  The chip raises an interrupt for a voice that sits at its target.  The
//  firmware reads 00 (voice), acknowledges through 16, reads 02/03 (rate and
//  target) and writes the next segment to 06/07; decay and release are chains
//  of segments, each one octave lower.  The MV-30 handler acts on every other
//  event of a voice and writes the same rate/target back on the others, so a
//  voice at its target must keep raising events.  Here the chip scans one
//  voice every ENV_SCAN_SAMPLES samples (5 ms per voice), which gives the
//  hardware's release staircase of one octave about every 10 ms, with the
//  first step after note-off 5 or 10 ms later.  The scan is a model fitted to
//  those recordings, not a known circuit.

#include "emu.h"
#include "roland_lp.h"

#include <bit>


DEFINE_DEVICE_TYPE(MB87419_MB87420, mb87419_mb87420_device, "mb87419_mb87420", "Roland LP MB87419/MB87420 PCM")

mb87419_mb87420_device::mb87419_mb87420_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, MB87419_MB87420, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_int_callback(*this)
	, m_clock(0)
	, m_rate(0)
	, m_stream(nullptr)
	, m_scan_timer(nullptr)
	, m_irq_state(false)
	, m_event_pending(0)
	, m_scan_voice(0)
	, m_sel_chn(0)
	, m_probe_slot(0)
	, m_probe_mode(PROBE_LEVEL_LO)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void mb87419_mb87420_device::device_start()
{
	m_clock = clock() / 2;
	m_rate = m_clock / 512; // usually 32 KHz

	m_stream = stream_alloc(0, VOICE_OUTPUT + NUM_CHANNELS, m_rate);
	m_scan_timer = timer_alloc(FUNC(mb87419_mb87420_device::env_scan_tick), this);

	save_item(NAME(m_irq_state));
	save_item(NAME(m_event_pending));
	save_item(NAME(m_scan_voice));
	save_item(NAME(m_sel_chn));
	save_item(NAME(m_probe_slot));
	save_item(NAME(m_probe_mode));

	logerror("Roland PCM: Clock %u, Rate %u\n", m_clock, m_rate);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void mb87419_mb87420_device::device_reset()
{
	m_irq_state = false;
	m_event_pending = 0;
	m_scan_voice = 0;
	m_probe_slot = 0;
	m_probe_mode = PROBE_LEVEL_LO;
	m_int_callback(CLEAR_LINE);

	// one sample is 1024 input clocks
	const attotime scan_period = attotime::from_ticks(ENV_SCAN_SAMPLES * 1024, clock());
	m_scan_timer->adjust(scan_period, 0, scan_period);
}

void mb87419_mb87420_device::update_irq()
{
	const bool state = m_event_pending != 0;
	if (state != m_irq_state)
	{
		m_irq_state = state;
		m_int_callback(state ? ASSERT_LINE : CLEAR_LINE);
	}
}

TIMER_CALLBACK_MEMBER(mb87419_mb87420_device::env_scan_tick)
{
	m_stream->update();
	const unsigned voice = m_scan_voice;
	m_scan_voice = (m_scan_voice + 1) & (NUM_CHANNELS - 1);

	const pcm_channel &chn = m_chns[voice];
	if (chn.enable && chn.env_level == env_target_level(chn.env_target))
	{
		m_event_pending |= 1U << voice;
		update_irq();
	}
}

//-------------------------------------------------
//  rom_bank_pre_change - refresh the stream if the
//  ROM banking changes
//-------------------------------------------------

void mb87419_mb87420_device::rom_bank_pre_change()
{
	// unused right now
	m_stream->update();
}


u8 mb87419_mb87420_device::read(offs_t offset)
{
	// Note: only offset 0x01 is verified, the rest is probably all wrong
	if (offset != 0x01)
		logerror("Reading Reg %02X\n", offset);
	if (offset < 0x10)
	{
		pcm_channel& chn = m_chns[m_sel_chn];
		switch(offset)
		{
		case 0x00:  // voice with a pending envelope event
			{
				const uint8_t voice = m_event_pending ? std::countr_zero(m_event_pending) : 0;
				if (m_event_pending && !machine().side_effects_disabled())
				{
					m_probe_slot = voice;
					m_probe_mode = PROBE_EVENT;
				}
				return voice;
			}
		case 0x01:
			m_stream->update();
			{
				// LP readback is biased two bytes ahead of the address latched in
				// D808-D80B.  The firmware compensates by programming RW28 - 2 in
				// LpReadByte() before reading D801, so emulate the hardware bias
				// here rather than in the caller-visible register image.
				//
				// The firmware uses this path to walk PCM ROM during setup while
				// the voice is disabled, so chn.addr is not yet a valid playback
				// position — use chn.start instead.
				offs_t addr = ((chn.start + (2U << 14)) >> 14) | ((chn.bank & 0x3C00) << 8);
				logerror("WaveROM read addr=%06X (%02X)\n", addr, read_byte(addr));
				return read_byte(addr);
			}
		case 0x02:  // readback selected by 10/12/1A/00 (low byte / high byte)
		case 0x03:
			{
				// MV-30 firmware: at note-off LpStartReleaseFromLevel reads the 26-bit
				// envelope level through 10 then 12 and sets the release target one
				// octave below it.  LpCheckLoopPointReached reads the playback address
				// through 1A and compares it with the loop word.  SoundEngineIsr reads
				// 00 and then 02/03 as the event voice's rate and target.
				m_stream->update();
				const pcm_channel& probe = m_chns[m_probe_slot & (NUM_CHANNELS - 1)];
				uint16_t value;
				switch (m_probe_mode)
				{
				case PROBE_LEVEL_LO: value = probe.env_level & 0xFFFF; break;
				case PROBE_LEVEL_HI: value = (probe.env_level >> 16) & 0x03FF; break;
				case PROBE_ADDRESS:  value = probe.addr >> 16; break;
				default:             value = (probe.env_target << 8) | probe.env_rate; break;
				}
				return (value >> ((offset & 1) ? 8 : 0)) & 0xFF;
			}
		case 0x04:  // sample step LSB
			return (chn.step >> 0) & 0xFF;
		case 0x05:  // sample step LSB
			return (chn.step >> 8) & 0xFF;
		case 0x06:  // envelope rate (readback unverified)
			return chn.env_rate;
		case 0x07:  // envelope target (readback unverified)
			return chn.env_target;
		case 0x08:  // current address, fraction LSB
			return (chn.start >>  0) & 0xFF;
		case 0x09:  // current address, fraction MSB
			return (chn.start >>  8) & 0xFF;
		case 0x0A:  // current address LSB
			return (chn.start >> 16) & 0xFF;
		case 0x0B:  // current address MSB
			return (chn.start >> 24) & 0xFF;
		case 0x0C:  // sample end address LSB
			return (chn.end >> 0) & 0xFF;
		case 0x0D:  // sample end address MSB
			return (chn.end >> 8) & 0xFF;
		case 0x0E:  // sample loop address LSB
			return (chn.loop >> 0) & 0xFF;
		case 0x0F:  // sample loop address MSB
			return (chn.loop >> 8) & 0xFF;
		}
	}
	else
	{
		switch(offset)
		{
		case 0x11:
		case 0x13:
		case 0x15:
		case 0x17:
			m_stream->update();
			{
				uint8_t basechn = ((offset >> 1) & 0x03) * 8;
				uint8_t result = 0x00;
				for (uint8_t ch_id = 0; ch_id < 8; ch_id ++)
					result |= (m_chns[basechn + ch_id].enable << ch_id);
				return result;
			}
			break;
		case 0x1F:
			return m_sel_chn;
		}
	}
	return 0x00;
}

void mb87419_mb87420_device::write(offs_t offset, u8 data)
{
	logerror("Reg %02X = %02X\n", offset, data);

	if (offset < 0x10)
	{
		pcm_channel& chn = m_chns[m_sel_chn];
		switch(offset)
		{
		case 0x00:
			chn.mode = (chn.mode & 0xFF00) | (data << 0);
			break;
		case 0x01:
			chn.mode = (chn.mode & 0x00FF) | (data << 8);
			break;
		case 0x02:  // ROM bank LSB
			chn.bank = (chn.bank & 0xFF00) | (data << 0);
			break;
		case 0x03:  // ROM bank MSB / loop mode
			chn.bank = (chn.bank & 0x00FF) | (data << 8);
			break;
		case 0x04:  // sample step LSB
			chn.step = (chn.step & 0xFF00) | (data << 0);
			break;
		case 0x05:  // sample step LSB
			chn.step = (chn.step & 0x00FF) | (data << 8);
			break;
		case 0x06:  // envelope rate
			m_stream->update();
			chn.env_rate = data;
			break;
		case 0x07:  // envelope target; the level ramps towards it at the rate in 06
			m_stream->update();
			chn.env_target = data;
			break;
		case 0x08:  // current address, fraction LSB
			chn.start = (chn.start & 0xFFFFFF00) | (data <<  0);
			if (chn.enable)
				logerror("Roland PCM, channel %u: changing start address while playing!\n", m_sel_chn);
			break;
		case 0x09:  // current address, fraction MSB
			chn.start = (chn.start & 0xFFFF00FF) | (data <<  8);
			if (chn.enable)
				logerror("Roland PCM, channel %u: changing start address while playing!\n", m_sel_chn);
			break;
		case 0x0A:  // current address LSB
			chn.start = (chn.start & 0xFF00FFFF) | (data << 16);
			if (chn.enable)
				logerror("Roland PCM, channel %u: changing start address while playing!\n", m_sel_chn);
			break;
		case 0x0B:  // current address MSB
			chn.start = (chn.start & 0x00FFFFFF) | (data << 24);
			if (chn.enable)
				logerror("Roland PCM, channel %u: changing start address while playing!\n", m_sel_chn);
			break;
		case 0x0C:  // sample end address, LSB
			chn.end = (chn.end & 0xFF00) | (data << 0);
			break;
		case 0x0D:  // sample end address MSB
			chn.end = (chn.end & 0x00FF) | (data << 8);
			break;
		case 0x0E:  // sample loop address LSB
			chn.loop = (chn.loop & 0xFF00) | (data << 0);
			break;
		case 0x0F:  // sample loop address MSB
			chn.loop = (chn.loop & 0x00FF) | (data << 8);
			break;
		}
	}
	else
	{
		switch(offset)
		{
		case 0x10:  // select voice: 02/03 read envelope level bits 0-15
			m_probe_slot = data;
			m_probe_mode = PROBE_LEVEL_LO;
			break;
		case 0x12:  // select voice: 02/03 read envelope level bits 16-25
			m_probe_slot = data;
			m_probe_mode = PROBE_LEVEL_HI;
			break;
		case 0x1A:  // select voice: 02/03 read the playback address
			m_probe_slot = data;
			m_probe_mode = PROBE_ADDRESS;
			break;
		case 0x11:
		case 0x13:
		case 0x15:
		case 0x17:
			{
				m_stream->update();
				uint8_t basechn = ((offset >> 1) & 0x03) * 8;
				for (uint8_t ch_id = 0; ch_id < 8; ch_id ++)
				{
					pcm_channel& chn = m_chns[basechn + ch_id];
					bool play = static_cast<bool>((data >> ch_id) & 1);

					if (play && ! chn.enable)
					{
						chn.addr = chn.start;
						offs_t addr = (chn.addr >> 14) | ((chn.bank & 0x3C00) << 8);
						chn.smpl_cur = 0;
						chn.smpl_nxt = decode_sample((int8_t)read_byte(addr));
						chn.play_dir = +1;
						chn.env_level = 0;
						logerror("Starting channel %u, bank 0x%04X, addr 0x%05X.%03X\n",
							basechn + ch_id, chn.bank, chn.start >> 14, (chn.start & 0x3FFF) << 2);
						logerror("Smpl End Ofs: 0x%04X, Loop Ofs 0x%04X, Step 0x%04X, Env Target %02X Rate %02X\n",
							chn.end << 2, chn.loop << 2, chn.step, chn.env_target, chn.env_rate);
					}
					chn.enable = play;
					if (!play)
						m_event_pending &= ~(1U << (basechn + ch_id));
				}
				update_irq();
			}
			break;
		case 0x16:
			// IRQ acknowledge.  The firmware ISR reads D800 (voice id of
			// the pending event) then writes the same value back to D816
			// to dequeue it.  The line drops and rises again if more events
			// are pending, since MCS-96 EXTINT is rising-edge triggered.
			m_event_pending &= ~(1U << (data & (NUM_CHANNELS - 1)));
			if (m_irq_state)
			{
				m_irq_state = false;
				m_int_callback(CLEAR_LINE);
			}
			update_irq();
			break;
		case 0x1F:
			m_sel_chn = data & 0x1F;
			break;
		default:
			logerror("Writing unknown reg %02X = %02X\n", offset, data);
			break;
		}
	}
}

//-------------------------------------------------
//  sound_stream_update - handle a stream update
//-------------------------------------------------

void mb87419_mb87420_device::sound_stream_update(sound_stream &stream)
{
	for (unsigned voice = 0; voice < NUM_CHANNELS; voice++)
	{
		pcm_channel &chn = m_chns[voice];
		if (! chn.enable || chn.play_dir == 0)
			continue;

		for (int smpl = 0; smpl < stream.samples(); smpl ++)
		{
			// Envelope: linear ramp towards the target, one step per sample.
			// Ramping down is 16 times slower than ramping up at the same rate
			// (MV-30 release Ladder: offset o down matches offset o-4 up).
			const uint32_t target = env_target_level(chn.env_target);
			const uint32_t env_inc = env_step(chn.env_rate);
			if (chn.env_level < target)
				chn.env_level = std::min(target, chn.env_level + env_inc);
			else if (chn.env_level > target)
			{
				const uint32_t env_dec = std::max<uint32_t>(env_inc >> 4, 1);
				chn.env_level = (chn.env_level - target > env_dec) ? (chn.env_level - env_dec) : target;
			}

			s32 smp_data;
			if (chn.play_dir > 0)
				smp_data = sample_interpolate(chn.smpl_cur, chn.smpl_nxt, chn.addr & 0x3FFF);
			else
				smp_data = sample_interpolate(chn.smpl_nxt, chn.smpl_cur, chn.addr & 0x3FFF);
			smp_data = smp_data * s32(chn.env_level >> 10); // 26-bit level -> 16-bit multiplier
			stream.add_int(0, smpl, smp_data, 32768 << 14); // >>14 results in a good overall volume
			stream.add_int(1, smpl, smp_data, 32768 << 14);
			stream.put_int(VOICE_OUTPUT + voice, smpl, smp_data, 32768 << 14);

			uint32_t old_addr = chn.addr;
			if (chn.play_dir > 0)
				chn.addr += chn.step;
			else if (chn.play_dir < 0)
				chn.addr -= chn.step;

			// Note: The sample data is read after incrementing the address, but before handling loop points.
			//       Reading the sample data after loop point handling breaks the test mode square wave.
			//       (Sample 0xA7 on the CM-32P.)
			if ((chn.addr >> 14) != (old_addr >> 14))
			{
				offs_t addr = (chn.addr >> 14) | ((chn.bank & 0x3C00) << 8);
				//logerror("Chn %u: sample addr 0x%05X.%03X\n", &chn - &m_chns[0], chn.addr >> 14, (chn.addr & 0x3FFF) << 2);
				chn.smpl_cur = chn.smpl_nxt;
				chn.smpl_nxt += decode_sample((int8_t)read_byte(addr)); // This was verified to be independent from play_dir.

				// until the decoding is fixed, we prevent overflow bugs (due to DC offsets when looping) this way
				chn.smpl_nxt = std::clamp<int16_t>(chn.smpl_nxt, -0x7FF, +0x7FF);
			}

			bool reachedEnd = false;
			if (chn.play_dir > 0)
			{
				// This works well with the test mode sine/square wave samples.
				if ((chn.addr >> 16) >= chn.end)
					reachedEnd = true;
			}
			else if (chn.play_dir < 0)
			{
				if ((chn.addr >> 16) < chn.loop)
					reachedEnd = true;
			}
			if (reachedEnd)
			{
				offs_t oldAddr = chn.addr;
				logerror("Chn %u: Sample End at addr 0x%05X.%03X. (Dir %d, Loop Mode %u)\n",
					&chn - &m_chns[0], chn.addr >> 14, (chn.addr & 0x3FFF) << 2,
					chn.play_dir, chn.bank >> 14);
				switch(chn.bank >> 14)
				{
				case 0: // normal loop
					chn.addr = chn.addr + ((chn.loop - chn.end) << 16);
					logerror("addr 0x%05X.%03X -> addr 0x%05X.%03X\n",
						oldAddr >> 14, (oldAddr & 0x3FFF) << 2,
						chn.addr >> 14, (chn.addr & 0x3FFF) << 2);
					break;
				case 1: // no loop
				case 3: // invalid, assume "no loop"
					chn.play_dir = 0;
					break;
				case 2: // ping-pong
					// CM-32P samples with ping-pong loop mode:
					//  0x00 (low piano note), 0x58..0x67, 0x69..0x6C (choir/strings), 0x9F..0xA4 (brass)
					// Note: These formulae are probably incorrect, as they cause some DC offset in most samples.
					//       Reference sample rate for C5 @ Sample 9F: 34290 Hz.
					if (chn.play_dir > 0)
						chn.addr = (chn.end << 16) - chn.addr + (chn.end << 16);
					else if (chn.play_dir < 0)
						chn.addr = (chn.loop << 16) - chn.addr + (chn.loop << 16);
					chn.play_dir = -chn.play_dir;
					break;
				}
				if (chn.play_dir == 0)
					break;
			}
		}
	}

	return;
}

int16_t mb87419_mb87420_device::decode_sample(int8_t data)
{
	int16_t val;
	int16_t sign;
	uint8_t shift;
	int16_t result;

	if (data < 0)
	{
		sign = -1;
		val = -data;
	}
	else
	{
		sign = +1;
		val = data;
	}

	// thanks to Sarayan for figuring out the decoding formula
	shift = val >> 4;
	val &= 0x0F;
	if (! shift)
		result = val;
	else
		result = (0x10 + val) << (shift - 1);
	return result * sign;
}

// Target code e<<4|m -> 26-bit level (16+m) << (6+e); e = 0 is denormal (m << 7).
uint32_t mb87419_mb87420_device::env_target_level(uint8_t target)
{
	const unsigned e = target >> 4;
	const unsigned m = target & 0x0F;
	return (e ? ((0x10 + m) << (e - 1)) : m) << 7;
}

// Signed rate (negative when ramping down) -> level step per sample.  The
// magnitude is a float with a 3-bit mantissa: (8 + bits 2-0) << (bits 6-3),
// so every 8 steps of the rate double the speed.  Fitted to MV-30 attack
// Ladders with rates 08..7F (rate 0x1F takes 2.1 s from 0 to target 0xFF).
uint32_t mb87419_mb87420_device::env_step(uint8_t rate)
{
	const unsigned magnitude = std::min(std::abs(int(int8_t(rate))), 0x7F);
	return (8 + (magnitude & 0x07)) << ((magnitude >> 3) + 3);
}

int16_t mb87419_mb87420_device::sample_interpolate(int16_t smp1, int16_t smp2, uint16_t frac)
{
	int32_t smpfrac0 = (int32_t)smp1 * (0x4000 - frac);
	int32_t smpfrac1 = (int32_t)smp2 * frac;
	return (int16_t)((smpfrac0 + smpfrac1) >> 14);
}
