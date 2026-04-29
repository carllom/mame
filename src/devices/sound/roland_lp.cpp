// license:BSD-3-Clause
// copyright-holders:Valley Bell

// register write:
//  00/01 - ??
//  02/03 - ROM bank (bits 10-13) / loop mode (bits 14-15)
//  04/05 - frequency (2.14 fixed point, 0x4000 = 32000 Hz)
//  06/07 - volume
//  08/09 - sample start address, fraction (2.14 fixed point, i.e. 1 byte = 0x4000)
//  0A/0B - sample start address (high word, i.e. address bits 2..17)
//  0C/0D - sample end address (high word)
//  0E/0F - sample loop address (high word)
//  10/12 - ?? (sometimes value 1F is written)
//  11/13/15/17 - voice enable mask (11 = least significant 8 bits, 17 = most significant 8 bits)
//  19/1B/1D - ?? (written at init time)
//  1A - ??
//  1F - voice select
//
// register read:
//  01 - last sample data (used by main CPU to read sample table from PCM ROM)
//  02/03 - ?? (read after writing to 10/12)

#include "emu.h"
#include "roland_lp.h"


DEFINE_DEVICE_TYPE(MB87419_MB87420, mb87419_mb87420_device, "mb87419_mb87420", "Roland LP MB87419/MB87420 PCM")

mb87419_mb87420_device::mb87419_mb87420_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, MB87419_MB87420, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_rom_interface(mconfig, *this)
	, m_int_callback(*this)
	, m_clock(0)
	, m_rate(0)
	, m_stream(nullptr)
	, m_sel_chn(0)
	, m_probe_slot(0)
	, m_probe_half(0)
{
}

