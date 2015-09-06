// license:BSD-3-Clause
// copyright-holders:Miodrag Milanovic
/***************************************************************************

        Partner driver by Miodrag Milanovic

        09/06/2008 Preliminary driver.

****************************************************************************/


#include "emu.h"
#include "cpu/i8085/i8085.h"
#include "sound/wave.h"
#include "machine/i8255.h"
#include "imagedev/cassette.h"
#include "formats/smx_dsk.h"
#include "formats/rk_cas.h"
#include "includes/radio86.h"
#include "machine/ram.h"
#include "includes/partner.h"

/* Address maps */
static ADDRESS_MAP_START(partner_mem, AS_PROGRAM, 8, partner_state )
	AM_RANGE( 0x0000, 0x07ff ) AM_RAMBANK("bank1")
	AM_RANGE( 0x0800, 0x3fff ) AM_RAMBANK("bank2")
	AM_RANGE( 0x4000, 0x5fff ) AM_RAMBANK("bank3")
	AM_RANGE( 0x6000, 0x7fff ) AM_RAMBANK("bank4")
	AM_RANGE( 0x8000, 0x9fff ) AM_RAMBANK("bank5")
	AM_RANGE( 0xa000, 0xb7ff ) AM_RAMBANK("bank6")
	AM_RANGE( 0xb800, 0xbfff ) AM_RAMBANK("bank7")
	AM_RANGE( 0xc000, 0xc7ff ) AM_RAMBANK("bank8")
	AM_RANGE( 0xc800, 0xcfff ) AM_RAMBANK("bank9")
	AM_RANGE( 0xd000, 0xd7ff ) AM_RAMBANK("bank10")
	AM_RANGE( 0xd800, 0xd8ff ) AM_DEVREADWRITE("i8275", i8275_device, read, write)  // video
	AM_RANGE( 0xd900, 0xd9ff ) AM_DEVREADWRITE("ppi8255_1", i8255_device, read, write)
	AM_RANGE( 0xda00, 0xdaff ) AM_WRITE(partner_mem_page_w)
	AM_RANGE( 0xdb00, 0xdbff ) AM_DEVWRITE("dma8257", i8257_device, write)    // DMA
	AM_RANGE( 0xdc00, 0xddff ) AM_RAMBANK("bank11")
	AM_RANGE( 0xde00, 0xdeff ) AM_WRITE(partner_win_memory_page_w)
	AM_RANGE( 0xe000, 0xe7ff ) AM_RAMBANK("bank12")
	AM_RANGE( 0xe800, 0xffff ) AM_RAMBANK("bank13")
ADDRESS_MAP_END

