// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 serial peripheral interface (SPI0, SPI1)

    Reference: SPRUH91D chapter 27.

    Master mode only. A word written to SPIDAT0/SPIDAT1 is transferred at
    once through a callback, which returns the word shifted in.

    TODO:
    - transfer timing and delays, slave mode, SPIx_ENA
    - EDMA requests
    - GPIO mode of the SPI pins other than SCS0 output, SPIPC2 input levels

***************************************************************************/

#include "emu.h"
#include "tms320c6745_spi.h"

#include <algorithm>


DEFINE_DEVICE_TYPE(TMS320C6745_SPI, tms320c6745_spi_device, "tms320c6745_spi", "TMS320C6745 SPI")


tms320c6745_spi_device::tms320c6745_spi_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_SPI, tag, owner, clock),
	m_transfer_cb(*this),
	m_scs0_cb(*this),
	m_irq_cb(*this),
	m_gcr0(0),
	m_gcr1(0),
	m_int0(0),
	m_lvl(0),
	m_flg(0),
	m_pc{},
	m_dat1(0),
	m_buf(0),
	m_delay(0),
	m_def(0),
	m_fmt{},
	m_scs0(1),
	m_irq(0)
{
}

void tms320c6745_spi_device::device_resolve_objects()
{
	m_transfer_cb.resolve();
}

void tms320c6745_spi_device::device_start()
{
	save_item(NAME(m_gcr0));
	save_item(NAME(m_gcr1));
	save_item(NAME(m_int0));
	save_item(NAME(m_lvl));
	save_item(NAME(m_flg));
	save_item(NAME(m_pc));
	save_item(NAME(m_dat1));
	save_item(NAME(m_buf));
	save_item(NAME(m_delay));
	save_item(NAME(m_def));
	save_item(NAME(m_fmt));
	save_item(NAME(m_scs0));
	save_item(NAME(m_irq));
}

void tms320c6745_spi_device::device_reset()
{
	m_gcr0 = 0;
	m_gcr1 = 0;
	m_int0 = 0;
	m_lvl = 0;
	m_flg = 0;
	std::fill(std::begin(m_pc), std::end(m_pc), 0);
	m_dat1 = 0;
	m_buf = 0x8000'0000; // RXEMPTY
	m_delay = 0;
	m_def = 0xff;
	std::fill(std::begin(m_fmt), std::end(m_fmt), 0);
	set_scs0(1);
	m_irq = 0;
	m_irq_cb(0);
}

