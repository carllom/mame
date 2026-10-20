// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu e6400 effects card slot (CSDSP, 0x520000-0x53FFFF)

******************************************************************************/

#ifndef MAME_EMUSYS_E6400_FX_H
#define MAME_EMUSYS_E6400_FX_H

#pragma once

class device_e6400_fx_interface;

class e6400_fx_slot_device : public device_t, public device_single_card_slot_interface<device_e6400_fx_interface>
{
public:
	template <typename T>
	e6400_fx_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, T &&opts, const char *dflt, bool fixed = false)
		: e6400_fx_slot_device(mconfig, tag, owner, 0)
	{
		set_options(std::forward<T>(opts), dflt, fixed);
	}
	e6400_fx_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	// CSDSP window; reads 0 with no card, as on a unit without one
	u16 read(offs_t offset, u16 mem_mask = ~0);
	void write(offs_t offset, u16 data, u16 mem_mask = ~0);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;

private:
	device_e6400_fx_interface *m_card;
};

class device_e6400_fx_interface : public device_interface
{
public:
	virtual u16 read(offs_t offset, u16 mem_mask) = 0;
	virtual void write(offs_t offset, u16 data, u16 mem_mask) = 0;

protected:
	device_e6400_fx_interface(const machine_config &mconfig, device_t &device);
};

DECLARE_DEVICE_TYPE(E6400_FX_SLOT, e6400_fx_slot_device)
DECLARE_DEVICE_TYPE(E6400_FX_G05, device_e6400_fx_interface)

void e6400_fx_cards(device_slot_interface &device) ATTR_COLD;

#endif // MAME_EMUSYS_E6400_FX_H
