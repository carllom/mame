#include "machine/wd_fdc.h"
#include "cpu/mcs96/i8x9x.h"
#include "video/hd44780.h"
#include "video/tms3556.h"
#include "s330.lh"
#include "audio/sa16.h"
//#include "bus/midi/midiinport.h"
//#include "bus/midi/midioutport.h"

#include "formats/ql_dsk.h"

#include "emu.h"

#define	XTAL_14_3496MHz 14349600
#define XTAL_26_88MHz 26880000

#define LOG_INPUT 0
#define LOG_M60013_BANKSW 0
#define LOG_M60013_UNMAPPED 0
#define LOG_M60013 0


class s330_state : public driver_device
{
public:
	s330_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_lcdc(*this, "hd44780"),
		m_vdp(*this, "tms3556"),
		m_fdc(*this, "wd1772"),
		m_floppy0(*this, "wd1772:0:35dd"),
		m_sa16(*this, "sa16"),
		m_panel0(*this, "panel0"),
		m_panel1(*this, "panel1"),
		m_panel2(*this, "panel2"),
		m_panel3(*this, "panel3")
	{
		m_flpsel = 0x03;
		m_keyrowsread = 0;
	}

	required_device<cpu_device> m_maincpu;
	required_device<hd44780_device> m_lcdc;
	required_device<tms3556_device> m_vdp;
	required_device<wd1772_t> m_fdc;
	required_device<floppy_image_device> m_floppy0;
	required_device<sa16_device> m_sa16;

	// int m_mapper_state;
	// int m_seq_bank;
	// UINT8 m_seqram[0x10000];
	// UINT8 m_dosram[0x2000];
	// DECLARE_INPUT_CHANGED_MEMBER(key_stroke);
	required_ioport m_panel0;
	required_ioport m_panel1;
	required_ioport m_panel2;
	required_ioport m_panel3;
	UINT8 m_keyreg_p[4];
	UINT8 m_keyreg[4];
	DECLARE_INPUT_CHANGED_MEMBER(keyhandler);

	DECLARE_DRIVER_INIT( s330 );

	DECLARE_FLOPPY_FORMATS( floppy_formats );

	// void send_through_panel(UINT8 data);
	DECLARE_PALETTE_INIT(lcd);
	DECLARE_PALETTE_INIT(vdp);
	TIMER_DEVICE_CALLBACK_MEMBER(s330_hblank_interrupt);


	int m_keyrowsread;
	UINT8 m_d800, m_d802, m_d804;
	DECLARE_READ8_MEMBER(d800_read);
	DECLARE_WRITE8_MEMBER(d800_write);
	DECLARE_READ8_MEMBER(d802_read);
	DECLARE_WRITE8_MEMBER(d802_write);
	DECLARE_READ8_MEMBER(d804_read);
	DECLARE_WRITE8_MEMBER(d804_write);
	DECLARE_READ8_MEMBER(key_read);
	DECLARE_WRITE8_MEMBER(key_write);

	int m_flpsel;
	DECLARE_READ8_MEMBER(fdc_r);
	DECLARE_WRITE8_MEMBER(fdc_w);

	DECLARE_WRITE8_MEMBER(led_w);

	DECLARE_WRITE8_MEMBER(bank_ctrl);

	void ready0_cb(floppy_image_device *, int device);
	int load0(floppy_image_device *device);
	void unload0(floppy_image_device *device);

	DECLARE_WRITE_LINE_MEMBER(cb_intrq);
	DECLARE_WRITE_LINE_MEMBER(cb_drq);
	DECLARE_WRITE_LINE_MEMBER(cb_hld);
	DECLARE_WRITE_LINE_MEMBER(cb_enp);

	virtual void machine_reset();
	virtual void machine_start();

	// I/O gate array (M60013-0137FP)
	DECLARE_READ8_MEMBER(m60013_r);
	DECLARE_WRITE8_MEMBER(m60013_w);

	// Wave RAM interface (SA-16)
	DECLARE_READ8_MEMBER(sa16_r);
	DECLARE_WRITE8_MEMBER(sa16_w);