/* Input ports */
static INPUT_PORTS_START( partner )
	PORT_START("LINE0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_HOME) PORT_CHAR(UCHAR_MAMEKEY(HOME))
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_INSERT) PORT_CHAR(UCHAR_MAMEKEY(INSERT))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ESC) PORT_CHAR(UCHAR_MAMEKEY(ESC))
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F1) PORT_CHAR(UCHAR_MAMEKEY(F1))
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F2) PORT_CHAR(UCHAR_MAMEKEY(F2))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F3) PORT_CHAR(UCHAR_MAMEKEY(F3))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F4) PORT_CHAR(UCHAR_MAMEKEY(F4))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F5) PORT_CHAR(UCHAR_MAMEKEY(F5))

	PORT_START("LINE1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TAB) PORT_CHAR('\t')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_END) PORT_CHAR(UCHAR_MAMEKEY(END))
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_ENTER) PORT_CHAR(13)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Back") PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_LEFT) PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_UP) PORT_CHAR(UCHAR_MAMEKEY(UP))
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_RIGHT) PORT_CHAR(UCHAR_MAMEKEY(RIGHT))
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_DOWN) PORT_CHAR(UCHAR_MAMEKEY(DOWN))

	PORT_START("LINE2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_1) PORT_CHAR('1') PORT_CHAR('!')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_2) PORT_CHAR('2') PORT_CHAR('"')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_3) PORT_CHAR('3') PORT_CHAR('#')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_4) PORT_CHAR('4') PORT_CHAR('\xA4')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_5) PORT_CHAR('5') PORT_CHAR('%')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_6) PORT_CHAR('6') PORT_CHAR('&')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_7) PORT_CHAR('7') PORT_CHAR('\'')

	PORT_START("LINE3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_8) PORT_CHAR('8') PORT_CHAR('(')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_9) PORT_CHAR('9') PORT_CHAR(')')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_EQUALS) PORT_CHAR(':') PORT_CHAR('*')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COLON) PORT_CHAR(';') PORT_CHAR('+')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_COMMA) PORT_CHAR(',') PORT_CHAR('<')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS) PORT_CHAR('-') PORT_CHAR('=')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.') PORT_CHAR('>')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?')

	PORT_START("LINE4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_TILDE) PORT_CHAR(' ') PORT_CHAR('@') // without shift, this should be a Russian character, but we don't have yet these characters...
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_B) PORT_CHAR('B')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_C) PORT_CHAR('C')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_E) PORT_CHAR('E')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_G) PORT_CHAR('G')

	PORT_START("LINE5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_I) PORT_CHAR('I')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_J) PORT_CHAR('J')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_K) PORT_CHAR('K')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_L) PORT_CHAR('L')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_M) PORT_CHAR('M')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_N) PORT_CHAR('N')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_O) PORT_CHAR('O')

	PORT_START("LINE6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_P) PORT_CHAR('P')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_R) PORT_CHAR('R')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_T) PORT_CHAR('T')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_U) PORT_CHAR('U')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_V) PORT_CHAR('V')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_W) PORT_CHAR('W')

	PORT_START("LINE7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_X) PORT_CHAR('X')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y) PORT_CHAR('Y')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z) PORT_CHAR('Z')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_OPENBRACE) PORT_CHAR('[')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_CLOSEBRACE) PORT_CHAR(']')
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_QUOTE) PORT_CHAR('~')
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')

	PORT_START("LINE8")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Shift") PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT)  // Apparently in partner is Shift to switch between Latin and Russian Keyboard!
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Ctrl") PORT_CODE(KEYCODE_LCONTROL) PORT_CODE(KEYCODE_RCONTROL) PORT_CHAR(UCHAR_SHIFT_1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Rus/Lat") PORT_CODE(KEYCODE_LALT) PORT_CODE(KEYCODE_RALT)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END

/* Machine driver */
FLOPPY_FORMATS_MEMBER( partner_state::floppy_formats )
	FLOPPY_SMX_FORMAT
FLOPPY_FORMATS_END

static SLOT_INTERFACE_START( partner_floppies )
	SLOT_INTERFACE( "525qd", FLOPPY_525_QD )
SLOT_INTERFACE_END

/* F4 Character Displayer */
static const gfx_layout partner_charlayout =
{
	8, 8,                   /* 8 x 8 characters */
	512,                    /* 512 characters */
	1,                  /* 1 bits per pixel */
	{ 0 },                  /* no bitplanes */
	/* x offsets */
	{ 0, 1, 2, 3, 4, 5, 6, 7 },
	/* y offsets */
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8 },
	8*8                 /* every char takes 8 bytes */
};

static GFXDECODE_START( partner )
	GFXDECODE_ENTRY( "gfx1", 0x0000, partner_charlayout, 0, 1 )
GFXDECODE_END


