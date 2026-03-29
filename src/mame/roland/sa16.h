// license:BSD-3-Clause
// copyright-holders:AJR
/***************************************************************************

    Roland RF5C36 (15229840) & SA-16 (15229874) Sampler Custom ICs

***************************************************************************/

#ifndef MAME_ROLAND_SA16_H
#define MAME_ROLAND_SA16_H

#pragma once

//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> sa16_base_device

class sa16_base_device : public device_t
{
public:
	// address space indices
	enum {
		AS_WRAM     = 0,
		AS_REGS     = 1,
		AS_CHANREGS = 2
	};

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
	const address_space_config m_space_regs_config;
	const address_space_config m_space_chanregs_config;

	// device-specific overrides
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:

	void sa16(address_map &map);
	void regs_map(address_map &map);
	void chanregs_map(address_map &map);

	// line callbacks
	devcb_write_line m_int_callback;
	devcb_write_line m_sh_callback;

	u16 wram_r16(offs_t offset) { return this->space(AS_WRAM).read_word(offset); }
	void wram_w16(offs_t offset, u16 data) { this->space(AS_WRAM).write_word(offset, data); }
	u8 wram_r8(offs_t offset) { return this->space(AS_WRAM).read_byte(offset); }
	void wram_w8(offs_t offset, u8 data) { this->space(AS_WRAM).write_byte(offset, data); }

	// internal state (TODO)
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

#endif // MAME_ROLAND_SA16_H
