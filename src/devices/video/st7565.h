// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_VIDEO_ST7565_H
#define MAME_VIDEO_ST7565_H

#pragma once

class st7565_device : public device_t
{
public:
	st7565_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	// A0 selects command (0) or display data (1)
	void write(offs_t offset, u8 data);
	void control_w(u8 data);
	void data_w(u8 data);
	void reset_w(int state);

	// pixels by segment (x, 0-131) and common (y, 0-64) driver
	u32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, rectangle const &cliprect);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	static constexpr u32 COLUMNS = 132;
	static constexpr u32 PAGES = 9;

	u8 m_ram[PAGES * COLUMNS];
	u8 m_page;
	u8 m_column;
	u8 m_rmw_column;
	u8 m_start_line;
	u8 m_pending;       // command waiting for its second byte
	u8 m_volume;
	u8 m_power;
	bool m_display_on;
	bool m_adc_reverse;
	bool m_com_reverse;
	bool m_inverse;
	bool m_all_on;
	bool m_rmw;
	bool m_reset;
};

DECLARE_DEVICE_TYPE(ST7565, st7565_device)

#endif // MAME_VIDEO_ST7565_H
