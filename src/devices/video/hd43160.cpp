// license:BSD-3-Clause
// copyright-holders:Sandro Ronco
/***************************************************************************

        Hitachi HD44780 LCD controller

        TODO:
        - dump internal CGROM
        - emulate osc pin, determine video timings and busy flag duration from it

***************************************************************************/

#include "emu.h"
#include "video/hd43160.h"

#define LOG          1

//**************************************************************************
//  DEVICE DEFINITIONS
//**************************************************************************

const device_type HD43160 = &device_creator<hd43160_device>;

ROM_START(hd43160_char)
	ROM_REGION(0x1000, "cgrom", 0)
	ROM_LOAD("hd43160_char.rom", 0x0100, 0x240, BAD_DUMP CRC(D10CBBB6)) // from page 6 of the HD43160 datasheet
ROM_END


//-------------------------------------------------
//  hd44780_device - constructor
//-------------------------------------------------

hd43160_device::hd43160_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock) :
	device_t(mconfig, HD43160, "HD43160", tag, owner, clock, "hd43160_char", __FILE__)
{
}

hd43160_device::hd43160_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source) :
	device_t(mconfig, type, name, tag, owner, clock, shortname, source)
{
}

const rom_entry *hd43160_device::device_rom_region() const
{
	return ROM_NAME(hd43160_char);
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void hd43160_device::device_start()
{
	m_cgrom = memregion("cgrom")->base();

	// state saving
	save_item(NAME(m_ac));
	save_item(NAME(m_display_on));
	save_item(NAME(m_cursor_on));
	save_item(NAME(m_char_size));
}

//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void hd43160_device::device_reset()
{
	memset(m_ram, 0x20, sizeof(m_ram)); // filled with SPACE char

	m_ac         = 0;
	m_display_on = true;
	m_cursor_on  = false;
	m_char_size  = 8;
}


//**************************************************************************
//  HELPERS
//**************************************************************************

inline void hd43160_device::pixel_update(bitmap_ind16 &bitmap, UINT8 line, UINT8 pos, UINT8 y, UINT8 x, int state)
{
	//if (m_pixel_update_func != NULL)
	//{
	//	m_pixel_update_func(*this, bitmap, line, pos, y, x, state);
	//}
	//else
	//{
		UINT8 line_height = (m_char_size == 8) ? m_char_size : m_char_size + 1;

		if (m_lines <= 2)
		{
			if (pos < m_chars)
				bitmap.pix16(line * (line_height + 1) + y, pos * 6 + x) = state;
		}
		else
		{
			fatalerror("%s: use a custom callback for this LCD configuration (%d x %d)\n", tag(), m_lines, m_chars);
		}
	//}
}


//**************************************************************************
//  device interface
//**************************************************************************

const UINT8 *hd43160_device::render()
{
	memset(m_render_buf, 0, sizeof(m_render_buf));

	if (m_display_on)
	{
		for (int line = 0; line < m_lines; line++)
		{
			for (int pos = 0; pos < m_chars; pos++)
			{
				UINT16 char_pos = line * 40 + (pos % m_chars);

				int char_base = 0;
				if (m_ram[char_pos] > 0x1F)
				{
					// draw CGROM characters
					char_base = m_ram[char_pos] * 0x8;
				}

				UINT8 *dest = m_render_buf + 16 * (line * m_chars + pos);
				memcpy (dest, m_cgrom + char_base, m_char_size);

				if (char_pos == m_ac)
				{
					// draw the cursor
					if (m_cursor_on)
						dest[m_char_size - 1] = 0x1f;
				}
			}
		}
	}

	return m_render_buf;
}

UINT32 hd43160_device::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	bitmap.fill(0, cliprect);
	const UINT8 *img = render();

	for (int line = 0; line < m_lines; line++)
	{
		for (int pos = 0; pos < m_chars; pos++)
		{
			const UINT8 *src = img + 16 * (line * m_chars + pos);
			for (int y = 0; y < m_char_size; y++)
				for (int x = 0; x < 5; x++)
					pixel_update(bitmap, line, pos, y, x, BIT(src[y], 4 - x));
		}
	}

	return 0;
}


READ8_MEMBER(hd43160_device::read)
{
	switch (offset & 0x01)
	{
	case 0: return control_read(space, 0);
	case 1: return data_read(space, 0);
	}

	return 0;
}

WRITE8_MEMBER(hd43160_device::write)
{
	switch (offset & 0x01)
	{
	case 0: control_write(space, 0, data);  break;
	case 1: data_write(space, 0, data);     break;
	}
}

WRITE8_MEMBER(hd43160_device::control_write)
{
	if (data == 1) {
		// clear display
		if (LOG) logerror("HD43160 '%s': clear display\n", tag());

		m_ac = 0;
		memset(m_ram, 0x20, sizeof(m_ram));
	}
	else if (data == 2) {
		// cursor home
		if (LOG) logerror("HD43160 '%s': cursor home\n", tag());
		if (m_lines > 1 && m_ac > m_chars) {
			m_ac = m_chars; // home line 2;
		}
		else {
			m_ac = 0;
		}
	}
	else if ((data & 0xFE) == 4) {
		// Cursor on/off
		m_cursor_on = (~data) & 1;
		if (LOG) logerror("HD43160 '%s': cursor %s\n", tag(), m_cursor_on ? "on" : "off");
	}
	else if (data & 0x80) {
		// Set cursor
		if (data & 0x40)
			m_ac = (data & 0x3F) + ((data & 0x40) ? 40 : 0);
		if (LOG) logerror("HD43160 '%s': cursor position: %d\n", tag(), m_ac);
	}
}

READ8_MEMBER(hd43160_device::control_read)
{
	return 0; // TODO: correct display timing w. busy signal on D7
}

WRITE8_MEMBER(hd43160_device::data_write)
{
	if (LOG) logerror("HD43160 '%s': RAM write %02x %02x '%c'\n", tag(), m_ac, data, isprint(data) ? data : '.');

	m_ram[m_ac] = data;
	m_ac++;
	//if (m_ac > m_chars)
	//m_ac = 40;
	m_ac %= 80;
}

READ8_MEMBER(hd43160_device::data_read)
{
	return 0; // TODO: correct display timing w. busy signal on D7
}