	// CPU I/O ports
	DECLARE_READ16_MEMBER(ad_conv_r);
	DECLARE_READ16_MEMBER(ad_adj_r);
	DECLARE_READ16_MEMBER(midi_r);
	DECLARE_WRITE16_MEMBER(midi_w);
};

DRIVER_INIT_MEMBER( s330_state, s330 )
{
}

void s330_state::machine_reset()
{
	m_maincpu->reset();
	m_fdc->reset();
	m_vdp->reset();
	m_lcdc->reset();

	m_fdc->set_floppy(NULL);

	// 60013
	m_keyrowsread = 0;
	memset(m_keyreg,0xFF,4*sizeof(UINT8));
	memset(m_keyreg_p,0xFF,4*sizeof(UINT8));
	//m_keyreg[1]=0xFB; // Left key pressed
}

void s330_state::machine_start()
{
	m_floppy0->setup_ready_cb(floppy_image_device::ready_cb(FUNC(s330_state::ready0_cb), this));
	m_floppy0->setup_load_cb(floppy_image_device::load_cb(FUNC(s330_state::load0), this));
	m_floppy0->setup_unload_cb(floppy_image_device::unload_cb(FUNC(s330_state::unload0), this));
}

static SLOT_INTERFACE_START( s330_floppies )
	SLOT_INTERFACE( "35dd", FLOPPY_35_DD )
SLOT_INTERFACE_END

FLOPPY_FORMATS_MEMBER( s330_state::floppy_formats )
	FLOPPY_QL_FORMAT
FLOPPY_FORMATS_END

PALETTE_INIT_MEMBER(s330_state, lcd)
{
	palette.set_pen_color(0, rgb_t(138, 146, 148));
	palette.set_pen_color(1, rgb_t(92, 83, 88));
}

PALETTE_INIT_MEMBER(s330_state, vdp)
{
	int i, red, green, blue;

	/* create the 8 color palette */
	for (i = 0; i < 8; i++)
	{
		red = (i & 1) ? 255 : 50;    /* red */
		green = (i & 2) ? 255 : 50;  /* green */
		blue = (i & 4) ? 255 : 50;   /* blue */
		palette.set_pen_color(i, red, green, blue);
	}
}

TIMER_DEVICE_CALLBACK_MEMBER(s330_state::s330_hblank_interrupt)
{
	m_vdp->interrupt(machine());
}


//
// The M60013 I/O gate array controls all I/O communication
// The addresses are interleaved:
// - Odd addresses are passed on to SA-16 (Wave RAM interface)
// - Even addresses are passed on to the other chips
//
READ8_MEMBER(s330_state::m60013_r) //UINT8 name(address_space &space, offs_t offset, UINT8 mem_mask)
{
	if (offset & 1) {
		// Odd addresses are passed on to SA-16. Offset is normalized to target address range.
		return m_sa16->portb_r(space, offset/2, mem_mask);
	} else {
		// Even address I/O - unmapped
		if (LOG_M60013_UNMAPPED)
			logerror("%s: M60013 - Unmapped I/O read from address %04x\n", space.machine().describe_context(), offset);
		return 0;
	}
}

//
// The M60013 I/O gate array controls all I/O communication
// The addresses are interleaved:
// - Odd addresses are passed on to SA-16 (Wave RAM interface)
// - Even addresses are passed on to the other chips
//
WRITE8_MEMBER(s330_state::m60013_w) //void name(address_space &space, offs_t offset, UINT8 data, UINT8 mem_mask)
{
	if (offset & 1) {
		// Odd addresses are passed on to SA-16. Offset is normalized to target address range.
		m_sa16->portb_w(space, offset/2, data, mem_mask);
	} else {
		// Even address I/O - unmapped
		if (LOG_M60013_UNMAPPED)
			logerror("%s: M60013 - Unmapped I/O write to address %04x, data %02x\n", space.machine().describe_context(), offset, data);
	}
}

