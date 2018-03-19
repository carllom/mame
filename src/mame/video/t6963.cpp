// license:BSD-3-Clause
// copyright-holders:Carl Lom
/**********************************************************************

    HD44102 Dot Matrix Liquid Crystal Graphic Display Column Driver emulation

**********************************************************************/

#include "emu.h"
#include "video/t6963.h"



//**************************************************************************
//  MACROS / CONSTANTS
//**************************************************************************

#define LOG 1

#define CONTROL_SET_CURSOR_POS 0x21
#define CONTROL_SET_OFFSET_REG 0x22
#define CONTROL_SET_ADDRESS    0x24
#define CONTROL_SET_TEXT_HOME  0x40
#define CONTROL_SET_TEXT_AREA  0x41
#define CONTROL_SET_GFX_HOME   0x42
#define CONTROL_SET_GFX_AREA   0x43
#define CONTROL_SET_AUTOWRITE  0xB0
#define CONTROL_SET_AUTOREAD   0xB1
#define CONTROL_AUTORESET      0xB2
#define CONTROL_WRITE_INCR     0xC0
#define CONTROL_READ_INCR      0xC1
#define CONTROL_WRITE_DECR     0xC2
#define CONTROL_READ_DECR      0xC3
#define CONTROL_WRITE          0xC4
#define CONTROL_READ           0xC5
#define CONTROL_SCREEN_PEEK    0xE0
#define CONTROL_SCREEN_COPY    0xE8

#define STATUS_EXEC   0x01
#define STATUS_DATARW 0x02
#define STATUS_AUTORD 0x04
#define STATUS_AUTOWR 0x08
#define STATUS_OPER   0x20
#define STATUS_ERROR  0x40
#define STATUS_BLINK  0x80

#define STATUS_AUTOMODE (m_status & 0x0C)

//#define CGRAM_DATA(code,line) (m_ram[off | (code << 3) | line])

//#define BLIT_OR  ((mode & 0x3)==0)
//#define BLIT_XOR ((mode & 0x3)==1)
//#define BLIT_AND ((mode & 0x3)==3)

UINT8 onehot[] = { 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };

// device type definition
const device_type T6963 = &device_creator<t6963_device>;


//**************************************************************************
//  INLINE HELPERS
//**************************************************************************

//-------------------------------------------------
//  count_up_or_down -
//-------------------------------------------------

//inline void hd44102_device::count_up_or_down()
//{
//	if (m_status & STATUS_COUNT_UP)
//	{
//		if (++m_y > 49) m_y = 0;
//	}
//	else
//	{
//		if (--m_y < 0) m_y = 49;
//	}
//}



//**************************************************************************
//  LIVE DEVICE
//**************************************************************************

//-------------------------------------------------
//  t6963_device - constructor
//-------------------------------------------------

t6963_device::t6963_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: device_t(mconfig, T6963, "T6963", tag, owner, clock, "t6963", __FILE__),
		device_video_interface(mconfig, *this),
		m_status(0x03),
		ndata(0)
//		m_cs2(0),
//		m_page(0),
//		m_x(0),
//		m_y(0)
{
}


//-------------------------------------------------
//  static_set_offsets - configuration helper
//-------------------------------------------------

