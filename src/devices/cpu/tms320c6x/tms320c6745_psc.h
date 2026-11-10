// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6745_PSC_H
#define MAME_CPU_TMS320C6X_TMS320C6745_PSC_H

#pragma once

class tms320c6745_psc_device : public device_t
{
public:
	tms320c6745_psc_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	void map(address_map &map) ATTR_COLD;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	static constexpr unsigned MODULES = 32;

	u32 mdstat_r(offs_t offset);
	void ptcmd_w(u32 data);

	u32 m_pdctl[2];
	u32 m_mdctl[MODULES];
	u8 m_state[MODULES];
};

DECLARE_DEVICE_TYPE(TMS320C6745_PSC, tms320c6745_psc_device)

#endif // MAME_CPU_TMS320C6X_TMS320C6745_PSC_H
