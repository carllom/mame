// license:BSD-3-Clause
// copyright-holders:Carl Lom
/**********************************************************************

    Toshiba T6963 LCD controller

**********************************************************************/

#pragma once

#ifndef __T6963__
#define __T6963__

#include "emu.h"



///*************************************************************************
//  INTERFACE CONFIGURATION MACROS
///*************************************************************************

#define MCFG_T6963_ADD(_tag, _screen_tag, _sx, _sy) \
	MCFG_DEVICE_ADD(_tag, T6963, 0) \
	MCFG_VIDEO_SET_SCREEN(_screen_tag) \
	t6963_device::static_set_offsets(*device, _sx, _sy);



///*************************************************************************
//  TYPE DEFINITIONS
///*************************************************************************

// ======================> t6963_device

class t6963_device :  public device_t,
						public device_video_interface
{
public:
	// construction/destruction
	t6963_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);

	// inline configuration helpers
	static void static_set_offsets(device_t &device, int sx, int sy);

	UINT32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);

	DECLARE_READ8_MEMBER( status_r );
	DECLARE_WRITE8_MEMBER( control_w );

	DECLARE_READ8_MEMBER( data_r );
	DECLARE_WRITE8_MEMBER( data_w );

protected:
	// device-level overrides
	virtual void device_start();
	virtual void device_reset();

private:
	inline void count_up_or_down();

	UINT8 m_ram[64*1024];             // max 64k display RAM

	UINT8 m_status;                 // status register
	UINT8 m_output;                 // output register

	UINT8 rebuilt;

	int ndata;
	UINT8 m_data[2];
	UINT8 m_cmd;

	int curs_x; // cursor pointer X
	int curs_y; // cursor pointer Y
	int off;    // Offset register
	int addr;   // Address pointer

	int taddr;  // Text home address
	int tcols;  // Text area
	int gaddr;  // Graphic home address
	int gcols;  // Graphic area

	int mode;
	int dispmode;
	int cursptrn;


	//int m_cs2;                      // chip select
	//int m_page;                     // display start page
	//int m_x;                        // X address
	//int m_y;                        // Y address

	int m_sx; //TODO: remove?
	int m_sy; //TODO: remove?
};


// device type definition
extern const device_type T6963;



#endif
