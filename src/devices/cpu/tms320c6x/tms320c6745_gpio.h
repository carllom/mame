// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_GPIO_H
#define MAME_CPU_TMS320C6X_TMS320C6745_GPIO_H

#pragma once

class tms320c6745_gpio_device : public device_t
{
public:
	static constexpr unsigned BANKS = 8;

	tms320c6745_gpio_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	// output levels of a bank (pins configured as inputs read as 1)
	template <unsigned Bank> auto out() { return m_out_cb[Bank].bind(); }
	// bank interrupt, pulsed on each enabled edge
	template <unsigned Bank> auto bank_irq() { return m_irq_cb[Bank].bind(); }

	// levels driven onto a bank's pins from outside
	void in_w(unsigned bank, u16 data);

	u16 output(unsigned bank) const { return m_out[bank] | m_dir[bank]; }

	void map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	u32 pair_r(offs_t offset);
	void pair_w(offs_t offset, u32 data, u32 mem_mask);
	u16 pins(unsigned bank) const { return (m_in[bank] & m_dir[bank]) | (m_out[bank] & ~m_dir[bank]); }
	void update(unsigned bank, u16 old_out, u16 old_pins);

	devcb_write16::array<BANKS> m_out_cb;
	devcb_write_line::array<BANKS> m_irq_cb;

	u32 m_binten;
	u16 m_in[BANKS];
	u16 m_dir[BANKS];
	u16 m_out[BANKS];
	u16 m_ris[BANKS];
	u16 m_fal[BANKS];
	u16 m_intstat[BANKS];
};

DECLARE_DEVICE_TYPE(TMS320C6745_GPIO, tms320c6745_gpio_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_GPIO_H
