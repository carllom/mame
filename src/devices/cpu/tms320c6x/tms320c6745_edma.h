// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_EDMA_H
#define MAME_CPU_TMS320C6X_TMS320C6745_EDMA_H

#pragma once

class tms320c6745_edma_device : public device_t
{
public:
	tms320c6745_edma_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	template <typename T> void set_space(T &&tag, int spacenum) { m_space.set_tag(std::forward<T>(tag), spacenum); }

	// shadow region completion interrupts (EDMA3_CC0_INTn), and the error interrupt
	template <unsigned N> auto region_irq() { return m_region_irq_cb[N].bind(); }
	auto error_irq() { return m_error_irq_cb.bind(); }

	// synchronization event from a peripheral
	void event(unsigned channel);

	void map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	static constexpr unsigned CHANNELS = 32;
	static constexpr unsigned QCHANNELS = 8;
	static constexpr unsigned PARAM_SETS = 128;

	void trigger(unsigned set, bool qdma, unsigned channel);
	void completion(unsigned tcc, bool interrupt, bool chain);
	void update_interrupts(bool force);
	void process_events();

	u32 channel_r(offs_t offset, u32 mask);
	void channel_w(offs_t offset, u32 data, u32 mask);
	u32 global_r(offs_t offset);
	void global_w(offs_t offset, u32 data);
	u32 region_r(offs_t offset);
	void region_w(offs_t offset, u32 data);
	u32 param_r(offs_t offset);
	void param_w(offs_t offset, u32 data, u32 mem_mask);

	required_address_space m_space;
	devcb_write_line::array<4> m_region_irq_cb;
	devcb_write_line m_error_irq_cb;

	u32 m_er;
	u32 m_eer;
	u32 m_cer;
	u32 m_ser;
	u32 m_ier;
	u32 m_ipr;
	u32 m_emr;
	u32 m_qer;
	u32 m_qeer;
	u32 m_qemr;
	u32 m_ccerr;
	u32 m_drae[4];
	u32 m_qrae[4];
	u32 m_qchmap[QCHANNELS];
	u32 m_dmaqnum[4];
	u32 m_qdmaqnum;
	u32 m_quepri;
	u8 m_irq_state;
	u32 m_param[PARAM_SETS * 8];
	bool m_busy;
};

DECLARE_DEVICE_TYPE(TMS320C6745_EDMA, tms320c6745_edma_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_EDMA_H
