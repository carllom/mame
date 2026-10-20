// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu H-chip (IC413) — polyphony board voice filter and amplifier

    Takes 32 G-chip voices, applies each one's filter and left/right volume,
    and mixes them to a stereo output. Each G-chip feeds two H-chips: one
    for its even voices and one for its odd voices.

******************************************************************************/

#ifndef MAME_EMUSYS_EMU_HCHIP_H
#define MAME_EMUSYS_EMU_HCHIP_H

#pragma once

class emu_hchip_device : public device_t, public device_sound_interface
{
public:
	static constexpr feature_type imperfect_features() { return feature::SOUND; }

	static constexpr unsigned NUM_CHANNELS = 32;

	emu_hchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// The channel whose register window is at the given byte offset; it is also its sound input
	static constexpr int channel(offs_t byte_offset) { return ((byte_offset >> 8) & 0x10) | ((byte_offset >> 7) & 0x0f); }

	// Host CPU interface — 16-bit, 0x2000-byte window
	u16 read(offs_t offset, u16 mem_mask = ~0);
	void write(offs_t offset, u16 data, u16 mem_mask = ~0);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_clock_changed() override;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr int NUM_REGS = 0x1000;  // 0x2000 bytes / 2
	static constexpr int GAIN_BITS = 12;     // gain table index = level >> (16 - GAIN_BITS)

	void build_gain_table() ATTR_COLD;

	sound_stream *m_stream;

	// Register file as last written
	u16 m_regs[NUM_REGS];

	// Volume, per channel and side (0 = left, 1 = right), in 0.375 dB steps
	u16 m_level[NUM_CHANNELS][2];  // current level, 8.8 fixed point; reads back at +$34 / +$36
	u8 m_target[NUM_CHANNELS][2];  // +$71 / +$73
	u16 m_fall[NUM_CHANNELS][2];   // fall rate towards the target, 8.8 steps per sample, from the +$46 latch

	float m_gain[1 << GAIN_BITS];
};

DECLARE_DEVICE_TYPE(EMU_HCHIP, emu_hchip_device)

#endif // MAME_EMUSYS_EMU_HCHIP_H
