// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu G-chip (IC402) — polyphony board sound engine

    Custom ASIC on the E-mu polyphony board (AP503). Controls sample memory
    (72-pin SIMMs) and provides voice playback registers. Sample RAM is not
    CPU-addressable; the host CPU accesses it indirectly through G-chip
    registers for read (+$1C / +$30) and write (+$1E / +$34).

    The E6400 supports 1 or 2 G-chips. Each G-chip handles up to 64 voices
    with a 0x40-byte register stride per voice. SIMM configuration registers
    live at +$43E, +$83E, +$C3E from the chip base.

    This implementation provides the register interface needed for the firmware
    SIMM detection (reports "128mb of Sound Memory Installed") and sample
    load/store operations. Sound generation is not yet implemented.

******************************************************************************/

#ifndef MAME_EMUSYS_EMU_GCHIP_H
#define MAME_EMUSYS_EMU_GCHIP_H

#pragma once

class emu_gchip_device : public device_t, public device_memory_interface
{
public:
	emu_gchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// Host CPU interface — 16-bit data on D[15:0], word-addressed register space
	u16 read(offs_t offset);
	void write(offs_t offset, u16 data);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual space_config_vector memory_space_config() const override;

private:
	// Sample memory address space — 128 MB max (64M × 16-bit words)
	address_space_config m_sample_space_config;
	memory_access<27, 1, 0, ENDIANNESS_BIG>::specific m_sample_space;

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
};

DECLARE_DEVICE_TYPE(EMU_GCHIP, emu_gchip_device)

#endif // MAME_EMUSYS_EMU_GCHIP_H
