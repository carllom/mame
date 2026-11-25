// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 multichannel audio serial port (McASP0, McASP1)

    Reference: SPRUH91D chapter 22.

    Word level model of TDM mode with the transmitter's clocks and frame
    sync: a timer ticks at every slot boundary. Each active transmit slot
    sends the shift register (XRSR) of every transmit serializer through
    the format unit to the axr_w callbacks, reloads it from XBUF and asks
    for the next word with XDATA and AXEVT, so a word goes out two slots
    after it is written. Each
    active receive slot that ends reads the axr_r callbacks through the
    format unit into RBUF and raises RDATA and AREVT. The buffers can be
    serviced through the configuration bus (XBUFn, RBUFn) or the data
    port, which goes through the serializers in ascending order.

    Releasing the transmit serializers from reset (XSRCLR) empties their
    buffers and raises AXEVT at once, and releasing the state machine
    (XSMRST) moves XBUF to the shift registers and raises it again, so two
    words are taken before the frame sync starts. The MS-100BT primes its
    DMA with a frame for this; without the shift register stage its left
    and right channels come out a slot apart.

    TODO:
    - separate receive timing (ASYNC), external clocks and frame syncs
    - AFIFO, interrupts, clock checking, DIT mode, loopback, pin GPIO
    - the first bit delay (XDATDLY, RDATDLY) has to match on both sides

***************************************************************************/

#include "emu.h"
#include "tms320c6745_mcasp.h"

#include <algorithm>
#include <bit>


DEFINE_DEVICE_TYPE(TMS320C6745_MCASP, tms320c6745_mcasp_device, "tms320c6745_mcasp", "TMS320C6745 McASP")


namespace {

enum : u32
{
	GBLCTL = 0x44 / 4,
	RMASK = 0x64 / 4,
	RFMT = 0x68 / 4,
	AFSRCTL = 0x6c / 4,
	RTDM = 0x78 / 4,
	RSTAT = 0x80 / 4,
	RSLOT = 0x84 / 4,
	REVTCTL = 0x8c / 4,
	XMASK = 0xa4 / 4,
	XFMT = 0xa8 / 4,
	AFSXCTL = 0xac / 4,
	ACLKXCTL = 0xb0 / 4,
	AHCLKXCTL = 0xb4 / 4,
	XTDM = 0xb8 / 4,
	XSTAT = 0xc0 / 4,
	XSLOT = 0xc4 / 4,
	XEVTCTL = 0xcc / 4,
	SRCTL0 = 0x180 / 4
};

// GBLCTL: receiver bits 0-4, transmitter bits 8-12
enum : u32
{
	RSRCLR = 1U << 2, RSMRST = 1U << 3, RFRST = 1U << 4,
	XSRCLR = 1U << 10, XSMRST = 1U << 11, XFRST = 1U << 12
};

// XSTAT and RSTAT
enum : u32
{
	STAT_UNDRN_OVRN = 1U << 0,
	STAT_DATA = 1U << 5,
	STAT_STAFRM = 1U << 6,
	STAT_ERR = 1U << 8
};

// SRCTLn serializer modes
enum : u32 { SRMOD_INACTIVE = 0, SRMOD_TX = 1, SRMOD_RX = 2 };

u32 reverse_bits(u32 data)
{
	data = ((data >> 1) & 0x5555'5555) | ((data & 0x5555'5555) << 1);
	data = ((data >> 2) & 0x3333'3333) | ((data & 0x3333'3333) << 2);
	data = ((data >> 4) & 0x0f0f'0f0f) | ((data & 0x0f0f'0f0f) << 4);
	return swapendian_int32(data);
}

// pad value for the bits the mask clears: zeros, ones, or copies of one bit
u32 pad_bits(u32 fmt, u32 data)
{
	switch (BIT(fmt, 13, 2))
	{
	case 0: return 0;
	case 1: return ~u32(0);
	default: return BIT(data, BIT(fmt, 8, 5)) ? ~u32(0) : 0;
	}
}

} // anonymous namespace


