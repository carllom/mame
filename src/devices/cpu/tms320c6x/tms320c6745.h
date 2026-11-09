// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_H
#define MAME_CPU_TMS320C6X_TMS320C6745_H

#pragma once

#include "tms320c674x.h"

class tms320c6745_device : public tms320c674x_device
{
public:
	tms320c6745_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

protected:
	// device_t implementation
	virtual tiny_rom_entry const *device_rom_region() const override ATTR_COLD;

private:
	void internal_map(address_map &map) ATTR_COLD;
};

DECLARE_DEVICE_TYPE(TMS320C6745, tms320c6745_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_H
