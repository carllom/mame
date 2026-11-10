// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 multichannel audio serial port (McASP0, McASP1)

    Reference: SPRUH91D chapter 22.

    Models the transmit and receive frame timing: once a section is out of
    reset, every active slot raises its EDMA event. Received data is
    silence and transmitted data is dropped. The GBLCTL handshake (bits
    set through RGBLCTL and XGBLCTL show up in GBLCTL at once) and the
    write 1 to clear status registers are there; everything else is
    register storage.

    TODO:
    - audio input and output
    - separate receive timing, external clocks and frame syncs
    - AFIFO, interrupts, error status, DIT mode

***************************************************************************/

#include "emu.h"
#include "tms320c6745_mcasp.h"

#include <algorithm>


DEFINE_DEVICE_TYPE(TMS320C6745_MCASP, tms320c6745_mcasp_device, "tms320c6745_mcasp", "TMS320C6745 McASP")


namespace {

enum : u32
{
	GBLCTL = 0x44 / 4,
	RTDM = 0x78 / 4,
	RSTAT = 0x80 / 4,
	XFMT = 0xa8 / 4,
	AFSXCTL = 0xac / 4,
	ACLKXCTL = 0xb0 / 4,
	AHCLKXCTL = 0xb4 / 4,
	XTDM = 0xb8 / 4,
	XSTAT = 0xc0 / 4
};

} // anonymous namespace


tms320c6745_mcasp_device::tms320c6745_mcasp_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_MCASP, tag, owner, clock),
	m_axevt_cb(*this),
	m_arevt_cb(*this),
	m_slot_timer(nullptr),
	m_reg{},
	m_slot(0),
	m_running(false)
{
}

void tms320c6745_mcasp_device::device_start()
{
	m_slot_timer = timer_alloc(FUNC(tms320c6745_mcasp_device::slot_tick), this);

	save_item(NAME(m_reg));
	save_item(NAME(m_slot));
	save_item(NAME(m_running));
}

void tms320c6745_mcasp_device::device_reset()
{
	std::fill(std::begin(m_reg), std::end(m_reg), 0);
	m_slot = 0;
	m_running = false;
	m_slot_timer->adjust(attotime::never);
}

void tms320c6745_mcasp_device::map(address_map &map)
{
	// everything not listed after this is register storage
	map(0x000, 0x2ff).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_reg[offset]; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_reg[offset]); }));
	map(0x000, 0x003).lr32(NAME([] () -> u32 { return 0x4430'0a02; })); // PID
	map(0x044, 0x047).lr32(NAME([this] () -> u32 { return m_reg[GBLCTL]; })).w(FUNC(tms320c6745_mcasp_device::gblctl_w));
	map(0x060, 0x063).lr32(NAME([this] () -> u32 { return m_reg[GBLCTL]; })).w(FUNC(tms320c6745_mcasp_device::rgblctl_w));
	map(0x080, 0x083).lr32(NAME([this] () -> u32 { return m_reg[RSTAT]; })).w(FUNC(tms320c6745_mcasp_device::stat_w<RSTAT>));
	map(0x0a0, 0x0a3).lr32(NAME([this] () -> u32 { return m_reg[GBLCTL]; })).w(FUNC(tms320c6745_mcasp_device::xgblctl_w));
	map(0x0c0, 0x0c3).lr32(NAME([this] () -> u32 { return m_reg[XSTAT]; })).w(FUNC(tms320c6745_mcasp_device::stat_w<XSTAT>));
}

void tms320c6745_mcasp_device::gblctl_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_reg[GBLCTL]);
	update_timer();
}

void tms320c6745_mcasp_device::rgblctl_w(u32 data)
{
	// receiver half of GBLCTL
	m_reg[GBLCTL] = (m_reg[GBLCTL] & ~0x0000'001f) | (data & 0x0000'001f);
	update_timer();
}

void tms320c6745_mcasp_device::xgblctl_w(u32 data)
{
	// transmitter half of GBLCTL
	m_reg[GBLCTL] = (m_reg[GBLCTL] & ~0x0000'1f00) | (data & 0x0000'1f00);
	update_timer();
}

template <u32 Reg>
void tms320c6745_mcasp_device::stat_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_reg[Reg] &= ~(data & mem_mask & 0x01ff); // write 1 to clear
}

void tms320c6745_mcasp_device::data_map(address_map &map)
{
	map(0x000, 0xfff).rw(FUNC(tms320c6745_mcasp_device::data_r), FUNC(tms320c6745_mcasp_device::data_w));
}

u32 tms320c6745_mcasp_device::data_r()
{
	// TODO: audio input
	return 0;
}

void tms320c6745_mcasp_device::data_w(u32 data)
{
	// TODO: audio output
}

void tms320c6745_mcasp_device::update_timer()
{
	// transmit or receive state machine and frame sync out of reset
	bool const tx = BIT(m_reg[GBLCTL], 11) && BIT(m_reg[GBLCTL], 12);
	bool const rx = BIT(m_reg[GBLCTL], 3) && BIT(m_reg[GBLCTL], 4);
	bool const run = tx || rx;
	if (run == m_running)
		return;
	m_running = run;
	if (!run)
	{
		m_slot_timer->adjust(attotime::never);
		return;
	}

	// slot time from the transmit clock: AHCLKX divided down to ACLKX
	u32 ahclk = clock();
	if (BIT(m_reg[AHCLKXCTL], 15))
		ahclk = clock() / (BIT(m_reg[AHCLKXCTL], 0, 12) + 1); // TODO: internal AUXCLK source
	u32 aclk = ahclk;
	if (BIT(m_reg[ACLKXCTL], 5))
		aclk = ahclk / (BIT(m_reg[ACLKXCTL], 0, 5) + 1);
	u32 const ssz = BIT(m_reg[XFMT], 4, 4);
	u32 const slot_bits = (ssz >= 3) ? (ssz + 1) * 2 : 32;
	if (!aclk)
	{
		logerror("no transmit clock\n");
		return;
	}
	m_slot = 0;
	attotime const period = attotime::from_ticks(slot_bits, aclk);
	m_slot_timer->adjust(period, 0, period);
}

TIMER_CALLBACK_MEMBER(tms320c6745_mcasp_device::slot_tick)
{
	u32 slots = BIT(m_reg[AFSXCTL], 7, 9);
	if (slots < 2)
		slots = 1;
	u32 const slot = m_slot;
	m_slot = (m_slot + 1) % std::min(slots, 32U);

	if (BIT(m_reg[GBLCTL], 11) && BIT(m_reg[GBLCTL], 12) && BIT(m_reg[XTDM], slot))
	{
		m_reg[XSTAT] |= 1U << 5; // XDATA
		m_axevt_cb(1);
		m_axevt_cb(0);
	}
	if (BIT(m_reg[GBLCTL], 3) && BIT(m_reg[GBLCTL], 4) && BIT(m_reg[RTDM], slot))
	{
		m_reg[RSTAT] |= 1U << 5; // RDATA
		m_arevt_cb(1);
		m_arevt_cb(0);
	}
}