tms320c6745_mcasp_device::tms320c6745_mcasp_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_MCASP, tag, owner, clock),
	m_axevt_cb(*this),
	m_arevt_cb(*this),
	m_axr_w_cb(*this),
	m_axr_r_cb(*this, 0),
	m_slot_timer(nullptr),
	m_reg{},
	m_xbuf{},
	m_xrsr{},
	m_rbuf{},
	m_xbuf_empty(0),
	m_rbuf_full(0),
	m_slot(0),
	m_rx_slot(-1),
	m_running(false)
{
}

void tms320c6745_mcasp_device::device_start()
{
	m_slot_timer = timer_alloc(FUNC(tms320c6745_mcasp_device::slot_tick), this);

	save_item(NAME(m_reg));
	save_item(NAME(m_xbuf));
	save_item(NAME(m_xrsr));
	save_item(NAME(m_rbuf));
	save_item(NAME(m_xbuf_empty));
	save_item(NAME(m_rbuf_full));
	save_item(NAME(m_slot));
	save_item(NAME(m_rx_slot));
	save_item(NAME(m_running));
}

void tms320c6745_mcasp_device::device_reset()
{
	std::fill(std::begin(m_reg), std::end(m_reg), 0);
	std::fill(std::begin(m_xbuf), std::end(m_xbuf), 0);
	std::fill(std::begin(m_xrsr), std::end(m_xrsr), 0);
	std::fill(std::begin(m_rbuf), std::end(m_rbuf), 0);
	m_xbuf_empty = 0;
	m_rbuf_full = 0;
	m_slot = 0;
	m_rx_slot = -1;
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
	map(0x180, 0x1bf).rw(FUNC(tms320c6745_mcasp_device::srctl_r), FUNC(tms320c6745_mcasp_device::srctl_w));
	map(0x200, 0x23f).lr32(NAME([this] (offs_t offset) -> u32 { return m_xbuf[offset]; })).w(FUNC(tms320c6745_mcasp_device::xbuf_w));
	map(0x280, 0x2bf).r(FUNC(tms320c6745_mcasp_device::rbuf_r)).nopw();
}

void tms320c6745_mcasp_device::gblctl_w(offs_t offset, u32 data, u32 mem_mask)
{
	u32 value = m_reg[GBLCTL];
	COMBINE_DATA(&value);
	set_gblctl(value);
}

void tms320c6745_mcasp_device::rgblctl_w(u32 data)
{
	// receiver half of GBLCTL
	set_gblctl((m_reg[GBLCTL] & ~0x0000'001f) | (data & 0x0000'001f));
}

void tms320c6745_mcasp_device::xgblctl_w(u32 data)
{
	// transmitter half of GBLCTL
	set_gblctl((m_reg[GBLCTL] & ~0x0000'1f00) | (data & 0x0000'1f00));
}

void tms320c6745_mcasp_device::set_gblctl(u32 data)
{
	u32 const old = m_reg[GBLCTL];
	m_reg[GBLCTL] = data;

	if (!(data & RSRCLR))
	{
		m_rbuf_full = 0;
		m_reg[RSTAT] &= ~STAT_DATA;
	}
	if (!(data & XSRCLR))
	{
		m_xbuf_empty = 0;
		m_reg[XSTAT] &= ~STAT_DATA;
	}
	else if (!(old & XSRCLR) || ((data & XSMRST) && !(old & XSMRST)))
	{
		// serializers out of reset with empty buffers, or the state machine
		// out of reset moving the buffers to the shift registers: both ask
		// for data before the first slot
		if (old & XSRCLR)
			std::copy(std::begin(m_xbuf), std::end(m_xbuf), std::begin(m_xrsr));
		m_xbuf_empty = serializers(SRMOD_TX);
		if (m_xbuf_empty)
		{
			m_reg[XSTAT] |= STAT_DATA;
			if (!BIT(m_reg[XEVTCTL], 0))
			{
				m_axevt_cb(1);
				m_axevt_cb(0);
			}
		}
	}

	update_timer();
}