void t6963_device::static_set_offsets(device_t &device, int sx, int sy)
{
	t6963_device &t6963 = downcast<t6963_device &>(device);
	t6963.m_sx = sx;
	t6963.m_sy = sy;
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

void t6963_device::device_start()
{
	// register for state saving
	save_item(NAME(m_ram));
	save_item(NAME(m_status));
	save_item(NAME(m_output));
//	save_item(NAME(m_cs2));
//	save_item(NAME(m_page));
//	save_item(NAME(m_x));
//	save_item(NAME(m_y));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void t6963_device::device_reset()
{
//	m_status = STATUS_DISPLAY_OFF | STATUS_COUNT_UP;
	m_status = STATUS_EXEC | STATUS_DATARW;
}


//-------------------------------------------------
//  status_r - status read
//-------------------------------------------------

READ8_MEMBER( t6963_device::status_r )
{
	return m_status;
}


//-------------------------------------------------
//  control_w - control write
//-------------------------------------------------

WRITE8_MEMBER( t6963_device::control_w )
{
	// Check device ready
	if (!((m_status & STATUS_EXEC) && (m_status & STATUS_DATARW))) return;

	// Only allow auto reset command when in auto mode
	if (STATUS_AUTOMODE && (data != CONTROL_AUTORESET))
	{
		logerror("T6963 '%s' Illegal command in auto mode (%02x)\n", tag(), data);
		return;
	}

	switch (data)
	{
	case CONTROL_SET_CURSOR_POS:
		curs_x = m_data[0] & 0x7F;
		curs_y = m_data[1] & 0x1F;

		if (LOG) logerror("T6963 '%s' Set cursor pointer (%d,%d)\n", tag(), curs_x, curs_y);
		break;

	case CONTROL_SET_OFFSET_REG:
		off = (m_data[0] & 0x1F) << 11; // Shifted to address bus position

		if (LOG) logerror("T6963 '%s' Set offset register (%d)\n", tag(), off);
		break;

	case CONTROL_SET_ADDRESS:
		addr = m_data[0] | (m_data[1] << 8);

		if (LOG) logerror("T6963 '%s' Set address pointer (%04x)\n", tag(), addr);
		break;

	case CONTROL_SET_TEXT_HOME:
		taddr = m_data[0] | (m_data[1] << 8);

		if (LOG) logerror("T6963 '%s' Set text home address (%04x)\n", tag(), taddr);
		break;

	case CONTROL_SET_TEXT_AREA:
		tcols = m_data[0];

		if (LOG) logerror("T6963 '%s' Set text area (%04x)\n", tag(), tcols);
		break;

	case CONTROL_SET_GFX_HOME:
		gaddr = m_data[0] | (m_data[1] << 8);

		if (LOG) logerror("T6963 '%s' Set graphic home address (%04x)\n", tag(), gaddr);
		break;

	case CONTROL_SET_GFX_AREA:
		gcols = m_data[0];

		if (LOG) logerror("T6963 '%s' Set graphic area (%04x)\n", tag(), gcols);
		break;

	case CONTROL_SET_AUTOWRITE:
		m_status |= STATUS_AUTOWR;

		if (LOG) logerror("T6963 '%s' Set data auto write\n", tag());
		break;

	case CONTROL_SET_AUTOREAD:
		m_status |= STATUS_AUTORD;

		if (LOG) logerror("T6963 '%s' Set data auto read\n", tag());
		break;

	case CONTROL_AUTORESET:
		m_status &= ~(STATUS_AUTOWR | STATUS_AUTORD); // Reset auto bits

		if (LOG) logerror("T6963 '%s' Auto reset\n", tag());
		break;

	case CONTROL_SCREEN_PEEK:
		// Unimplemented
		logerror("T6963 '%s' Screen peek unimplemented\n", tag());
		break;

	case CONTROL_SCREEN_COPY:
		// Unimplemented
		logerror("T6963 '%s' Screen copy unimplemented\n", tag());
		break;

	default:
		if ((data & 0xF0) == 0x80) //Mode set
		{
			mode = data & 0x0F;
			if (LOG) logerror("T6963 '%s' Set mode (%01x)\n", tag(), mode);
		}
		else if ((data & 0xF0) == 0x90) // Display mode set
		{
			dispmode = data & 0x0F;
			if (LOG) logerror("T6963 '%s' Set display mode (%01x)\n", tag(), dispmode);
		}
		else if ((data & 0xF0) == 0xA0) // Cursor pattern
		{
			cursptrn = data & 0x0F;
			if (LOG) logerror("T6963 '%s' Cursor pattern select (%01x)\n", tag(), cursptrn);
		}
		else if ((data & 0xF0) == 0xC0) // Data R/W with incr/decr
		{
			m_cmd = data;
		}
		else if ((data & 0xF0) == 0xF0) // Bit/pixel set
		{
			if (data & 0x08)
			{
				m_ram[addr] |= onehot[data & 0x7]; // Set bit
			}
			else
			{
				m_ram[addr] &= ~onehot[data & 0x7]; // Clear bit
			}
		}
		else
		{
			logerror("T6963 '%s' Unexpected command (%02x)\n", tag(), data);
		}
	}

	ndata = 0; // reset data counter if command received
}


//-------------------------------------------------
//  data_r - data read
//-------------------------------------------------

READ8_MEMBER( t6963_device::data_r )
{
	UINT8 data = m_output;

	if (m_status & STATUS_AUTORD)
	{
		data = m_ram[addr++];
	}
	else if ((m_cmd & 0xF0) == 0xC0) // Data read/write
	{
		switch (m_cmd) {
		case CONTROL_READ_INCR:
			if (LOG) logerror("T6963 '%s' Read data with increment (%02x @ $%04x)\n", tag(), data, addr);
			data = m_ram[addr++];
			break;

		case CONTROL_READ_DECR:
			if (LOG) logerror("T6963 '%s' Read data with decrement (%02x @ $%04x)\n", tag(), data, addr);

			data = m_ram[addr--];
			break;

		case CONTROL_READ:
			if (LOG) logerror("T6963 '%s' Read data (%02x @ $%04x)\n", tag(), data, addr);

			data = m_ram[addr];
			break;

		default:
			logerror("T6963 '%s' Unexpected r/w command for data read (%02x)\n", tag(), m_cmd);
		}
		m_cmd = 0;
	}

	return data;
}


//-------------------------------------------------
//  data_w - data write
//-------------------------------------------------

WRITE8_MEMBER( t6963_device::data_w )
{
	if (m_status & STATUS_AUTOWR)
	{
		m_ram[addr++] = data;
		if (LOG && (data != 0)) logerror("T6963 '%s' Auto write data ($%04x=%02x)\n", tag(), addr, data);
	}
	else if ((m_cmd & 0xF0) == 0xC0) // Data read/write
	{
		switch (m_cmd) {
		case CONTROL_WRITE_INCR:
			if (LOG) logerror("T6963 '%s' Write data with increment ($%04x=%02x)\n", tag(), addr, data);

			m_ram[addr++] = data;
			break;

		case CONTROL_WRITE_DECR:
			if (LOG) logerror("T6963 '%s' Write data with decrement ($%04x=%02x)\n", tag(), addr, data);

			m_ram[addr--] = data;
			break;

		case CONTROL_WRITE:
			if (LOG) logerror("T6963 '%s' Write data ($%04x=%02x)\n", tag(), addr, data);

			m_ram[addr] = data;
			break;

		default:
			logerror("T6963 '%s' Unexpected r/w command for data write (%02x)\n", tag(), m_cmd);
		}
		m_cmd = 0;
	}
	else if (ndata < 2)
	{
		m_data[ndata++] = data;
	}
}


//-------------------------------------------------
//  update_screen - update screen
//-------------------------------------------------

UINT32 t6963_device::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
//	for (int y = 0; y < 50; y++)
//	{
//		int z = m_page << 3;
//
//		for (int x = 0; x < 32; x++)
//		{
//			UINT8 data = m_ram[z / 8][y];
//
//			int sy = m_sy + z;
//			int sx = m_sx + y;
//
//			if (cliprect.contains(sx, sy))
//			{
//				int color = (m_status & STATUS_DISPLAY_OFF) ? 0 : BIT(data, z % 8);
//
//				bitmap.pix16(sy, sx) = color;
//			}
//
//			z++;
//			z %= 32;
//		}
//	}
	return 0;
}
