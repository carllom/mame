// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_SPI_H
#define MAME_CPU_TMS320C6X_TMS320C6745_SPI_H

#pragma once

class tms320c6745_spi_device : public device_t
{
public:
	// shifts out a word of the given length and returns the received word
	using transfer_delegate = device_delegate<u32 (u32 data, u32 bits)>;

	tms320c6745_spi_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	template <typename... T> void set_transfer_callback(T &&... args) { m_transfer_cb.set(std::forward<T>(args)...); }
	auto scs0() { return m_scs0_cb.bind(); }
	auto irq() { return m_irq_cb.bind(); }

	void map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_resolve_objects() override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void set_scs0(int state);
	void gcr1_w(u32 data);
	void update_gpio();
	void transfer(u16 data);
	void update_irq();

	u32 buf_r();
	void dat0_w(offs_t offset, u32 data, u32 mem_mask);
	void dat1_w(offs_t offset, u32 data, u32 mem_mask);

	transfer_delegate m_transfer_cb;
	devcb_write_line m_scs0_cb;
	devcb_write_line m_irq_cb;

	u32 m_gcr0;
	u32 m_gcr1;
	u32 m_int0;
	u32 m_lvl;
	u32 m_flg;
	u32 m_pc[4]; // SPIPC0-SPIPC3
	u32 m_dat1;
	u32 m_buf;
	u32 m_delay;
	u32 m_def;
	u32 m_fmt[4];
	u8 m_scs0;
	u8 m_irq;
};

DECLARE_DEVICE_TYPE(TMS320C6745_SPI, tms320c6745_spi_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_SPI_H
