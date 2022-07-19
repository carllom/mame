// license:BSD-3-Clause
// copyright-holders:AJR
/***************************************************************************

    Roland RF5C36 (15229840) & SA-16 (15229874) Sampler Custom ICs

***************************************************************************/

#ifndef MAME_MACHINE_SA16_H
#define MAME_MACHINE_SA16_H

#pragma once

#include "emu.h"

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> sa16_base_device

class sa16_base_device : public device_t, public device_memory_interface
{
public:
	// callback configuration
	auto int_callback() { return m_int_callback.bind(); }
	auto sh_callback() { return m_sh_callback.bind(); }

	// CPU read/write handlers
	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

protected:
	// base type constructor
	sa16_base_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock);

	// device_config_memory_interface overrides
	virtual space_config_vector memory_space_config() const override;
 
	// address space configurations
	const address_space_config m_space_config;

	// device-specific overrides
	virtual void device_resolve_objects() override;
	virtual void device_start() override;
	virtual void device_reset() override;

private:

	void sa16(address_map &map);

	// line callbacks
	devcb_write_line m_int_callback;
	devcb_write_line m_sh_callback;

	u16 wram_r16(offs_t offset) { return this->space().read_word(offset); }
	void wram_w16(offs_t offset, u16 data) { this->space().write_word(offset, data); }
	u8 wram_r8(offs_t offset) { return this->space().read_byte(offset); }
	void wram_w8(offs_t offset, u8 data) { this->space().write_byte(offset, data); }

	// internal state (TODO)
	u16 m_active_channels;

	int m_reg800_woffset; // offset for reg writes???
	int m_reg800_roffset; // offset for reg reads???
	int m_blockoffset; // Block offset for WaveRAM port
	int m_smpcounter; // Sample port counter
	u16 m_port_wram; // WaveRAM port value (
	u16 m_port_smp16; // Sample port value (16-bit)

	u8 m_regs[9]; // Regs @ offset 0
	u8 m_ports[8]; // Ports @ offset 400h
	u16 m_regs800[2048];

};

enum SA16_REGS {
	SA16REG_0 = 0,
	SA16REG_1,
	SA16REG_2,
	SA16REG_3,
	SA16REG_4,
	SA16REG_5,
	SA16REG_6,
	SA16REG_7,
	SA16REG_BLK
};

enum SA16_PORTS {
	SA16_SMP_LOW = 4,
	SA16_SMP_HIGH = 5
};

// ======================> rf5c36_device

class rf5c36_device : public sa16_base_device
{
public:
	// device type constructor
	rf5c36_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
};

// ======================> sa16_device

class sa16_device : public sa16_base_device
{
public:
	// device type constructor
	sa16_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock);
};

// device type declarations
DECLARE_DEVICE_TYPE(RF5C36, rf5c36_device)
DECLARE_DEVICE_TYPE(SA16, sa16_device)

#endif // MAME_MACHINE_SA16_H