//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void mb87419_mb87420_device::device_start()
{
	m_clock = clock() / 2;
	m_rate = m_clock / 512; // usually 32 KHz

	m_stream = stream_alloc(0, 2, m_rate);
	m_irq_timer = timer_alloc(FUNC(mb87419_mb87420_device::irq_timer_tick), this);

	save_item(NAME(m_irq_state));
	save_item(NAME(m_sel_chn));
	save_item(NAME(m_probe_slot));
	save_item(NAME(m_probe_half));

	logerror("Roland PCM: Clock %u, Rate %u\n", m_clock, m_rate);
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void mb87419_mb87420_device::device_reset()
{
	m_irq_state = false;
	m_probe_slot = 0;
	m_probe_half = 0;
	m_int_callback(CLEAR_LINE);

	// IRQ is asserted only when there's a real per-voice event pending
	// (currently nothing feeds that queue — to be wired up alongside
	// the playback engine).  No periodic spurious IRQs.
	m_irq_timer->adjust(attotime::never);
}

//-------------------------------------------------
//  rom_bank_pre_change - refresh the stream if the
//  ROM banking changes
//-------------------------------------------------

TIMER_CALLBACK_MEMBER(mb87419_mb87420_device::irq_timer_tick)
{
	if (!m_irq_state)
	{
		m_irq_state = true;
		m_int_callback(ASSERT_LINE);
	}
}

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
		case 0x00:
			return (chn.mode >> 0) & 0xFF;
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
		case 0x02:  // probe-slot envelope counter (low or high byte of selected half)
		case 0x03:
			{
				// FUN_d5da, FUN_ea4b, FUN_e9f5 in firmware all read this via D810/D81A and
				// D812 to monitor the chip's internal envelope/volume counter — not the
				// address phase counter.  FUN_d5da compares the low 16 bits against a
				// per-slot threshold to detect "volume reached attack target".  FUN_ea4b
				// /FUN_e9f5 read four bytes (low half + high half), mask to 26 bits, and
				// treat zero as "voice fell silent" — that's the path that writes back
				// D807=0, D806=0 and recycles the slot.
				m_stream->update();
				const pcm_channel& probe = m_chns[m_probe_slot & (NUM_CHANNELS - 1)];
				if (m_probe_half)
					return 0;  // chn.volume is 16-bit; high half always zero
				return (probe.volume >> ((offset & 1) ? 8 : 0)) & 0xFF;
			}
		case 0x04:  // sample step LSB
			return (chn.step >> 0) & 0xFF;
		case 0x05:  // sample step LSB
			return (chn.step >> 8) & 0xFF;
		case 0x06:  // volume LSB
			return (chn.volume >> 0) & 0xFF;
		case 0x07:  // volume MSB
			return (chn.volume >> 8) & 0xFF;
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
		case 0x06:  // volume rate (added/subtracted per envelope tick toward target)
			m_stream->update();
			chn.vol_rate = data;
			break;
		case 0x07:  // volume target (high byte of 16-bit volume; chip ramps current toward it)
			m_stream->update();
			chn.vol_target = data;
			// Don't snap here.  The firmware's note-on sequence writes D807 before D806
			// (target first, then rate), so a snap-on-D807 would always make rate=0 at
			// the moment of the target write and skip every envelope ramp.  The chip
			// only needs to "start at zero" at note-on; we reset chn.volume in the
			// channel-enable transition below instead.
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
		case 0x10:  // probe slot select, low half (D802/D803 expose addr bits 0..15)
		case 0x1A:  // same low-half probe path used by the envelope/voice-end checker (FUN_d5da)
			m_probe_slot = data;
			m_probe_half = 0;
			break;
		case 0x12:  // probe slot select, high half (D802/D803 expose addr bits 16..31)
			m_probe_slot = data;
			m_probe_half = 1;
			break;
		case 0x11:
		case 0x13:
		case 0x15:
		case 0x17:
			{
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
						chn.volume = 0;
						chn.vol_phase = 0;
						logerror("Starting channel %u, bank 0x%04X, addr 0x%05X.%03X\n",
							basechn + ch_id, chn.bank, chn.start >> 14, (chn.start & 0x3FFF) << 2);
						logerror("Smpl End Ofs: 0x%04X, Loop Ofs 0x%04X, Step 0x%04X, Volume %04X\n",
							chn.end << 2, chn.loop << 2, chn.step, chn.volume);
					}
					chn.enable = play;
				}
			}
			break;
		case 0x16:
			// IRQ acknowledge.  The firmware ISR reads D800 (voice id of
			// the pending event) then writes the same value back to D816
			// to dequeue it and deassert the IRQ line.
			if (m_irq_state)
			{
				m_irq_state = false;
				m_int_callback(CLEAR_LINE);
			}
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
	for (auto& chn : m_chns)
	{
		if (! chn.enable || chn.play_dir == 0)
			continue;

		for (int smpl = 0; smpl < stream.samples(); smpl ++)
		{
			// Envelope ramp: chip integrates current volume toward target by rate per envelope tick.
			// Empirically the tick rate is ~32kHz/10 ≈ 3.2 kHz: rate 0x0D taking ~1.3s to ramp 0..0xD200
			// matches a divisor of ~10 between sample rate and envelope tick rate.
			constexpr unsigned ENV_TICK_DIVISOR = 10;
			if (++chn.vol_phase >= ENV_TICK_DIVISOR)
			{
				chn.vol_phase = 0;
				uint16_t target16 = uint16_t(chn.vol_target) << 8;
				if (chn.volume < target16)
					chn.volume = uint16_t(std::min<uint32_t>(target16, uint32_t(chn.volume) + chn.vol_rate));
				else if (chn.volume > target16)
					chn.volume = uint16_t(std::max<int32_t>(int32_t(target16), int32_t(chn.volume) - chn.vol_rate));
			}

			s32 smp_data;
			if (chn.play_dir > 0)
				smp_data = sample_interpolate(chn.smpl_cur, chn.smpl_nxt, chn.addr & 0x3FFF);
			else
				smp_data = sample_interpolate(chn.smpl_nxt, chn.smpl_cur, chn.addr & 0x3FFF);
			smp_data = smp_data * chn.volume;
			stream.add_int(0, smpl, smp_data, 32768 << 14); // >>14 results in a good overall volume
			stream.add_int(1, smpl, smp_data, 32768 << 14);

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

int16_t mb87419_mb87420_device::sample_interpolate(int16_t smp1, int16_t smp2, uint16_t frac)
{
	int32_t smpfrac0 = (int32_t)smp1 * (0x4000 - frac);
	int32_t smpfrac1 = (int32_t)smp2 * frac;
	return (int16_t)((smpfrac0 + smpfrac1) >> 14);
}