template <u32 Reg>
void tms320c6745_mcasp_device::stat_w(offs_t offset, u32 data, u32 mem_mask)
{
	m_reg[Reg] &= ~(data & mem_mask & 0x01ff); // write 1 to clear
}

u32 tms320c6745_mcasp_device::srctl_r(offs_t offset)
{
	// XRDY and RRDY show the buffer state
	return (m_reg[SRCTL0 + offset] & 0x0000'000f) | (BIT(m_xbuf_empty, offset) << 4) | (BIT(m_rbuf_full, offset) << 5);
}

void tms320c6745_mcasp_device::srctl_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_reg[SRCTL0 + offset]);
}

void tms320c6745_mcasp_device::xbuf_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_xbuf[offset]);
	xbuf_serviced(offset);
}

u32 tms320c6745_mcasp_device::rbuf_r(offs_t offset)
{
	if (!machine().side_effects_disabled())
		rbuf_serviced(offset);
	return m_rbuf[offset];
}

void tms320c6745_mcasp_device::data_map(address_map &map)
{
	map(0x000, 0xfff).rw(FUNC(tms320c6745_mcasp_device::data_r), FUNC(tms320c6745_mcasp_device::data_w));
}

u32 tms320c6745_mcasp_device::data_r()
{
	// the lowest numbered receive serializer that has data
	if (!m_rbuf_full)
		return 0;
	unsigned const n = std::countr_zero(m_rbuf_full);
	if (!machine().side_effects_disabled())
		rbuf_serviced(n);
	return m_rbuf[n];
}

void tms320c6745_mcasp_device::data_w(u32 data)
{
	// the lowest numbered transmit serializer that wants data
	if (!m_xbuf_empty)
		return;
	unsigned const n = std::countr_zero(m_xbuf_empty);
	m_xbuf[n] = data;
	xbuf_serviced(n);
}

void tms320c6745_mcasp_device::xbuf_serviced(u32 n)
{
	m_xbuf_empty &= ~(1U << n);
	if (!m_xbuf_empty)
		m_reg[XSTAT] &= ~STAT_DATA;
}

void tms320c6745_mcasp_device::rbuf_serviced(u32 n)
{
	m_rbuf_full &= ~(1U << n);
	if (!m_rbuf_full)
		m_reg[RSTAT] &= ~STAT_DATA;
}

u32 tms320c6745_mcasp_device::serializers(u32 mode) const
{
	u32 result = 0;
	for (unsigned n = 0; n < SERIALIZERS; n++)
	{
		if (BIT(m_reg[SRCTL0 + n], 0, 2) == mode)
			result |= 1U << n;
	}
	return result;
}

u32 tms320c6745_mcasp_device::slot_bits(u32 fmt) const
{
	// XSSZ/RSSZ: 8 to 32 bits in steps of 4
	u32 const ssz = BIT(fmt, 4, 4);
	return (ssz >= 3) ? (ssz + 1) * 2 : 32;
}

u32 tms320c6745_mcasp_device::tx_format(u32 data) const
{
	// mask and pad, rotate right, then bit reverse for MSB first
	u32 const fmt = m_reg[XFMT];
	u32 const mask = m_reg[XMASK];
	data = (data & mask) | (pad_bits(fmt, data) & ~mask);
	data = std::rotr(data, 4 * BIT(fmt, 0, 3));
	if (!BIT(fmt, 15))
		data = reverse_bits(data); // LSB first: bit 0 goes out first
	return data & (~u32(0) << (32 - slot_bits(fmt)));
}

