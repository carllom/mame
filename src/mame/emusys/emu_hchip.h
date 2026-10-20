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
	static constexpr unsigned CASCADE_INPUT = NUM_CHANNELS;  // left, then right: the previous chip's mix (CSCDI)

	// The mix saturates at this level, in units of one full-scale voice at volume 255
	static constexpr double MIX_FULL_SCALE = 3.55;

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
	virtual void device_post_load() override;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr int NUM_REGS = 0x1000;  // 0x2000 bytes / 2
	static constexpr int GAIN_BITS = 12;     // gain table index = level >> (16 - GAIN_BITS)
	static constexpr int SECTIONS = 3;

	// Filter section coefficients, decoded from the registers (recomputed after a state load):
	// (n0 + n1 z^-1 + n2 z^-2) / (1 + b1 z^-1 + b2 z^-2)
	struct section_coefs
	{
		bool active = false;
		double n0 = 1.0, n1 = 0.0, n2 = 0.0;
		double b1 = 0.0, b2 = 0.0;
	};

	static constexpr offs_t window(int ch) { return ((ch & 0x10) << 7) | ((ch & 0x0f) << 6); } // word offset
	static constexpr int prev_channel(int ch) { return (ch & 0x10) | ((ch - 1) & 0x0f); }
	static constexpr int next_channel(int ch) { return (ch & 0x10) | ((ch + 1) & 0x0f); }
	u32 reg32(int ch, offs_t byte) const { return (u32(m_regs[window(ch) | (byte >> 1)]) << 16) | m_regs[window(ch) | (byte >> 1) | 1]; }
	u8 reg8(int ch, offs_t byte) const { return BIT(byte, 0) ? (m_regs[window(ch) | (byte >> 1)] & 0xff) : (m_regs[window(ch) | (byte >> 1)] >> 8); }

	void build_gain_table() ATTR_COLD;
	void update_filter(int ch);

	sound_stream *m_stream;

	// Register file as last written
	u16 m_regs[NUM_REGS];

	// Volume, per channel and side (0 = left, 1 = right), in 0.375 dB steps
	u16 m_level[NUM_CHANNELS][2];  // current level, 8.8 fixed point; reads back at +$34 / +$36
	u8 m_target[NUM_CHANNELS][2];  // +$71 / +$73
	u16 m_fall[NUM_CHANNELS][2];   // fall rate towards the target, 8.8 steps per sample, from the +$46 latch

	section_coefs m_section[NUM_CHANNELS][SECTIONS];
	float m_filter_gain[NUM_CHANNELS];
	double m_state[NUM_CHANNELS][SECTIONS * 2];  // transposed direct form II, two per section

	float m_gain[1 << GAIN_BITS];
};

DECLARE_DEVICE_TYPE(EMU_HCHIP, emu_hchip_device)

#endif // MAME_EMUSYS_EMU_HCHIP_H
