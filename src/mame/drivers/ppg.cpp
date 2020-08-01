/*
 * fuboard.c
 * FuB//o/ard - Imaginary SBC computer
 *
 *  Created on: 8 jun 2011
 *      Author: Carl Lom
 */
#include "emu.h" // Core emulation goodies
#include "cpu/m6809/m6809.h"
#include "machine/6821pia.h"
#include "video/hd43160.h"

class ppg_state : public driver_device
{
public:
	ppg_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"), // Main CPU
		m_piabtn(*this, "piabtn"),
		m_digkey0(*this, "digkey0"),
		m_digkey1(*this, "digkey1"),
		m_digkey2(*this, "digkey2"),
		m_lcd(*this, "lcd")
	{

	}

	required_device<m6809_device> m_maincpu;
	required_device<pia6821_device> m_piabtn;
	required_device<hd43160_device> m_lcd;

	DECLARE_READ8_MEMBER(io_r);
	DECLARE_WRITE8_MEMBER(io_w);

	DECLARE_READ8_MEMBER(lcd_r); // TODO - make into a device
	DECLARE_WRITE8_MEMBER(lcd_w);

	void poll_keys();
	DECLARE_INPUT_CHANGED_MEMBER(keyhandler);
	DECLARE_WRITE8_MEMBER(key_line_select);
	required_ioport m_digkey0;
	required_ioport m_digkey1;
	required_ioport m_digkey2;

	UINT8 m_leds = 0;
};

READ8_MEMBER(ppg_state::io_r) {
	logerror("%s: I/O read @%02x\n", space.machine().describe_context(), offset);
	return 0;
}

WRITE8_MEMBER(ppg_state::io_w) {
	logerror("%s: I/O write @%02x=%02x\n", space.machine().describe_context(), offset, data);
}

READ8_MEMBER(ppg_state::lcd_r) {
	logerror("%s: LCD read @%02x\n", space.machine().describe_context(), offset);
	m_lcd->read(space, offset, mem_mask);
	return 0;
}

WRITE8_MEMBER(ppg_state::lcd_w) {
	logerror("%s: LCD write @%02x=%02x\n", space.machine().describe_context(), offset, data);
	m_lcd->write(space, offset, data, mem_mask);
}

void ppg_state::poll_keys() {
	UINT8 line = ~(m_piabtn->a_output()) & 0xF;

	UINT8 pia0_pb = 0xFF;
	UINT8 pia0_pb_z = 0xFF;

	ioport_value portval = -1;
	if (line & 1)
		portval = m_digkey0->read();
	else if (line & 2)
		portval = m_digkey1->read();
	else if (line & 4)
		portval = m_digkey2->read();

	m_piabtn->portb_w((UINT8)portval);
}

WRITE8_MEMBER(ppg_state::key_line_select) {
	poll_keys();
	data >>= 4; // Top nybble has leds
	if (m_leds != data) {
		m_leds = data;
		logerror("LED change: GroupA=%d GroupB=%d PanelB=%d PanelC=%d\n", (m_leds & 8)>0, (m_leds & 4)>0, (m_leds & 2)>0, (m_leds & 1)>0);
	}
}


INPUT_CHANGED_MEMBER(ppg_state::keyhandler) {
	poll_keys();
}

static ADDRESS_MAP_START(ppg_map, AS_PROGRAM, 8, ppg_state)
	AM_RANGE(0x0000, 0x2FFF) AM_RAM AM_REGION("ram", 0x0000)// 12K RAM
	AM_RANGE(0x3000, 0x3FFF) AM_RAM AM_REGION("wram", 0x0000) // 4K WRAM
	//AM_RANGE(0x3000, 0x7FFF) AM_UNMAP // Nothing
	AM_RANGE(0x8000, 0xAFFF) AM_ROM AM_REGION("os", 0x0000) // Wave ROM
	AM_RANGE(0xB000, 0xB003) AM_DEVREADWRITE("piabtn", pia6821_device, read, write) // Panel buttons
	AM_RANGE(0xB006, 0xB007) AM_READWRITE(lcd_r, lcd_w) // LCD panel
	AM_RANGE(0xB000, 0xBFFF) AM_READWRITE(io_r, io_w) // I/O space
	AM_RANGE(0xC000, 0xEFFF) AM_ROM AM_REGION("os", 0x3000) // OS ROM
	AM_RANGE(0xF000, 0xFFFF) AM_ROM AM_REGION("os", 0x5000) // mirror of E000
ADDRESS_MAP_END