READ8_MEMBER( s330_state::d800_read )
{
	logerror("%s: $D800 read\n", space.machine().describe_context());
	return m_d800;
}
WRITE8_MEMBER( s330_state::d800_write )
{
	logerror("%s: $D800 write (data=%02x)\n", space.machine().describe_context(), data);
	m_d800 = data;
}
READ8_MEMBER( s330_state::d802_read )
{
	logerror("%s: $D802 read\n", space.machine().describe_context());
	return m_d802;
}
WRITE8_MEMBER( s330_state::d802_write )
{
	logerror("%s: $D802 write (data=%02x)\n", space.machine().describe_context(), data);
	m_d802 = data;
}
READ8_MEMBER( s330_state::d804_read )
{
	logerror("%s: $D804 read\n", space.machine().describe_context());
	return m_d804;
}
WRITE8_MEMBER( s330_state::d804_write )
{
	logerror("%s: $D804 write (data=%02x)\n", space.machine().describe_context(), data);
	m_d804 = data;
}

//
// Panel keys
//

READ8_MEMBER( s330_state::key_read )
{
	UINT8 value, oldvalue;
	value = m_keyreg[m_keyrowsread];
	if (LOG_M60013) {
		oldvalue = m_keyreg_p[m_keyrowsread];
		if (value != oldvalue) // Edge triggered
			logerror("KEYPORT Read: row %d, value %02x\n", m_keyrowsread, value);
		m_keyreg_p[m_keyrowsread] = m_keyreg[m_keyrowsread]; // old = new
	}
	m_keyrowsread = (m_keyrowsread + 1) % 4;
	return value;
}

WRITE8_MEMBER( s330_state::key_write )
{
	if (data && LOG_M60013)
		logerror("KEYPORT Write: %02x\n", data);
	m_keyrowsread = 0; // Init output counter
}

INPUT_CHANGED_MEMBER(s330_state::keyhandler) {
	int _key = (int)(FPTR)param;

	if (oldval == 0 && newval == 1)
	{
		m_keyreg[(_key>>8)&3] |= _key&0xFF;
		if (LOG_INPUT)
			logerror("keyrelease %d => reg[%d]=%02x\n",_key, (_key>>8)&3, m_keyreg[(_key>>8)&3]);
	}
	else if (oldval == 1 && newval == 0)
	{
		m_keyreg[(_key>>8)&3] &= ~(_key&0xFF);
		if (LOG_INPUT)
			logerror("keypress %d => reg[%d]=%02x\n",_key, (_key>>8)&3, m_keyreg[(_key>>8)&3]);
	}
}

//
// bit0: Disk ready (from shugart bus)
// bit1: Disk change (from shugart bus)
// bit2: Interrupt request (from wd1772)
// bit3: Data request (from wd1772)
//
READ8_MEMBER( s330_state::fdc_r )
{
	if (m_fdc->intrq_r())
		m_flpsel |= 0x4;
	else
		m_flpsel &= 0xFB;
	if (m_fdc->drq_r())
		m_flpsel |= 0x8;
	else
		m_flpsel &= 0xF7;
	return m_flpsel & 0x0F;
}

//
// bit0: Side
// bit2,3: Drive0,1 (only 1 available?!)
//
WRITE8_MEMBER( s330_state::fdc_w )
{
	if (BIT(data, 2))
	{
		m_fdc->set_floppy(m_floppy0);
		m_floppy0->ss_w(BIT(data, 0)); // Side
	}
	else if (BIT(data, 3))
	{
		logerror("Selected non-existing floppy1!\n");
		m_fdc->set_floppy(NULL);
	}
}

void s330_state::ready0_cb(floppy_image_device *device, int state)
{
	m_flpsel &= 0xFD;
	m_flpsel |= (state & 1) << 1;
}

int s330_state::load0(floppy_image_device *device)
{
	m_flpsel &= 0xFE;
	return IMAGE_INIT_PASS;
}

void s330_state::unload0(floppy_image_device *device)
{
	m_flpsel |= 0x01;
}

WRITE8_MEMBER( s330_state::led_w )
{
	logerror("wrote %02x to led\n", data);
}