u32 tms320c6745_mcasp_device::rx_format(u32 data) const
{
	// bit reverse for LSB first, rotate right, then mask and pad
	u32 const fmt = m_reg[RFMT];
	u32 const mask = m_reg[RMASK];
	data &= ~u32(0) << (32 - slot_bits(fmt));
	if (!BIT(fmt, 15))
		data = reverse_bits(data);
	data = std::rotr(data, 4 * BIT(fmt, 0, 3));
	return (data & mask) | (pad_bits(fmt, data) & ~mask);
}

void tms320c6745_mcasp_device::transmit_slot(u32 slot)
{
	m_reg[XSLOT] = slot;
	if (!BIT(m_reg[XTDM], slot))
		return;

	u32 const active = serializers(SRMOD_TX);
	if (m_xbuf_empty & active)
		m_reg[XSTAT] |= STAT_UNDRN_OVRN | STAT_ERR;
	for (unsigned n = 0; n < SERIALIZERS; n++)
	{
		if (BIT(active, n))
		{
			m_axr_w_cb[n](slot, tx_format(m_xrsr[n]));
			m_xrsr[n] = m_xbuf[n];
		}
	}

	// buffers move to the shift registers and want the next word
	m_xbuf_empty = active;
	if (slot == 0)
		m_reg[XSTAT] |= STAT_STAFRM;
	if (active)
	{
		m_reg[XSTAT] |= STAT_DATA;
		if (!BIT(m_reg[XEVTCTL], 0))
		{
			m_axevt_cb(1);
			m_axevt_cb(0);
		}
	}
}

void tms320c6745_mcasp_device::receive_slot(u32 slot)
{
	m_reg[RSLOT] = slot;
	if (!BIT(m_reg[RTDM], slot))
		return;

	u32 const active = serializers(SRMOD_RX);
	if (m_rbuf_full & active)
		m_reg[RSTAT] |= STAT_UNDRN_OVRN | STAT_ERR;
	for (unsigned n = 0; n < SERIALIZERS; n++)
	{
		if (BIT(active, n))
			m_rbuf[n] = rx_format(m_axr_r_cb[n](slot));
	}

	m_rbuf_full = active;
	if (slot == 0)
		m_reg[RSTAT] |= STAT_STAFRM;
	if (active)
	{
		m_reg[RSTAT] |= STAT_DATA;
		if (!BIT(m_reg[REVTCTL], 0))
		{
			m_arevt_cb(1);
			m_arevt_cb(0);
		}
	}
}

void tms320c6745_mcasp_device::update_timer()
{
	// transmit or receive state machine and frame sync out of reset
	u32 const gblctl = m_reg[GBLCTL];
	bool const tx = (gblctl & (XSMRST | XFRST)) == (XSMRST | XFRST);
	bool const rx = (gblctl & (RSMRST | RFRST)) == (RSMRST | RFRST);
	bool const run = tx || rx;
	if (run == m_running)
		return;
	m_running = run;
	m_rx_slot = -1;
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
	if (!aclk)
	{
		logerror("no transmit clock\n");
		return;
	}
	m_slot = 0;
	attotime const period = attotime::from_ticks(slot_bits(m_reg[XFMT]), aclk);
	m_slot_timer->adjust(period, 0, period);
}

TIMER_CALLBACK_MEMBER(tms320c6745_mcasp_device::slot_tick)
{
	u32 slots = BIT(m_reg[AFSXCTL], 7, 9);
	if (slots < 2)
		slots = 1;
	slots = std::min(slots, 32U);
	u32 const slot = m_slot;
	m_slot = (m_slot + 1) % slots;

	// the receive slot that just ended, then the transmit slot that starts
	u32 const gblctl = m_reg[GBLCTL];
	if (m_rx_slot >= 0)
		receive_slot(m_rx_slot);
	m_rx_slot = ((gblctl & (RSMRST | RFRST)) == (RSMRST | RFRST)) ? s16(slot) : -1;
	if ((gblctl & (XSMRST | XFRST)) == (XSMRST | XFRST))
		transmit_slot(slot);
}
