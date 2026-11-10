// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 Power and Sleep Controller (PSC0, PSC1)

    Reference: SPRUH91D chapter 8.

    Minimal: module state transitions complete as soon as PTCMD is written,
    and nothing is actually gated.

    TODO:
    - power domain control, interrupts and error registers
    - module states after reset (the ROM boot loader leaves some enabled)

***************************************************************************/

#include "emu.h"
#include "tms320c6745_psc.h"


DEFINE_DEVICE_TYPE(TMS320C6745_PSC, tms320c6745_psc_device, "tms320c6745_psc", "TMS320C6745 Power and Sleep Controller")


tms320c6745_psc_device::tms320c6745_psc_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_PSC, tag, owner, clock),
	m_pdctl{},
	m_mdctl{},
	m_state{}
{
}

void tms320c6745_psc_device::device_start()
{
	save_item(NAME(m_pdctl));
	save_item(NAME(m_mdctl));
	save_item(NAME(m_state));
}

void tms320c6745_psc_device::device_reset()
{
	std::fill(std::begin(m_pdctl), std::end(m_pdctl), 0x0000'1001);
	std::fill(std::begin(m_mdctl), std::end(m_mdctl), 0);
	std::fill(std::begin(m_state), std::end(m_state), 0);
}

void tms320c6745_psc_device::map(address_map &map)
{
	map(0x000, 0x003).lr32(NAME([] () -> u32 { return 0x4482'3a00; })); // REVID
	map(0x120, 0x123).w(FUNC(tms320c6745_psc_device::ptcmd_w));
	map(0x128, 0x12b).lr32(NAME([] () -> u32 { return 0; })); // PTSTAT: no transition in progress
	map(0x200, 0x207).lr32(NAME([] () -> u32 { return 0x0000'0301; })); // PDSTAT: power domain on
	map(0x300, 0x307).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_pdctl[offset]; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_pdctl[offset]); }));
	map(0x800, 0x87f).r(FUNC(tms320c6745_psc_device::mdstat_r));
	map(0xa00, 0xa7f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_mdctl[offset]; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_mdctl[offset]); }));
}

u32 tms320c6745_psc_device::mdstat_r(offs_t offset)
{
	// STATE, local reset de-asserted and done, module reset and clock follow the state
	u8 const state = m_state[offset];
	u32 value = state | (1U << 11) | (1U << 9) | (1U << 8);
	if (state >= 2)
		value |= 1U << 10; // MRST de-asserted in Disable and Enable
	if (state == 3)
		value |= 1U << 12; // MCKOUT
	return value;
}

void tms320c6745_psc_device::ptcmd_w(u32 data)
{
	// GO for either power domain moves every module to its NEXT state
	if (data & 3)
	{
		for (unsigned n = 0; n < MODULES; n++)
			m_state[n] = m_mdctl[n] & 7;
	}
}
