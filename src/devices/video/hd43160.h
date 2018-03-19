#pragma once

#ifndef __HD43160_H__
#define __HD43160_H__

#include "video/hd44780.h"

#define MCFG_HD43160_ADD( _tag ) \
	MCFG_DEVICE_ADD( _tag, HD43160, 0 )

#define MCFG_HD43160_LCD_SIZE(_lines, _chars) \
	hd43160_device::static_set_lcd_size(*device, _lines, _chars);

class hd43160_device :  public device_t
{
public:
	// construction/destruction
	hd43160_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);
	hd43160_device(const machine_config &mconfig, device_type type, const char *name, const char *tag, device_t *owner, UINT32 clock, const char *shortname, const char *source);

	// static configuration helpers
	static void static_set_lcd_size(device_t &device, int _lines, int _chars) { hd43160_device &dev=downcast<hd43160_device &>(device); dev.m_lines = _lines; dev.m_chars = _chars; }

	// device interface
	virtual DECLARE_WRITE8_MEMBER(write);
	virtual DECLARE_READ8_MEMBER(read);
	virtual DECLARE_WRITE8_MEMBER(control_write);
	virtual DECLARE_READ8_MEMBER(control_read);
	virtual DECLARE_WRITE8_MEMBER(data_write);
	virtual DECLARE_READ8_MEMBER(data_read);

	const UINT8 *render();
	virtual UINT32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

protected:
	virtual void device_start();
	virtual void device_reset();
	// optional information overrides
	virtual const rom_entry *device_rom_region() const;
private:
	void pixel_update(bitmap_ind16 &bitmap, UINT8 line, UINT8 pos, UINT8 y, UINT8 x, int state);

	UINT8       m_ram[80];
	int         m_lines;          // display height in lines
	int         m_chars;          // display width in characters
	bool        m_cursor_on;      // cursor on/off
	bool        m_display_on;     // display on/off
	UINT8       m_char_size;      // char size 5x8 or 5x10
	int         m_ac;             // address counter
	UINT8 *     m_cgrom;          // internal chargen ROM

	UINT8       m_render_buf[80 * 16];
};

// device type definition
extern const device_type HD43160;

#endif
