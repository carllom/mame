// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Sitronix ST7565 65 x 132 dot matrix LCD controller/driver

    Also covers the command-compatible ST7565R/ST7567 and similar parts.
    The bus interface (parallel or serial) is up to the host, which
    writes commands and display data a byte at a time.

    TODO:
    - reads (status, display data) for the parallel interfaces
    - contrast, bias and power control effects
    - the static indicator

***************************************************************************/

#include "emu.h"
#include "st7565.h"

#include "screen.h"

#define VERBOSE (0)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(ST7565, st7565_device, "st7565", "Sitronix ST7565 LCD controller")


st7565_device::st7565_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, ST7565, tag, owner, clock),
	m_ram{},
	m_page(0),
	m_column(0),
	m_rmw_column(0),
	m_start_line(0),
	m_pending(0),
	m_volume(0),
	m_power(0),
	m_display_on(false),
	m_adc_reverse(false),
	m_com_reverse(false),
	m_inverse(false),
	m_all_on(false),
	m_rmw(false),
	m_reset(false)
{
}

void st7565_device::device_start()
{
	save_item(NAME(m_ram));
	save_item(NAME(m_page));
	save_item(NAME(m_column));
	save_item(NAME(m_rmw_column));
	save_item(NAME(m_start_line));
	save_item(NAME(m_pending));
	save_item(NAME(m_volume));
	save_item(NAME(m_power));
	save_item(NAME(m_display_on));
	save_item(NAME(m_adc_reverse));
	save_item(NAME(m_com_reverse));
	save_item(NAME(m_inverse));
	save_item(NAME(m_all_on));
	save_item(NAME(m_rmw));
	save_item(NAME(m_reset));
}

void st7565_device::device_reset()
{
	// display RAM keeps its contents through a reset
	m_page = 0;
	m_column = 0;
	m_start_line = 0;
	m_pending = 0;
	m_volume = 0x20;
	m_power = 0;
	m_display_on = false;
	m_adc_reverse = false;
	m_com_reverse = false;
	m_inverse = false;
	m_all_on = false;
	m_rmw = false;
}

void st7565_device::reset_w(int state)
{
	// /RES is active low
	if (!state && !m_reset)
		device_reset();
	m_reset = !state;
}

void st7565_device::write(offs_t offset, u8 data)
{
	if (offset & 1)
		data_w(data);
	else
		control_w(data);
}

void st7565_device::data_w(u8 data)
{
	if (m_reset)
		return;
	if (m_page < PAGES && m_column < COLUMNS)
		m_ram[m_page * COLUMNS + m_column] = data;
	if (m_column < COLUMNS)
		m_column++;
}

void st7565_device::control_w(u8 data)
{
	if (m_reset)
		return;

	// second byte of a two-byte command
	if (m_pending)
	{
		switch (m_pending)
		{
		case 0x81: m_volume = data & 0x3f; break;
		case 0xf8: break; // booster ratio
		case 0xac: case 0xad: break; // static indicator register
		}
		m_pending = 0;
		return;
	}

	switch (data & 0xf0)
	{
	case 0x00: // column address low
		m_column = (m_column & 0xf0) | (data & 0x0f);
		return;
	case 0x10: // column address high
		m_column = (m_column & 0x0f) | ((data & 0x0f) << 4);
		return;
	case 0x20: // regulator resistor ratio (0x20-0x27), power control (0x28-0x2f)
		if (data & 0x08)
			m_power = data & 7;
		return;
	case 0x40: case 0x50: case 0x60: case 0x70: // display start line
		m_start_line = data & 0x3f;
		return;
	case 0xb0: // page address
		m_page = data & 0x0f;
		return;
	case 0xc0: // common output mode
		m_com_reverse = BIT(data, 3);
		return;
	}

	switch (data)
	{
	case 0xae: case 0xaf: m_display_on = BIT(data, 0); break;
	case 0xa0: case 0xa1: m_adc_reverse = BIT(data, 0); break;
	case 0xa2: case 0xa3: break; // LCD bias
	case 0xa4: case 0xa5: m_all_on = BIT(data, 0); break;
	case 0xa6: case 0xa7: m_inverse = BIT(data, 0); break;
	case 0x81: case 0xf8: case 0xac: case 0xad: m_pending = data; break;
	case 0xe0: m_rmw = true; m_rmw_column = m_column; break;
	case 0xee: m_rmw = false; m_column = m_rmw_column; break;
	case 0xe2: device_reset(); break;
	case 0xe3: break; // NOP
	default:
		LOG("unknown command %02x\n", data);
		break;
	}
}

u32 st7565_device::screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect)
{
	for (int y = cliprect.top(); y <= cliprect.bottom(); y++)
	{
		// common driver y shows display line (start line + y), scanned in either direction
		u32 const com = m_com_reverse ? (63 - (y & 63)) : (y & 63);
		u32 const line = (y >= 64) ? 64 : ((com + m_start_line) & 63);
		u8 const *const row = &m_ram[(line >> 3) * COLUMNS];
		u16 *const dst = &bitmap.pix(y);
		for (int x = cliprect.left(); x <= cliprect.right(); x++)
		{
			u32 const column = m_adc_reverse ? (COLUMNS - 1 - x) : x;
			bool pixel = BIT(row[column], line & 7);
			if (m_all_on)
				pixel = true;
			if (m_inverse)
				pixel = !pixel;
			if (!m_display_on || m_reset)
				pixel = false;
			dst[x] = pixel ? 1 : 0;
		}
	}
	return 0;
}