WRITE8_MEMBER( s330_state::bank_ctrl )
{
	int lobank = (data >> 3) & 0x1F;
	int hibank = data & 3;

	if (lobank == 0) {
		membank("8kbank")->set_base(memregion("bootrom")->base() + 256);
	} else {
		membank("8kbank")->set_base(memregion("bankram")->base() + 256 + (lobank -1) * (8*1024));
	}
	membank("16kbank")->set_base(memregion("bankram")->base() + (64*1024) + hibank * (16*1024));

	if (LOG_M60013_BANKSW)
		logerror("%s BANKSWITCH: lobank=%d, hibank=%d\n", space.machine().describe_context(),lobank,hibank);
}


WRITE_LINE_MEMBER(s330_state::cb_intrq)
{
	logerror("FDC INTRQ=%d\n",state);
	m_flpsel &= 0xFB;
	m_flpsel |= (state & 1) << 2;
}

WRITE_LINE_MEMBER(s330_state::cb_drq)
{
	m_flpsel &= 0xF7;
	m_flpsel |= (state & 1) << 3;
}

WRITE_LINE_MEMBER(s330_state::cb_hld)
{
	logerror("cb_hld %d\n", state);
}

WRITE_LINE_MEMBER(s330_state::cb_enp)
{
	logerror("cb_enp %d\n", state);
}

