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
#include "screen.h"
#include "emupal.h"

class ppg_state : public driver_device
{
public:
	ppg_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"), // Main CPU
		m_piabtn(*this, "piabtn"),
		m_lcd(*this, "lcd"),
		m_digkey0(*this, "digkey0"),
		m_digkey1(*this, "digkey1"),
		m_digkey2(*this, "digkey2")
	{

	}

	required_device<m6809_device> m_maincpu;
	required_device<pia6821_device> m_piabtn;
	required_device<hd43160_device> m_lcd;

	void ppg(machine_config& config);
	void ppg_map(address_map& map);

	uint8_t io_r(offs_t offset);
	void io_w(offs_t offset, uint8_t data);

	uint8_t lcd_r(offs_t offset); // TODO - make into a device
	void lcd_w(offs_t offset, uint8_t data);

	void poll_keys();
	DECLARE_INPUT_CHANGED_MEMBER(keyhandler);
	void key_line_select(uint8_t data);
	required_ioport m_digkey0;
	required_ioport m_digkey1;
	required_ioport m_digkey2;

	uint8_t m_leds = 0;
};

uint8_t ppg_state::io_r(offs_t offset) {
	logerror("I/O read @%02x\n", offset);
	return 0;
}

void ppg_state::io_w(offs_t offset, uint8_t data) {
	logerror("I/O write @%02x=%02x\n", offset, data);
}

uint8_t ppg_state::lcd_r(offs_t offset) {
	logerror("LCD read @%02x\n", offset);
	m_lcd->read(offset);
	return 0;
}

void ppg_state::lcd_w(offs_t offset, uint8_t data) {
	logerror("LCD write @%02x=%02x\n", offset, data);
	m_lcd->write(offset, data);
}

void ppg_state::poll_keys() {
	uint8_t line = ~(m_piabtn->a_output()) & 0xF;

	//uint8_t pia0_pb = 0xFF;
	//uint8_t pia0_pb_z = 0xFF;

	ioport_value portval = -1;
	if (line & 1)
		portval = m_digkey0->read();
	else if (line & 2)
		portval = m_digkey1->read();
	else if (line & 4)
		portval = m_digkey2->read();

	m_piabtn->portb_w((uint8_t)portval);
}

void ppg_state::key_line_select(uint8_t data) {
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


void ppg_state::ppg_map(address_map& map)
{
	map(0x0000, 0x2FFF).ram().region("ram", 0x0000); // 12K RAM
	map(0x3000, 0x3FFF).ram().region("wram", 0x0000); // 4K WRAM
	map(0x4000, 0x7FFF).unmaprw(); // Nothing
	map(0x8000, 0xAFFF).rom().region("os", 0x0000); // Wave ROM
	map(0xB000, 0xB003).rw(m_piabtn, FUNC(pia6821_device::read), FUNC(pia6821_device::write)); // Panel buttons
	map(0xB006, 0xB007).rw(FUNC(ppg_state::lcd_r), FUNC(ppg_state::lcd_w)); // LCD panel
	map(0xB000, 0xBFFF).rw(FUNC(ppg_state::io_r), FUNC(ppg_state::io_w)); // I/O space
	map(0xC000, 0xEFFF).rom().region("os", 0x3000); // OS ROM
	map(0xF000, 0xFFFF).rom().region("os", 0x5000); // mirror of E000
}

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

void ppg_state::ppg(machine_config& config)
{
	M6809(config, m_maincpu, 1_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &ppg_state::ppg_map);

	PIA6821(config, m_piabtn);
	m_piabtn->writepa_handler().set(FUNC(ppg_state::key_line_select));

	HD43160(config, m_lcd, 0);
	m_lcd->set_lcd_size(2, 40); // 2*16 internal

	screen_device& screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(72);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	screen.set_size(480, 480);
	screen.set_visarea_full();
	screen.set_screen_update("lcd", FUNC(hd43160_device::screen_update));
	screen.set_palette("palette");
	PALETTE(config, "palette", palette_device::MONOCHROME);

}

ROM_START(ppg)
	ROM_REGION(0x6000, "os", ROMREGION_ERASEFF)
	ROM_LOAD("w20_181.bin", 0x0000, 0x1000, CRC(6BB5B573))
	ROM_LOAD("w20_191.bin", 0x1000, 0x1000, CRC(46739F8C))
	ROM_LOAD("w20_1a1.bin", 0x2000, 0x1000, CRC(85AACADA))
	ROM_LOAD("w20_1c1.bin", 0x3000, 0x1000, CRC(38290BD7))
	ROM_LOAD("w20_1d1.bin", 0x4000, 0x1000, CRC(C7800926))
	ROM_LOAD("w20_1e1.bin", 0x5000, 0x1000, CRC(40E43C10))

	ROM_REGION(0x3000, "ram", ROMREGION_ERASEVAL(0xA9))
	ROM_REGION(0x1000, "wram", ROMREGION_ERASEVAL(0x55))
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE INPUT CLASS          INIT  COMPANY               FULLNAME          FLAGS */
COMP(1981, ppg, 0, 0, ppg, ppg_panel, ppg_state, empty_init, "Palm Products Gmbh", "PPG Wave 2", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
