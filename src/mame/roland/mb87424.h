// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Roland MB87424 TVF (time variant filter)

***************************************************************************/

#ifndef MAME_ROLAND_MB87424_H
#define MAME_ROLAND_MB87424_H

#pragma once

class mb87424_device : public device_t, public device_sound_interface
{
public:
	static constexpr feature_type imperfect_features() { return feature::SOUND; }

	static constexpr unsigned NUM_VOICES = 32;

	mb87424_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// 7-bit word address, 16-bit data
	u16 read(offs_t offset);
	void write(offs_t offset, u16 data, u16 mem_mask = ~0);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	struct voice_t
	{
		u16 damping;    // 04
		u16 mode;       // 05
		u16 cutoff;     // 18
		u16 level;      // 1a: target level
		u16 ramp;       // 0c at the time of the level write

		u32 cur_level;  // 02/03: current level, 15.5 fixed point
		double lp, bp;  // 00/01 and 06/07 (assumed): filter state
	};

	void voice_w(voice_t &voice, offs_t offset, u16 data);

	sound_stream *m_stream;

	u16 m_select;         // 20: voice for the per-voice registers
	u16 m_readback;       // 32: voice whose current level reads back on 02/03
	u32 m_readback_level; // latched by the write to 32
	u32 m_enable;         // 08/09: voice enable mask
	u16 m_ramp;           // 0c: level ramp, 0x16 normally, 0x03 when a voice is cut off
	u16 m_latch_hi;       // high word of a 32-bit per-voice register pair

	voice_t m_voice[NUM_VOICES];
};

DECLARE_DEVICE_TYPE(MB87424, mb87424_device)

#endif // MAME_ROLAND_MB87424_H
