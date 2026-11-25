// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_MCASP_H
#define MAME_CPU_TMS320C6X_TMS320C6745_MCASP_H

#pragma once

class tms320c6745_mcasp_device : public device_t
{
public:
	static constexpr unsigned SERIALIZERS = 16;

	// the device clock is the external AHCLKX/AHCLKR input
	tms320c6745_mcasp_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	// EDMA synchronization events
	auto axevt() { return m_axevt_cb.bind(); }
	auto arevt() { return m_arevt_cb.bind(); }

	// serial data, one word per TDM slot: offset is the slot number and
	// the first bit on the wire is bit 31
	template <unsigned N> auto axr_w() { return m_axr_w_cb[N].bind(); }
	template <unsigned N> auto axr_r() { return m_axr_r_cb[N].bind(); }

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
	void set_gblctl(u32 data);
	template <u32 Reg> void stat_w(offs_t offset, u32 data, u32 mem_mask);
	u32 srctl_r(offs_t offset);
	void srctl_w(offs_t offset, u32 data, u32 mem_mask);
	void xbuf_w(offs_t offset, u32 data, u32 mem_mask);
	u32 rbuf_r(offs_t offset);
	u32 data_r();
	void data_w(u32 data);

	u32 serializers(u32 mode) const;
	u32 slot_bits(u32 fmt) const;
	u32 tx_format(u32 data) const;
	u32 rx_format(u32 data) const;
	void transmit_slot(u32 slot);
	void receive_slot(u32 slot);
	void xbuf_serviced(u32 n);
	void rbuf_serviced(u32 n);
	void update_timer();

	devcb_write_line m_axevt_cb;
	devcb_write_line m_arevt_cb;
	devcb_write32::array<SERIALIZERS> m_axr_w_cb;
	devcb_read32::array<SERIALIZERS> m_axr_r_cb;
	emu_timer *m_slot_timer;

	u32 m_reg[0x300 / 4];
	u32 m_xbuf[SERIALIZERS];
	u32 m_xrsr[SERIALIZERS];    // transmit shift registers
	u32 m_rbuf[SERIALIZERS];
	u16 m_xbuf_empty;   // transmit buffers waiting for data
	u16 m_rbuf_full;    // receive buffers waiting to be read
	u16 m_slot;         // transmit slot starting at the next tick
	s16 m_rx_slot;      // slot being received, or -1
	bool m_running;
};

DECLARE_DEVICE_TYPE(TMS320C6745_MCASP, tms320c6745_mcasp_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_MCASP_H