void tms320c6745_spi_device::map(address_map &map)
{
	map(0x00, 0x03).lrw32(NAME([this] () -> u32 { return m_gcr0; }), NAME([this] (u32 data) { m_gcr0 = data & 1; if (!m_gcr0) device_reset(); }));
	map(0x04, 0x07).lrw32(NAME([this] () -> u32 { return m_gcr1; }), NAME([this] (u32 data) { gcr1_w(data); }));
	map(0x08, 0x0b).lrw32(NAME([this] () -> u32 { return m_int0; }), NAME([this] (u32 data) { m_int0 = data; update_irq(); }));
	map(0x0c, 0x0f).lrw32(NAME([this] () -> u32 { return m_lvl; }), NAME([this] (u32 data) { m_lvl = data; }));
	map(0x10, 0x13).lrw32(
			NAME([this] () -> u32 { return m_flg; }),
			NAME([this] (u32 data) { m_flg &= ~(data & 0x0000'015f); update_irq(); }));
	map(0x14, 0x23).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_pc[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_pc[offset] = data; update_gpio(); })); // SPIPC0-SPIPC3
	map(0x24, 0x27).lrw32(
			NAME([this] () -> u32 { return m_pc[3]; }),
			NAME([this] (u32 data) { m_pc[3] |= data; update_gpio(); })); // SPIPC4: set DOUT bits
	map(0x28, 0x2b).lrw32(
			NAME([this] () -> u32 { return m_pc[3]; }),
			NAME([this] (u32 data) { m_pc[3] &= ~data; update_gpio(); })); // SPIPC5: clear DOUT bits
	map(0x38, 0x3b).w(FUNC(tms320c6745_spi_device::dat0_w));
	map(0x3c, 0x3f).lr32(NAME([this] () -> u32 { return m_dat1; })).w(FUNC(tms320c6745_spi_device::dat1_w));
	map(0x40, 0x43).r(FUNC(tms320c6745_spi_device::buf_r));
	map(0x44, 0x47).lr32(NAME([this] () -> u32 { return m_buf & 0xffff; })); // SPIEMU
	map(0x48, 0x4b).lrw32(NAME([this] () -> u32 { return m_delay; }), NAME([this] (u32 data) { m_delay = data; }));
	map(0x4c, 0x4f).lrw32(NAME([this] () -> u32 { return m_def; }), NAME([this] (u32 data) { m_def = data; if (!BIT(m_dat1, 28)) set_scs0(BIT(m_def, 0)); }));
	map(0x50, 0x5f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_fmt[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_fmt[offset] = data; }));
}

void tms320c6745_spi_device::set_scs0(int state)
{
	if (state != m_scs0)
	{
		m_scs0 = state;
		m_scs0_cb(state);
	}
}

void tms320c6745_spi_device::update_gpio()
{
	// SCS0 as a GPIO pin follows SPIPC3 DOUT when SPIPC1 makes it an output; an input floats high
	if (!BIT(m_pc[0], 0))
		set_scs0(!BIT(m_pc[1], 0) || BIT(m_pc[3], 0));
}

void tms320c6745_spi_device::update_irq()
{
	// RXINTENA, TXINTENA and the error interrupts share one line
	int const state = (m_flg & m_int0 & 0x0000'035f) ? 1 : 0;
	if (state != m_irq)
	{
		m_irq = state;
		m_irq_cb(state);
	}
}

void tms320c6745_spi_device::gcr1_w(u32 data)
{
	m_gcr1 = data & 0x0101'0f03;

	// TXINTFLG means the transmit buffer is empty; transfers finish at once, so it is whenever the SPI is enabled
	if (BIT(m_gcr1, 24))
		m_flg |= 1U << 9;
	else
		m_flg &= ~(1U << 9);
	update_irq();
}

void tms320c6745_spi_device::transfer(u16 data)
{
	if (!BIT(m_gcr1, 24) || !BIT(m_gcr1, 0))
	{
		logerror("transfer with SPI disabled or in slave mode\n");
		return;
	}

	u32 const fmt = m_fmt[BIT(m_dat1, 24, 2)];
	u32 bits = fmt & 0x1f;
	if (bits < 2)
		bits = 16;

	// SCS0 takes the CSNR level for the transfer when it's a functional pin
	if (BIT(m_pc[0], 0))
		set_scs0(BIT(m_dat1, 16));

	u32 out = data & make_bitmask<u32>(bits);
	bool const lsb_first = BIT(fmt, 20);
	if (lsb_first)
		out = bitswap<16>(out, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15) >> (16 - bits);
	u32 in = (m_transfer_cb.isnull() ? ~u32(0) : m_transfer_cb(out, bits)) & make_bitmask<u32>(bits);
	if (lsb_first)
		in = bitswap<16>(in << (16 - bits), 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15);

	if (BIT(m_pc[0], 0) && !BIT(m_dat1, 28))
		set_scs0(BIT(m_def, 0));

	if (!BIT(m_buf, 31))
		m_buf |= 1U << 30; // RXOVR
	m_buf = (m_buf & 0x4000'0000) | (in & 0xffff);
	m_flg |= (1U << 8) | (1U << 9); // RXINTFLG, TXINTFLG
	update_irq();
}

u32 tms320c6745_spi_device::buf_r()
{
	u32 const value = m_buf;
	if (!machine().side_effects_disabled())
	{
		m_buf = (m_buf & 0xffff) | 0x8000'0000; // RXEMPTY, clears RXOVR
		m_flg &= ~(1U << 8);
		update_irq();
	}
	return value;
}

void tms320c6745_spi_device::dat0_w(offs_t offset, u32 data, u32 mem_mask)
{
	if (ACCESSING_BITS_0_15)
	{
		m_dat1 = (m_dat1 & 0xffff'0000) | (data & 0xffff);
		transfer(data);
	}
}

void tms320c6745_spi_device::dat1_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_dat1);

	// writing only the control field selects the format without a transfer
	if (ACCESSING_BITS_0_15)
		transfer(m_dat1 & 0xffff);
	else if (BIT(m_pc[0], 0) && !BIT(m_dat1, 28))
		set_scs0(BIT(m_def, 0));
}
