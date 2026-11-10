// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 64-bit timer (Timer64P0, Timer64P1)

    Reference: SPRUH91D chapter 26.

    Supports the 64-bit and dual 32-bit unchained general-purpose modes
    with one-shot, continuous and continuous-reload operation, counting
    the internal clock.

    TODO:
    - dual 32-bit chained mode, watchdog mode
    - external clock and capture, compare registers and interrupts
    - emulation suspend, GPIO mode

***************************************************************************/

#include "emu.h"
#include "tms320c6745_timer.h"


DEFINE_DEVICE_TYPE(TMS320C6745_TIMER, tms320c6745_timer_device, "tms320c6745_timer", "TMS320C6745 64-bit timer")


tms320c6745_timer_device::tms320c6745_timer_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_TIMER, tag, owner, clock),
	m_tint12_cb(*this),
	m_tint34_cb(*this),
	m_timer{},
	m_base{},
	m_running{},
	m_prd{},
	m_rel{},
	m_tim{},
	m_tcr(0),
	m_tgcr(0),
	m_emumgt(0),
	m_intctlstat(0)
{
}

void tms320c6745_timer_device::device_start()
{
	for (auto &t : m_timer)
		t = timer_alloc(FUNC(tms320c6745_timer_device::period_expired), this);
	save_item(NAME(m_start));
	save_item(NAME(m_base));
	save_item(NAME(m_running));
	save_item(NAME(m_prd));
	save_item(NAME(m_rel));
	save_item(NAME(m_tim));
	save_item(NAME(m_tcr));
	save_item(NAME(m_tgcr));
	save_item(NAME(m_emumgt));
	save_item(NAME(m_intctlstat));
}

void tms320c6745_timer_device::device_reset()
{
	for (unsigned n = 0; n < 2; n++)
	{
		m_timer[n]->adjust(attotime::never);
		m_running[n] = false;
		m_base[n] = 0;
	}
	std::fill(std::begin(m_prd), std::end(m_prd), 0);
	std::fill(std::begin(m_rel), std::end(m_rel), 0);
	std::fill(std::begin(m_tim), std::end(m_tim), 0);
	m_tcr = 0;
	m_tgcr = 0;
	m_emumgt = 0;
	m_intctlstat = 0x0001'0001;
}

void tms320c6745_timer_device::map(address_map &map)
{
	map(0x00, 0x03).lr32(NAME([] () -> u32 { return 0x0044'7311; })); // PID12
	map(0x04, 0x07).lrw32(NAME([this] () -> u32 { return m_emumgt; }), NAME([this] (u32 data) { m_emumgt = data; }));
	map(0x10, 0x17).rw(FUNC(tms320c6745_timer_device::tim_r), FUNC(tms320c6745_timer_device::tim_w));
	map(0x18, 0x1f).lr32(NAME([this] (offs_t offset) -> u32 { return m_prd[offset]; })).w(FUNC(tms320c6745_timer_device::prd_w));
	map(0x20, 0x23).lr32(NAME([this] () -> u32 { return m_tcr; })).w(FUNC(tms320c6745_timer_device::tcr_w));
	map(0x24, 0x27).lr32(NAME([this] () -> u32 { return m_tgcr; })).w(FUNC(tms320c6745_timer_device::tgcr_w));
	map(0x34, 0x3b).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_rel[offset]; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_rel[offset]); }));
	map(0x44, 0x47).lrw32(
			NAME([this] () -> u32 { return m_intctlstat; }),
			NAME([this] (u32 data)
			{
				// enables are read/write, status bits are write 1 to clear
				m_intctlstat = (m_intctlstat & ~(data & 0x000a'000a)) & ~0x0005'0005;
				m_intctlstat |= data & 0x0005'0005;
			}));
}

u64 tms320c6745_timer_device::period(unsigned n) const
{
	if (chained())
		return (u64(m_prd[1]) << 32) | m_prd[0];
	return m_prd[n];
}

u32 tms320c6745_timer_device::prescale(unsigned n) const
{
	// only the upper half has a prescaler in dual 32-bit unchained mode
	return (!chained() && n) ? (BIT(m_tgcr, 8, 4) + 1) : 1;
}

u64 tms320c6745_timer_device::count(unsigned n) const
{
	if (!m_running[n])
		return chained() ? ((u64(m_tim[1]) << 32) | m_tim[0]) : m_tim[n];
	u64 const ticks = (machine().time() - m_start[n]).as_ticks(clock()) / prescale(n);
	return m_base[n] + ticks;
}

void tms320c6745_timer_device::update(unsigned n)
{
	bool const in_reset = chained() ? ((m_tgcr & 3) != 3) : !BIT(m_tgcr, n);
	bool const run = !in_reset && enamode(n);

	if (m_running[n])
	{
		u64 const c = count(n);
		if (chained())
		{
			m_tim[0] = u32(c);
			m_tim[1] = u32(c >> 32);
		}
		else
			m_tim[n] = u32(c);
	}
	if (in_reset)
	{
		m_tim[n] = 0;
		if (chained())
			m_tim[0] = m_tim[1] = 0;
	}

	m_running[n] = run;
	m_timer[n]->adjust(attotime::never);
	if (!run)
		return;

	m_start[n] = machine().time();
	m_base[n] = chained() ? ((u64(m_tim[1]) << 32) | m_tim[0]) : m_tim[n];
	u64 const prd = period(n);
	u64 const remaining = (m_base[n] <= prd) ? (prd - m_base[n] + 1) : 1;
	m_timer[n]->adjust(attotime::from_ticks(remaining * prescale(n), clock()), n);
}

TIMER_CALLBACK_MEMBER(tms320c6745_timer_device::period_expired)
{
	unsigned const n = param;

	// the counter resets to 0 and the period interrupt fires
	m_intctlstat |= n ? 0x0002'0000 : 0x0000'0002;
	if (n)
	{
		m_tint34_cb(1);
		m_tint34_cb(0);
	}
	else
	{
		m_tint12_cb(1);
		m_tint12_cb(0);
	}

	m_running[n] = false;
	m_tim[n] = 0;
	if (chained())
		m_tim[0] = m_tim[1] = 0;

	switch (enamode(n))
	{
	case 1: // one shot
		m_tcr &= ~(3U << (n ? 22 : 6));
		break;
	case 3: // continuous with reload
		m_prd[n] = m_rel[n];
		if (chained())
			m_prd[1] = m_rel[1];
		break;
	default:
		break;
	}
	update(n);
}

u32 tms320c6745_timer_device::tim_r(offs_t offset)
{
	if (chained())
		return u32(count(0) >> (offset * 32));
	return u32(count(offset));
}

void tms320c6745_timer_device::tim_w(offs_t offset, u32 data, u32 mem_mask)
{
	unsigned const n = chained() ? 0 : offset;
	if (m_running[n])
	{
		u64 const c = count(n);
		if (chained())
		{
			m_tim[0] = u32(c);
			m_tim[1] = u32(c >> 32);
		}
		else
			m_tim[n] = u32(c);
		m_running[n] = false;
	}
	COMBINE_DATA(&m_tim[offset]);
	update(n);
}

void tms320c6745_timer_device::prd_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_prd[offset]);
	update(chained() ? 0 : offset);
}

void tms320c6745_timer_device::tcr_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_tcr);
	update(0);
	if (!chained())
		update(1);
}

void tms320c6745_timer_device::tgcr_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_tgcr);
	if (BIT(m_tgcr, 2, 2) == 3)
		logerror("dual 32-bit chained mode not supported\n");
	update(0);
	if (!chained())
		update(1);
	else
		m_timer[1]->adjust(attotime::never);
}
