#pragma once

#ifndef __HD43160_H__
#define __HD43160_H__

#include "video/hd44780.h"

class hd43160_device :  public device_t
{
public:
	// construction/destruction
	hd43160_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// static configuration helpers
	void set_lcd_size(int lines, int chars) { m_lines = lines; m_chars = chars; }

	// device interface
	virtual void write(offs_t offset, uint8_t data);
	virtual uint8_t read(offs_t offset);
	virtual void control_write(uint8_t data);
	virtual uint8_t control_read();
	virtual void data_write(uint8_t data);
	virtual uint8_t data_read();

	const uint8_t *render();
	virtual uint32_t screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

protected:
	virtual void device_start();
	virtual void device_reset();
	// optional information overrides
	virtual const tiny_rom_entry *device_rom_region() const;
private:
	void pixel_update(bitmap_ind16 &bitmap, uint8_t line, uint8_t pos, uint8_t y, uint8_t x, int state);

	uint8_t     m_ram[80];
	int         m_lines;          // display height in lines
	int         m_chars;          // display width in characters
	bool        m_cursor_on;      // cursor on/off
	bool        m_display_on;     // display on/off
	uint8_t     m_char_size;      // char size 5x8 or 5x10
	int         m_ac;             // address counter
	uint8_t *   m_cgrom;          // internal chargen ROM

	uint8_t     m_render_buf[80 * 16];
};

// device type definition
DECLARE_DEVICE_TYPE(HD43160, hd43160_device);

#endif