static MACHINE_CONFIG_START( partner, partner_state )
	/* basic machine hardware */
	MCFG_CPU_ADD("maincpu", I8080, XTAL_16MHz / 9)
	MCFG_CPU_PROGRAM_MAP(partner_mem)

	MCFG_MACHINE_RESET_OVERRIDE(partner_state, partner )

	MCFG_DEVICE_ADD("ppi8255_1", I8255, 0)
	MCFG_I8255_OUT_PORTA_CB(WRITE8(radio86_state, radio86_8255_porta_w2))
	MCFG_I8255_IN_PORTB_CB(READ8(radio86_state, radio86_8255_portb_r2))
	MCFG_I8255_IN_PORTC_CB(READ8(radio86_state, radio86_8255_portc_r2))
	MCFG_I8255_OUT_PORTC_CB(WRITE8(radio86_state, radio86_8255_portc_w2))

	MCFG_DEVICE_ADD("i8275", I8275, XTAL_16MHz / 12)
	MCFG_I8275_CHARACTER_WIDTH(6)
	MCFG_I8275_DRAW_CHARACTER_CALLBACK_OWNER(partner_state, display_pixels)
	MCFG_I8275_DRQ_CALLBACK(DEVWRITELINE("dma8257",i8257_device, dreq2_w))

	/* video hardware */
	MCFG_SCREEN_ADD("screen", RASTER)
	MCFG_SCREEN_UPDATE_DEVICE("i8275", i8275_device, screen_update)
	MCFG_SCREEN_REFRESH_RATE(50)
	MCFG_SCREEN_SIZE(78*6, 30*10)
	MCFG_SCREEN_VISIBLE_AREA(0, 78*6-1, 0, 30*10-1)

	MCFG_GFXDECODE_ADD("gfxdecode", "palette", partner)
	MCFG_PALETTE_ADD("palette", 3)
	MCFG_PALETTE_INIT_OWNER(partner_state,radio86)

	MCFG_SPEAKER_STANDARD_MONO("mono")
	MCFG_SOUND_WAVE_ADD(WAVE_TAG, "cassette")
	MCFG_SOUND_ROUTE(ALL_OUTPUTS, "mono", 0.25)

	MCFG_DEVICE_ADD("dma8257", I8257, XTAL_16MHz / 9)
	MCFG_I8257_OUT_HRQ_CB(WRITELINE(partner_state, hrq_w))
	MCFG_I8257_IN_MEMR_CB(READ8(radio86_state, memory_read_byte))
	MCFG_I8257_OUT_MEMW_CB(WRITE8(radio86_state, memory_write_byte))
	MCFG_I8257_IN_IOR_0_CB(DEVREAD8("wd1793", fd1793_t, data_r))
	MCFG_I8257_OUT_IOW_0_CB(DEVWRITE8("wd1793", fd1793_t, data_w))
	MCFG_I8257_OUT_IOW_2_CB(DEVWRITE8("i8275", i8275_device, dack_w))
	MCFG_I8257_REVERSE_RW_MODE(1)

	MCFG_CASSETTE_ADD( "cassette" )
	MCFG_CASSETTE_FORMATS(rkp_cassette_formats)
	MCFG_CASSETTE_DEFAULT_STATE(CASSETTE_STOPPED | CASSETTE_SPEAKER_ENABLED | CASSETTE_MOTOR_ENABLED)
	MCFG_CASSETTE_INTERFACE("partner_cass")

	MCFG_SOFTWARE_LIST_ADD("cass_list","partner_cass")

	MCFG_FD1793_ADD("wd1793", XTAL_16MHz / 16)
	MCFG_WD_FDC_DRQ_CALLBACK(DEVWRITELINE("dma8257", i8257_device, dreq0_w))
	MCFG_FLOPPY_DRIVE_ADD("wd1793:0", partner_floppies, "525qd", partner_state::floppy_formats)
	MCFG_FLOPPY_DRIVE_ADD("wd1793:1", partner_floppies, "525qd", partner_state::floppy_formats)

	MCFG_SOFTWARE_LIST_ADD("flop_list","partner_flop")

	/* internal ram */
	MCFG_RAM_ADD(RAM_TAG)
	MCFG_RAM_DEFAULT_SIZE("64K")
	MCFG_RAM_DEFAULT_VALUE(0x00)
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( partner )
	ROM_REGION( 0x1A000, "maincpu", ROMREGION_ERASEFF )
	ROM_LOAD( "partner.rom", 0x10000, 0x2000, CRC(be1eaa10) SHA1(f9658d8055bf434240ec020d7892ea98cb5cbb76))
	ROM_LOAD( "basic.rom",   0x12000, 0x2000, CRC(1e9be0ec) SHA1(2c431f487cffddaac8413efddfc0527ad595f03b))
	ROM_LOAD( "mcpg.rom",    0x14000, 0x0800, CRC(3401225c) SHA1(6c252393ee73ed1a53d3e583547d86ab6718a533))
	ROM_LOAD( "fdd.rom",     0x16000, 0x0800, CRC(8ca350b5) SHA1(76fc92298726fb2840f4c19d7edc860d1ed86356))
	ROM_LOAD( "font.rom",    0x18000, 0x2000, CRC(dc4f1723) SHA1(6b8d5efb403cf0aeb3fd3197a0529d23c8e2f93c))
	ROM_REGION(0x2000, "gfx1",0)
	ROM_LOAD ("partner.fnt", 0x0000, 0x2000, CRC(2705f726) SHA1(3d7b33901ef098a405d7ddad924ba9677f6a9b15))
ROM_END

/* Driver */
/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT   INIT        COMPANY   FULLNAME       FLAGS */
COMP( 1987, partner, radio86,   0,  partner,    partner, partner_state,partner, "SAM SKB VM",   "Partner-01.01",    MACHINE_NOT_WORKING)
