// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_I2C_H
#define MAME_CPU_TMS320C6X_TMS320C6745_I2C_H

#pragma once

class tms320c6745_i2c_device : public device_t
{
public:
	// byte-level bus: start (7-bit address, read) and write return the acknowledge
	using start_delegate = device_delegate<bool (u8 address, bool read)>;
	using write_delegate = device_delegate<bool (u8 data)>;
	using read_delegate = device_delegate<u8 ()>;
	using stop_delegate = device_delegate<void ()>;

	tms320c6745_i2c_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	template <typename... T> void set_start_callback(T &&... args) { m_start_cb.set(std::forward<T>(args)...); }
	template <typename... T> void set_write_callback(T &&... args) { m_write_cb.set(std::forward<T>(args)...); }
	template <typename... T> void set_read_callback(T &&... args) { m_read_cb.set(std::forward<T>(args)...); }
	template <typename... T> void set_stop_callback(T &&... args) { m_stop_cb.set(std::forward<T>(args)...); }
	auto irq() { return m_irq_cb.bind(); }

	void map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_resolve_objects() override ATTR_COLD;
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	void mdr_w(u32 data);
	void dxr_w(u32 data);
	u32 drr_r();
	u32 ivr_r();
	void transmit();
	void receive();
	void stop();
	void update_irq();

	start_delegate m_start_cb;
	write_delegate m_write_cb;
	read_delegate m_read_cb;
	stop_delegate m_stop_cb;
	devcb_write_line m_irq_cb;

	u32 m_reg[16];
	bool m_dxr_full;
	int m_irq;
};

DECLARE_DEVICE_TYPE(TMS320C6745_I2C, tms320c6745_i2c_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_I2C_H