static ADDRESS_MAP_START( s330_map, AS_PROGRAM, 8, s330_state )

	AM_RANGE(0x0100, 0x1FFD) AM_RAMBANK("8kbank")

	AM_RANGE(0x0100, 0x1FFD) AM_ROM AM_REGION("bootrom", 0x0100)
	AM_RANGE(0x2000, 0x3FFF) AM_ROM AM_REGION("bootrom", 0x2000)

	//AM_RANGE(0x4000, 0x7FFF) AM_RAMBANK("16kbank")
	//AM_RANGE(0x8000, 0xBFFF) AM_UNMAP
	AM_RANGE(0x4000, 0x7FFF) AM_RAM
	AM_RANGE(0x8000, 0xBFFF) AM_RAMBANK("16kbank")



	//
	// I/O area
	// The I/O controller maps odd addresses to SR-16 (Wave RAM interface)
	// Even addresses are mapped to other chips/ports
	//

	//AM_RANGE(0xC000, 0xC000) AM_WRITE(rstbits_w)

	AM_RANGE(0xC200, 0xC200) AM_READWRITE(fdc_r, fdc_w)

	AM_RANGE(0xC300, 0xC300) AM_DEVREADWRITE("hd44780", hd44780_device, control_read, control_write)
	AM_RANGE(0xC302, 0xC302) AM_DEVREADWRITE("hd44780", hd44780_device, data_read, data_write)

	AM_RANGE(0xC600, 0xC600) AM_WRITE(bank_ctrl)

	AM_RANGE(0xC800, 0xC800) AM_DEVREADWRITE("wd1772", wd1772_t, status_r, cmd_w )
	AM_RANGE(0xC802, 0xC802) AM_DEVREADWRITE("wd1772", wd1772_t, track_r, track_w )
	AM_RANGE(0xC804, 0xC804) AM_DEVREADWRITE("wd1772", wd1772_t, sector_r, sector_w )
	AM_RANGE(0xC806, 0xC806) AM_DEVREADWRITE("wd1772", wd1772_t, data_r, data_w )

	AM_RANGE(0xD000, 0xD000) AM_DEVREAD("tms3556", tms3556_device, vram_r)
	AM_RANGE(0xD002, 0xD002) AM_DEVREADWRITE("tms3556", tms3556_device, vram_r, vram_w)
	AM_RANGE(0xD004, 0xD004) AM_DEVREADWRITE("tms3556", tms3556_device, reg_r, reg_w)

	AM_RANGE(0xD800, 0xD800) AM_READWRITE(d800_read, d800_write)
	AM_RANGE(0xD802, 0xD802) AM_READWRITE(d802_read, d802_write)
	AM_RANGE(0xD804, 0xD804) AM_READWRITE(d804_read, d804_write)
	AM_RANGE(0xD806, 0xD806) AM_READWRITE(key_read, key_write)

	AM_RANGE(0xF00C, 0xF00C) AM_WRITE(led_w)

	// Catch for odd/unmapped addresses
	AM_RANGE(0xC000, 0xFFFF) AM_READWRITE(m60013_r, m60013_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( cpu_io_map, AS_IO, 16, s330_state )
	AM_RANGE(i8x9x_device::A4, i8x9x_device::A4) AM_READ(ad_adj_r)
	AM_RANGE(i8x9x_device::A7, i8x9x_device::A7) AM_READ(ad_conv_r)
	AM_RANGE(i8x9x_device::SERIAL, i8x9x_device::SERIAL) AM_READWRITE(midi_r, midi_w)
ADDRESS_MAP_END

static ADDRESS_MAP_START( sa16_waveram, AS_DATA, 16, s330_state )
	AM_RANGE(0x00000, 0xFFFFF) AM_RAM AM_REGION("waveram", 0)
ADDRESS_MAP_END


READ16_MEMBER(s330_state::ad_conv_r) {
	logerror("A/D read\n");
	return 511; // half of 10 bits
}
READ16_MEMBER(s330_state::ad_adj_r) {
	return 511;
}
READ16_MEMBER(s330_state::midi_r) {
	logerror("MIDI read\n");
	return 0;
}
WRITE16_MEMBER(s330_state::midi_w) {
	logerror("MIDI write %02x\n", data);
}


static MACHINE_CONFIG_START( s330, s330_state )
	MCFG_CPU_ADD( "maincpu", P8098, XTAL_12MHz )
	MCFG_CPU_PROGRAM_MAP( s330_map )
	MCFG_CPU_IO_MAP( cpu_io_map )
	MCFG_TIMER_DRIVER_ADD_SCANLINE("scantimer", s330_state, s330_hblank_interrupt, "crtscreen", 0, 1)

	MCFG_WD1772_ADD("wd1772", XTAL_8MHz)
/*	MCFG_WD_FDC_INTRQ_CALLBACK(WRITELINE(s330_state,cb_intrq))*/
/*	MCFG_WD_FDC_DRQ_CALLBACK(WRITELINE(s330_state,cb_drq))*/
/*	MCFG_WD_FDC_HLD_CALLBACK(WRITELINE(s330_state,cb_hld))*/
/*	MCFG_WD_FDC_ENP_CALLBACK(WRITELINE(s330_state,cb_enp))*/

	/*floppy_image_device::default_floppy_formats*/
	MCFG_FLOPPY_DRIVE_ADD("wd1772:0", s330_floppies, "35dd", s330_state::floppy_formats)

	MCFG_SA16_ADD("sa16", XTAL_26_88MHz, sa16_waveram)

	MCFG_DEVICE_ADD("tms3556", TMS3556, XTAL_14_3496MHz)
	MCFG_HD44780_ADD("hd44780")
	MCFG_HD44780_LCD_SIZE(2, 16)

	MCFG_PALETTE_ADD_BLACK_AND_WHITE("lcd_pal")
	MCFG_PALETTE_INIT_OWNER(s330_state, lcd)
	MCFG_PALETTE_ADD("vdp_pal", 8)
	MCFG_PALETTE_INIT_OWNER(s330_state, vdp)
	MCFG_DEFAULT_LAYOUT(layout_s330)

	/* CRT screen */
	MCFG_SCREEN_ADD("crtscreen", RASTER)
	MCFG_SCREEN_VIDEO_ATTRIBUTES(VIDEO_UPDATE_BEFORE_VBLANK)
	MCFG_SCREEN_UPDATE_DEVICE("tms3556", tms3556_device, screen_update)
	MCFG_SCREEN_SIZE(TMS3556_TOTAL_WIDTH, TMS3556_TOTAL_HEIGHT*2)
	MCFG_SCREEN_VISIBLE_AREA(0, TMS3556_TOTAL_WIDTH-1, 0, TMS3556_TOTAL_HEIGHT-1)
	MCFG_SCREEN_REFRESH_RATE(50)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
	MCFG_SCREEN_PALETTE("vdp_pal")

	/* LCD screen */
	MCFG_SCREEN_ADD("lcdpanel", LCD)
	MCFG_SCREEN_REFRESH_RATE(50)
	MCFG_SCREEN_VBLANK_TIME(ATTOSECONDS_IN_USEC(2500)) /* not accurate */
	MCFG_SCREEN_SIZE(6*16, 9*2)
	MCFG_SCREEN_VISIBLE_AREA(0, 6*16-1, 0, 9*2-1)
	MCFG_SCREEN_UPDATE_DEVICE("hd44780", hd44780_device, screen_update)
	MCFG_SCREEN_PALETTE("lcd_pal")

	//MCFG_MIDI_PORT_ADD("mdin", midiin_slot, "midiin")
	//MCFG_MIDI_RX_HANDLER(DEVWRITELINE("maincpu:sci0", h8_sci_device, rx_w))

	//MCFG_MIDI_PORT_ADD("mdout", midiout_slot, "midiout")
	//MCFG_DEVICE_MODIFY("maincpu:sci0")
	//MCFG_H8_SCI_TX_CALLBACK(DEVWRITELINE(":mdout", midi_port_device, write_txd))
MACHINE_CONFIG_END

static INPUT_PORTS_START( s330_panel )
	PORT_START("panel0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Mode") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x01) PORT_CODE(KEYCODE_Q) PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Menu") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x02) PORT_CODE(KEYCODE_W) PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Dec/No") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x04) PORT_CODE(KEYCODE_E) PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Up") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x08) PORT_CODE(KEYCODE_R) PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Inc/Yes") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x10) PORT_CODE(KEYCODE_T) PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Command") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x20) PORT_CODE(KEYCODE_Y) PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("panel1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Page") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x101) PORT_CODE(KEYCODE_A) PORT_CHAR('A')
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sub Menu") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x102) PORT_CODE(KEYCODE_S) PORT_CHAR('S')
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Left") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x104) PORT_CODE(KEYCODE_D) PORT_CHAR('D')
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Down") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x108) PORT_CODE(KEYCODE_F) PORT_CHAR('F')
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Right") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x110) PORT_CODE(KEYCODE_G) PORT_CHAR('G')
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Execute") PORT_CHANGED_MEMBER(DEVICE_SELF, s330_state, keyhandler, 0x120) PORT_CODE(KEYCODE_H) PORT_CHAR('H')
	PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("panel2")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("panel3")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	// PORT_START("A7")
	// PORT_BIT(0x03ff, 0x0000, IPT_DIAL) PORT_NAME("Knob") PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_CODE_DEC(KEYCODE_DOWN) PORT_CODE_INC(KEYCODE_UP)
INPUT_PORTS_END

static INPUT_PORTS_START( s330 )
	PORT_INCLUDE( s330_panel )
INPUT_PORTS_END

ROM_START( s330 )
	ROM_REGION(0x4000, "bootrom", 0)
	//ROM_LOAD( "s330_rom.bin",	0x0000, 0x4000, CRC(AA09C3A1) )
	ROM_LOAD16_BYTE( "s330_even.bin", 0x0000, 0x2000, CRC(20AA7CE0) )
	ROM_LOAD16_BYTE( "s330_odd.bin",  0x0001, 0x2000, CRC(32A00F31) )

	ROM_REGION(0x20000, "bankram", ROMREGION_ERASE) // 128kbyte bank RAM
	ROM_REGION16_LE(0x100000, "waveram", ROMREGION_ERASE) // 512kword 12-bit wave RAM (768kbyte)
ROM_END
//    YEAR,NAME,PARENT,COMPAT,MACHINE,INPUT,     CLASS,INIT, COMPANY,FULLNAME,FLAGS)
CONS( 1987,s330,     0,     0,   s330, s330,s330_state,s330,"Roland", "S-330",MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
