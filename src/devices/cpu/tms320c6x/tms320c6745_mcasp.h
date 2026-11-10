// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_MCASP_H
#define MAME_CPU_TMS320C6X_TMS320C6745_MCASP_H

#pragma once

class tms320c6745_mcasp_device : public device_t
{
public:
	// the device clock is the external AHCLKX/AHCLKR input
	tms320c6745_mcasp_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	// EDMA synchronization events
	auto axevt() { return m_axevt_cb.bind(); }
	auto arevt() { return m_arevt_cb.bind(); }

	void map(address_map &map) ATTR_COLD;
	void data_map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	TIMER_CALLBACK_MEMBER(slot_tick);

	void gblctl_w(offs_t offset, u32 data, u32 mem_mask);
	void rgblctl_w(u32 data);
	void xgblctl_w(u32 data);
	template <u32 Reg> void stat_w(offs_t offset, u32 data, u32 mem_mask);
	void update_timer();
	u32 data_r();
	void data_w(u32 data);

	devcb_write_line m_axevt_cb;
	devcb_write_line m_arevt_cb;
	emu_timer *m_slot_timer;

	u32 m_reg[0x300 / 4];
	u16 m_slot;
	bool m_running;
};

DECLARE_DEVICE_TYPE(TMS320C6745_MCASP, tms320c6745_mcasp_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_MCASP_H
