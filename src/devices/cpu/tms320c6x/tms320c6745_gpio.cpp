// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 general-purpose I/O (8 banks of 16 pins)

    Reference: SPRUH91D chapter 15.

    Each pair of banks has DIR, OUT_DATA, SET_DATA, CLR_DATA, IN_DATA, the
    edge trigger set/clear registers and INTSTAT. Output pins read back
    their output level. A rising or falling edge enabled in SET_RIS_TRIG or
    SET_FAL_TRIG sets INTSTAT and, if the bank is enabled in BINTEN, pulses
    the bank interrupt.

    TODO:
    - EDMA events from the bank interrupts

***************************************************************************/

#include "emu.h"
#include "tms320c6745_gpio.h"


DEFINE_DEVICE_TYPE(TMS320C6745_GPIO, tms320c6745_gpio_device, "tms320c6745_gpio", "TMS320C6745 GPIO")


tms320c6745_gpio_device::tms320c6745_gpio_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_GPIO, tag, owner, clock),
	m_out_cb(*this),
	m_irq_cb(*this),
	m_binten(0),
	m_in{ 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff },
	m_dir{},
	m_out{},
	m_ris{},
	m_fal{},
	m_intstat{}
{
}

void tms320c6745_gpio_device::device_start()
{
	save_item(NAME(m_binten));
	save_item(NAME(m_in));
	save_item(NAME(m_dir));
	save_item(NAME(m_out));
	save_item(NAME(m_ris));
	save_item(NAME(m_fal));
	save_item(NAME(m_intstat));
}

void tms320c6745_gpio_device::device_reset()
{
	m_binten = 0;
	for (unsigned b = 0; b < BANKS; b++)
	{
		m_dir[b] = 0xffff; // all inputs
		m_out[b] = 0;
		m_ris[b] = 0;
		m_fal[b] = 0;
		m_intstat[b] = 0;
		m_out_cb[b](output(b));
	}
}

void tms320c6745_gpio_device::map(address_map &map)
{
	map(0x00, 0x03).lr32(NAME([] () -> u32 { return 0x4483'0105; })); // REVID
	map(0x08, 0x0b).lrw32(NAME([this] () -> u32 { return m_binten; }), NAME([this] (u32 data) { m_binten = data & 0xff; }));
	map(0x10, 0xaf).rw(FUNC(tms320c6745_gpio_device::pair_r), FUNC(tms320c6745_gpio_device::pair_w));
}

void tms320c6745_gpio_device::in_w(unsigned bank, u16 data)
{
	u16 const old_pins = pins(bank);
	m_in[bank] = data;
	update(bank, output(bank), old_pins);
}

void tms320c6745_gpio_device::update(unsigned bank, u16 old_out, u16 old_pins)
{
	u16 const now = output(bank);
	if (now != old_out)
		m_out_cb[bank](now);

	// edge detection works on the pin level, whichever way the pin is driven
	u16 const level = pins(bank);
	u16 const edges = (~old_pins & level & m_ris[bank]) | (old_pins & ~level & m_fal[bank]);
	if (edges)
	{
		m_intstat[bank] |= edges;
		if (BIT(m_binten, bank))
		{
			m_irq_cb[bank](1);
			m_irq_cb[bank](0);
		}
	}
}

u32 tms320c6745_gpio_device::pair_r(offs_t offset)
{
	unsigned const pair = offset / 10;
	unsigned const reg = offset % 10;
	unsigned const lo = pair * 2, hi = lo + 1;
	auto const both = [] (u16 l, u16 h) -> u32 { return (u32(h) << 16) | l; };

	switch (reg)
	{
	case 0: return both(m_dir[lo], m_dir[hi]);
	case 1: case 2: case 3: return both(m_out[lo], m_out[hi]);
	case 4:
		{
			// inputs read the pins, outputs read back what they drive
			return both(pins(lo), pins(hi));
		}
	case 5: case 6: return both(m_ris[lo], m_ris[hi]);
	case 7: case 8: return both(m_fal[lo], m_fal[hi]);
	case 9: return both(m_intstat[lo], m_intstat[hi]);
	default: return 0;
	}
}

void tms320c6745_gpio_device::pair_w(offs_t offset, u32 data, u32 mem_mask)
{
	unsigned const pair = offset / 10;
	unsigned const reg = offset % 10;
	unsigned const lo = pair * 2, hi = lo + 1;
	u16 const old_out[2] = { output(lo), output(hi) };
	u16 const old_pins[2] = { pins(lo), pins(hi) };
	u16 const dl = ACCESSING_BITS_0_15 ? u16(data) : 0;
	u16 const dh = ACCESSING_BITS_16_31 ? u16(data >> 16) : 0;
	u16 const ml = ACCESSING_BITS_0_15 ? 0xffff : 0;
	u16 const mh = ACCESSING_BITS_16_31 ? 0xffff : 0;

	switch (reg)
	{
	case 0: m_dir[lo] = (m_dir[lo] & ~ml) | dl; m_dir[hi] = (m_dir[hi] & ~mh) | dh; break;
	case 1: m_out[lo] = (m_out[lo] & ~ml) | dl; m_out[hi] = (m_out[hi] & ~mh) | dh; break;
	case 2: m_out[lo] |= dl; m_out[hi] |= dh; break;
	case 3: m_out[lo] &= ~dl; m_out[hi] &= ~dh; break;
	case 5: m_ris[lo] |= dl; m_ris[hi] |= dh; break;
	case 6: m_ris[lo] &= ~dl; m_ris[hi] &= ~dh; break;
	case 7: m_fal[lo] |= dl; m_fal[hi] |= dh; break;
	case 8: m_fal[lo] &= ~dl; m_fal[hi] &= ~dh; break;
	case 9: m_intstat[lo] &= ~dl; m_intstat[hi] &= ~dh; break;
	default: break;
	}

	update(lo, old_out[0], old_pins[0]);
	update(hi, old_out[1], old_pins[1]);
}
