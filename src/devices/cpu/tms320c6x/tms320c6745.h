// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_H
#define MAME_CPU_TMS320C6X_TMS320C6745_H

#pragma once

#include "tms320c674x.h"
#include "tms320c6745_psc.h"
#include "tms320c6745_timer.h"
#include "tms320c6745_spi.h"
#include "tms320c6745_gpio.h"
#include "tms320c6745_i2c.h"
#include "tms320c6745_mcasp.h"
#include "tms320c6745_edma.h"

class tms320c6745_device : public tms320c674x_device
{
public:
	tms320c6745_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

	// system events 4-127 into the interrupt controller
	template <unsigned N> void event_w(int state) { if (state) event(N); }

	// peripheral connections
	template <typename... T> void set_spi_transfer_callback(unsigned n, T &&... args) { m_spi[n].lookup()->set_transfer_callback(std::forward<T>(args)...); }
	auto spi_scs0(unsigned n) { return m_spi[n].lookup()->scs0(); }
	template <unsigned Bank> auto gpio_out() { return m_gpio.lookup()->out<Bank>(); }
	template <unsigned Bank> auto gpio_in() { return m_gpio.lookup()->in<Bank>(); }
	u16 gpio_output(unsigned bank) const { return m_gpio->output(bank); }
	u32 pinmux(unsigned n) const { return m_syscfg[0x120 / 4 + n]; }
	void set_mcasp_ahclk(unsigned n, XTAL const &clock) { m_mcasp[n].lookup()->set_clock(clock); }
	template <typename... T> void set_i2c_start_callback(unsigned n, T &&... args) { m_i2c[n].lookup()->set_start_callback(std::forward<T>(args)...); }
	template <typename... T> void set_i2c_write_callback(unsigned n, T &&... args) { m_i2c[n].lookup()->set_write_callback(std::forward<T>(args)...); }
	template <typename... T> void set_i2c_read_callback(unsigned n, T &&... args) { m_i2c[n].lookup()->set_read_callback(std::forward<T>(args)...); }
	template <typename... T> void set_i2c_stop_callback(unsigned n, T &&... args) { m_i2c[n].lookup()->set_stop_callback(std::forward<T>(args)...); }

protected:
	// device_t implementation
	virtual tiny_rom_entry const *device_rom_region() const override ATTR_COLD;

	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_add_mconfig(machine_config &config) override ATTR_COLD;

private:
	void internal_map(address_map &map) ATTR_COLD;
	void intc_map(address_map &map) ATTR_COLD;

	// interrupt controller
	void event(unsigned n);
	void update_combined();
	void evtset_w(offs_t offset, u32 data);
	void evtclr_w(offs_t offset, u32 data);

	required_device_array<tms320c6745_psc_device, 2> m_psc;
	required_device_array<tms320c6745_timer_device, 2> m_timer;
	required_device_array<tms320c6745_spi_device, 2> m_spi;
	required_device<tms320c6745_gpio_device> m_gpio;
	required_device_array<tms320c6745_i2c_device, 2> m_i2c;
	required_device_array<tms320c6745_mcasp_device, 2> m_mcasp;
	required_device<tms320c6745_edma_device> m_edma;

	u32 m_syscfg[0x1000 / 4];

	u32 m_evtflag[4];
	u32 m_evtmask[4];
	u32 m_intmux[3];
	u8 m_combined;
};

DECLARE_DEVICE_TYPE(TMS320C6745, tms320c6745_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_H