static INPUT_PORTS_START(ppg_panel)
PORT_START("digkey0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x01) PORT_CODE(KEYCODE_0) PORT_CHAR('0')
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x02) PORT_CODE(KEYCODE_1) PORT_CHAR('1')
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x04) PORT_CODE(KEYCODE_2) PORT_CHAR('2')
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x08) PORT_CODE(KEYCODE_3) PORT_CHAR('3')
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x10) PORT_CODE(KEYCODE_4) PORT_CHAR('4')
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x20) PORT_CODE(KEYCODE_5) PORT_CHAR('5')
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x40) PORT_CODE(KEYCODE_6) PORT_CHAR('6')
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x80) PORT_CODE(KEYCODE_7) PORT_CHAR('7')

PORT_START("digkey1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x01) PORT_CODE(KEYCODE_8) PORT_CHAR('8')
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x02) PORT_CODE(KEYCODE_9) PORT_CHAR('9')
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Left") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x04) PORT_CODE(KEYCODE_LEFT)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Right") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x08) PORT_CODE(KEYCODE_RIGHT)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Program") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x10) PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Digital") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x20) PORT_CODE(KEYCODE_W) PORT_CHAR('W')
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Tuning") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x40) PORT_CODE(KEYCODE_E) PORT_CHAR('E')
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Analog") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x80) PORT_CODE(KEYCODE_R) PORT_CHAR('R')

PORT_START("digkey2")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sequence") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x01) PORT_CODE(KEYCODE_T) PORT_CHAR('T')
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Group") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x02) PORT_CODE(KEYCODE_A) PORT_CHAR('A')
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Datat.") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x04) PORT_CODE(KEYCODE_S) PORT_CHAR('S')
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keyb.") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x08) PORT_CODE(KEYCODE_D) PORT_CHAR('D')
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Panel") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x10) PORT_CODE(KEYCODE_F) PORT_CHAR('F')
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Run/Stop") PORT_CHANGED_MEMBER(DEVICE_SELF, ppg_state, keyhandler, 0x20) PORT_CODE(KEYCODE_G) PORT_CHAR('G')
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)


// PORT_START("A7")
// PORT_BIT(0x03ff, 0x0000, IPT_DIAL) PORT_NAME("Knob") PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_CODE_DEC(KEYCODE_DOWN) PORT_CODE_INC(KEYCODE_UP)
INPUT_PORTS_END


static MACHINE_CONFIG_START(ppg, ppg_state)
	MCFG_CPU_ADD("maincpu", M6809, XTAL_1MHz)
	MCFG_CPU_PROGRAM_MAP(ppg_map)
	MCFG_DEVICE_ADD("piabtn", PIA6821, 0)
	MCFG_PIA_WRITEPA_HANDLER(WRITE8(ppg_state, key_line_select))
	MCFG_HD43160_ADD("lcd")
	MCFG_HD43160_LCD_SIZE(2, 40) // 2*16 internal

	MCFG_SCREEN_ADD("screen", LCD)
	MCFG_SCREEN_REFRESH_RATE(72)
	MCFG_SCREEN_SIZE(480, 480)
	MCFG_SCREEN_VISIBLE_AREA(0, 480 - 1, 0, 480 - 1)
	MCFG_SCREEN_UPDATE_DEVICE("lcd", hd43160_device, screen_update)
	MCFG_SCREEN_PALETTE("palette")
	MCFG_PALETTE_ADD_BLACK_AND_WHITE("palette")
MACHINE_CONFIG_END

ROM_START(ppg)
	ROM_REGION(0x6000, "os", ROMREGION_ERASEFF)
	ROM_LOAD("W20_181.bin", 0x0000, 0x1000, CRC(6BB5B573))
	ROM_LOAD("W20_191.bin", 0x1000, 0x1000, CRC(46739F8C))
	ROM_LOAD("W20_1A1.bin", 0x2000, 0x1000, CRC(85AACADA))
	ROM_LOAD("W20_1C1.bin", 0x3000, 0x1000, CRC(38290BD7))
	ROM_LOAD("W20_1D1.bin", 0x4000, 0x1000, CRC(C7800926))
	ROM_LOAD("W20_1E1.bin", 0x5000, 0x1000, CRC(40E43C10))

	ROM_REGION(0x3000, "ram", ROMREGION_ERASEVAL(0xA9))
	ROM_REGION(0x1000, "wram", ROMREGION_ERASEVAL(0x55))
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE INPUT CLASS          INIT  COMPANY               FULLNAME          FLAGS */
COMP(1981, ppg, 0, 0, ppg, ppg_panel, driver_device, 0, "Palm Products Gmbh", "PPG Wave 2", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
