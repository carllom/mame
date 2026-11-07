// license:GPL-2.0+
// copyright-holders:Carl Lom
#ifndef MAME_PHILIPS_PM3585_KBD_H
#define MAME_PHILIPS_PM3585_KBD_H

#pragma once

#include "cpu/mcs48/mcs48.h"


class pm3585_kbd_device : public device_t
{
public:
	pm3585_kbd_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// open-drain I2C outputs (1 = released)
	auto scl_callback() { return m_scl_cb.bind(); }
	auto sda_callback() { return m_sda_cb.bind(); }

	// I2C bus levels
	void scl_w(int state);
	void sda_w(int state);

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;
	virtual const tiny_rom_entry *device_rom_region() const override ATTR_COLD;
	virtual ioport_constructor device_input_ports() const override ATTR_COLD;

private:
	required_device<mab8401_device> m_mcu;
	required_ioport_array<8> m_lines;
	required_ioport m_dial;

	devcb_write_line m_scl_cb;
	devcb_write_line m_sda_cb;

	emu_timer *m_dial_timer;

	uint8_t m_p0;
	uint8_t m_p1;
	uint8_t m_dial_pos;
	uint8_t m_dial_phase;
	bool m_dial_up;

	void program_map(address_map &map) ATTR_COLD;

	uint8_t p0_r();
	uint8_t p1_r();
	uint8_t p2_r();

	TIMER_CALLBACK_MEMBER(dial_tick);
};

DECLARE_DEVICE_TYPE(PM3585_KBD, pm3585_kbd_device)

#endif // MAME_PHILIPS_PM3585_KBD_H
