// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_TIMER_H
#define MAME_CPU_TMS320C6X_TMS320C6745_TIMER_H

#pragma once

class tms320c6745_timer_device : public device_t
{
public:
	tms320c6745_timer_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock);

	auto tint12() { return m_tint12_cb.bind(); }
	auto tint34() { return m_tint34_cb.bind(); }

	void map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	TIMER_CALLBACK_MEMBER(period_expired);

	bool chained() const { return BIT(m_tgcr, 2, 2) != 1; }
	u32 enamode(u32 n) const { return BIT(m_tcr, n ? 22 : 6, 2); }
	u64 period(u32 n) const;
	u32 prescale(u32 n) const;
	u64 count(u32 n) const;
	void update(u32 n);

	u32 tim_r(offs_t offset);
	void tim_w(offs_t offset, u32 data, u32 mem_mask);
	void prd_w(offs_t offset, u32 data, u32 mem_mask);
	void tcr_w(offs_t offset, u32 data, u32 mem_mask);
	void tgcr_w(offs_t offset, u32 data, u32 mem_mask);

	devcb_write_line m_tint12_cb;
	devcb_write_line m_tint34_cb;

	// per 32-bit half; half 0 is the whole timer in 64-bit mode
	emu_timer *m_timer[2];
	attotime m_start[2];    // when the counter was last loaded
	u64 m_base[2];          // counter value at start
	bool m_running[2];
	u32 m_prd[2];
	u32 m_rel[2];
	u32 m_tim[2];
	u32 m_tcr;
	u32 m_tgcr;
	u32 m_emumgt;
	u32 m_intctlstat;
};

DECLARE_DEVICE_TYPE(TMS320C6745_TIMER, tms320c6745_timer_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_TIMER_H
