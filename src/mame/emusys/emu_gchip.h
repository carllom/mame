// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu G-chip (IC402) — polyphony board sound engine

    Custom ASIC on the E-mu polyphony board (AP503). Controls sample memory
    (72-pin SIMMs) and plays 64 voices from it. Sample RAM is not
    CPU-addressable; the host CPU accesses it indirectly through G-chip
    registers for read (+$1C / +$30) and write (+$1E / +$34).

    The E6400 supports 1 or 2 G-chips. Each G-chip handles up to 64 voices
    with a 0x40-byte register stride per voice. SIMM configuration registers
    live at +$43E, +$83E, +$C3E from the chip base.

    Each voice is a sample oscillator with a 6-tap interpolating FIR; its
    output is one stream output per voice. Volume, pan and filtering are done
    downstream by the H-chip.

******************************************************************************/

#ifndef MAME_EMUSYS_EMU_GCHIP_H
#define MAME_EMUSYS_EMU_GCHIP_H

#pragma once

class emu_gchip_device : public device_t, public device_memory_interface, public device_sound_interface
{
public:
	static constexpr feature_type imperfect_features() { return feature::SOUND; }

	static constexpr unsigned NUM_VOICES = 64;

	emu_gchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// Host CPU interface — 16-bit data on D[15:0], word-addressed register space
	u16 read(offs_t offset);
	void write(offs_t offset, u16 data);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_clock_changed() override;

	// device_memory_interface implementation
	virtual space_config_vector memory_space_config() const override;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	static constexpr int NUM_TAPS = 6;          // FIR taps: words n-2 .. n+3 around read position n + f
	static constexpr int NUM_KERNELS = 7;       // interpolation kernels, selected by +$08 bits 26-28
	static constexpr int PHASE_BITS = 8;        // kernel phases per word = 1 << PHASE_BITS
	static constexpr int FRAC_BITS = 12;        // position fraction; the step is rate / 4096
	static constexpr u32 ADDR_MASK = 0x03ffffff; // 26-bit word addresses

	void build_kernels() ATTR_COLD;
	void voice_reg_written(int voice, offs_t offset);
	void start_voice(int voice, u32 start);
	s16 fetch(u32 word_addr) { return s16(m_sample_space.read_word((word_addr & ADDR_MASK) << 1)); }
	u32 reg32(offs_t offset) const { return (u32(m_regs[offset & (NUM_REGS - 2)]) << 16) | m_regs[(offset & (NUM_REGS - 2)) | 1]; }

	// Sample memory address space — 128 MB max (64M × 16-bit words)
	address_space_config m_sample_space_config;
	memory_access<27, 1, 0, ENDIANNESS_BIG>::specific m_sample_space;
	sound_stream *m_stream;

	// Register file — the full 128 KB chip-select window (0x10000 words).
	// Voice registers, config registers, and test locations all live here.
	// Special offsets (+$1C, +$1E, +$30–$36, config regs) have side-effects
	// handled in read()/write(); everything else is plain storage.
	static constexpr int NUM_REGS = 0x10000; // 128 KB / 2
	u16 m_regs[NUM_REGS] = {};

	// Indirect sample memory access registers
	u32 m_read_addr = 0;
	u32 m_write_addr = 0;
	u16 m_read_pipeline = 0; // pipeline latch — first read primes, second read returns data

	// SIMM configuration registers
	u16 m_simm_config_a = 0;  // +$43E
	u16 m_simm_config_b = 0;  // +$83E
	u16 m_simm_type = 0;      // +$C3E

	// Voice registers as last written (decoded from the 32-bit register pairs)
	u32 m_ctrl[NUM_VOICES];        // +$00: bit 11 = running, bit 12 = loop-end buffer in use (reads back the current one)
	u16 m_rate[NUM_VOICES];        // +$04 bits 31-16: step = rate / 4096 source words per output sample
	u8 m_kernel[NUM_VOICES];       // +$08 bits 26-28
	u32 m_loop_end[NUM_VOICES][2]; // +$08 (A) and +$0C (B) bits 25-0, ones'-complemented in the registers
	u32 m_loop_start[NUM_VOICES];  // +$14 bits 25-0
	bool m_next_b[NUM_VOICES];     // +$14 bit 26: loop-end buffer to use after the next wrap

	// Voice oscillator state
	bool m_cur_b[NUM_VOICES];      // loop-end buffer in use (B = +$0C)
	u32 m_lead[NUM_VOICES];        // word address of the newest tap (read position n + 3)
	u16 m_frac[NUM_VOICES];        // fraction f of the read position, FRAC_BITS
	s16 m_taps[NUM_VOICES][NUM_TAPS]; // fetched words, ring buffer; m_tap_pos is the oldest
	u8 m_tap_pos[NUM_VOICES];

	// Interpolation coefficients: [kernel][phase][tap], tap 0 = word n-2
	float m_coef[NUM_KERNELS][1 << PHASE_BITS][NUM_TAPS];
};

DECLARE_DEVICE_TYPE(EMU_GCHIP, emu_gchip_device)

#endif // MAME_EMUSYS_EMU_GCHIP_H
