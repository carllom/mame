// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 general-purpose I/O (8 banks of 16 pins)

    Reference: SPRUH91D chapter 15.

    Each pair of banks has DIR, OUT_DATA, SET_DATA, CLR_DATA, IN_DATA, the
    edge trigger set/clear registers and INTSTAT. Output pins read back
    their output level.

    TODO:
    - edge detection and bank interrupts (events 65, 41, 49, 52, 54, 59,
      62, 72)

***************************************************************************/

#include "emu.h"
#include "tms320c6745_gpio.h"


DEFINE_DEVICE_TYPE(TMS320C6745_GPIO, tms320c6745_gpio_device, "tms320c6745_gpio", "TMS320C6745 GPIO")


tms320c6745_gpio_device::tms320c6745_gpio_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_GPIO, tag, owner, clock),
	m_out_cb(*this),
	m_in_cb(*this, 0xffff),
	m_binten(0),
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
		u16 const old = output(b);
		m_dir[b] = 0xffff; // all inputs
		m_out[b] = 0;
		m_ris[b] = 0;
		m_fal[b] = 0;
		m_intstat[b] = 0;
		update(b, ~old);
	}
}

void tms320c6745_gpio_device::map(address_map &map)
{
	map(0x00, 0x03).lr32(NAME([] () -> u32 { return 0x4483'0105; })); // REVID
	map(0x08, 0x0b).lrw32(NAME([this] () -> u32 { return m_binten; }), NAME([this] (u32 data) { m_binten = data & 0xff; }));
	map(0x10, 0xaf).rw(FUNC(tms320c6745_gpio_device::pair_r), FUNC(tms320c6745_gpio_device::pair_w));
}

void tms320c6745_gpio_device::update(unsigned bank, u16 old)
{
	u16 const now = output(bank);
	if (now != old)
		m_out_cb[bank](now);
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
			u16 const inl = m_in_cb[lo]();
			u16 const inh = m_in_cb[hi]();
			return both((inl & m_dir[lo]) | (m_out[lo] & ~m_dir[lo]), (inh & m_dir[hi]) | (m_out[hi] & ~m_dir[hi]));
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
	u16 const old[2] = { output(lo), output(hi) };
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

	update(lo, old[0]);
	update(hi, old[1]);
}
