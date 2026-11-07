// license:GPL-2.0+
// copyright-holders:Carl Lom
/***************************************************************************

    Philips PM 3580/PM 3585 front panel keyboard

    A Philips MAB8401B at 6 MHz runs ROM 4292 from an external EPROM.
    It scans a matrix of 8 lines (P1) by 8 rows (P0) in both directions
    and reads a dial: a pulse on /INT (= T0) per detent, with the
    direction on P2.0. It is an I2C master and writes key codes and
    dial steps to the 68070 at slave address 0x26.

    Byte protocol (ROM 4292 v1.01):
    - 0x00-0x3f: key press, code = line << 3 | row
    - 0x27: SHIFT pressed on its own; a key pressed while SHIFT is held
      is sent as code | 0x40
    - 0x2d 0x00 0x02: version, sent 3.6 s after reset or after the
      first key
    - 0x80 | (delta + 0x20), delta = -32..31: dial; bit 6 = SHIFT held

    TODO:
    - Key legends: only the host's character codes are known for the
      function keys.
    - The dial is modelled as clean pulses; the real encoder circuit is
      unknown.

***************************************************************************/

#include "emu.h"
#include "pm3585_kbd.h"


DEFINE_DEVICE_TYPE(PM3585_KBD, pm3585_kbd_device, "pm3585_kbd", "Philips PM 3585 front panel keyboard")

namespace {

// the ROM debounces a dial pulse for about 1.3 ms
constexpr attotime DIAL_PHASE = attotime::from_msec(2);

ROM_START(pm3585_kbd)
	ROM_REGION(0x1000, "mcu", 0)
	ROM_LOAD("rom04292.bin", 0x0000, 0x1000, CRC(db14b999) SHA1(e4ccf58b698e2c5521d4eea29c30fa1b6381789f)) // v1.01, 1990-11-22
ROM_END

} // anonymous namespace


pm3585_kbd_device::pm3585_kbd_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: device_t(mconfig, PM3585_KBD, tag, owner, clock)
	, m_mcu(*this, "mcu")
	, m_lines(*this, "LINE%u", 0U)
	, m_dial(*this, "DIAL")
	, m_scl_cb(*this)
	, m_sda_cb(*this)
	, m_dial_timer(nullptr)
	, m_p0(0xff)
	, m_p1(0xff)
	, m_dial_pos(0)
	, m_dial_phase(0)
	, m_dial_up(false)
{
}


static INPUT_PORTS_START(pm3585_kbd)
	// TODO: names of the keys the host maps to codes 0x80-0xaa
	PORT_START("LINE0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("FORMAT") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("I/O") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PRINT") PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("STOP") PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CONFIG") PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TRACE") PORT_CODE(KEYCODE_F6)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("DISPLAY") PORT_CODE(KEYCODE_F7)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("RUN") PORT_CODE(KEYCODE_F8)

	PORT_START("LINE1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_D) PORT_CHAR('d') PORT_CHAR('D')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_H) PORT_CHAR('h') PORT_CHAR('H')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_L) PORT_CHAR('l') PORT_CHAR('L')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_P) PORT_CHAR('p') PORT_CHAR('P')
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_T) PORT_CHAR('t') PORT_CHAR('T')
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_X) PORT_CHAR('x') PORT_CHAR('X')
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_SLASH) PORT_CHAR('/') PORT_CHAR('?')
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_BACKSPACE) PORT_CHAR(8)

	PORT_START("LINE2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_C) PORT_CHAR('c') PORT_CHAR('C')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_G) PORT_CHAR('g') PORT_CHAR('G')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_K) PORT_CHAR('k') PORT_CHAR('K')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_O) PORT_CHAR('o') PORT_CHAR('O')
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_S) PORT_CHAR('s') PORT_CHAR('S')
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_W) PORT_CHAR('w') PORT_CHAR('W')
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_MINUS) PORT_CHAR('_')
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("INS") PORT_CODE(KEYCODE_BACKSLASH) PORT_CHAR('\\') PORT_CHAR('|')

	PORT_START("LINE3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_B) PORT_CHAR('b') PORT_CHAR('B')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_F) PORT_CHAR('f') PORT_CHAR('F')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_J) PORT_CHAR('j') PORT_CHAR('J')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_N) PORT_CHAR('n') PORT_CHAR('N')
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_R) PORT_CHAR('r') PORT_CHAR('R')
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_V) PORT_CHAR('v') PORT_CHAR('V')
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Z) PORT_CHAR('z') PORT_CHAR('Z')
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SPACE") PORT_CODE(KEYCODE_SPACE) PORT_CHAR(' ')

	PORT_START("LINE4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_A) PORT_CHAR('a') PORT_CHAR('A')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_E) PORT_CHAR('e') PORT_CHAR('E')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_I) PORT_CHAR('i') PORT_CHAR('I')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_M) PORT_CHAR('m') PORT_CHAR('M')
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Q) PORT_CHAR('q') PORT_CHAR('Q')
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_U) PORT_CHAR('u') PORT_CHAR('U')
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_Y) PORT_CHAR('y') PORT_CHAR('Y')
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT) PORT_CODE(KEYCODE_RSHIFT) PORT_CHAR(UCHAR_SHIFT_1)

	PORT_START("LINE5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_9) PORT_CHAR('9')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_6) PORT_CHAR('6')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_3) PORT_CHAR('3')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("+/-") PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_HOME) PORT_CHAR(UCHAR_MAMEKEY(HOME))
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_RIGHT) PORT_CHAR(UCHAR_MAMEKEY(RIGHT))
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("LINE6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_8) PORT_CHAR('8')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_5) PORT_CHAR('5')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_2) PORT_CHAR('2')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_STOP) PORT_CHAR('.')
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_DEL) PORT_CHAR(UCHAR_MAMEKEY(DEL))
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_UP) PORT_CHAR(UCHAR_MAMEKEY(UP))
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SELECT") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_DOWN) PORT_CHAR(UCHAR_MAMEKEY(DOWN))

	PORT_START("LINE7")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_7) PORT_CHAR('7')
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_4) PORT_CHAR('4')
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_1) PORT_CHAR('1')
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_0) PORT_CHAR('0')
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_INSERT) PORT_CHAR(UCHAR_MAMEKEY(INSERT))
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_CODE(KEYCODE_LEFT) PORT_CHAR(UCHAR_MAMEKEY(LEFT))
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("DIAL")
	PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_SENSITIVITY(25) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_PGDN) PORT_CODE_INC(KEYCODE_PGUP) // the default arrow keys are panel keys
INPUT_PORTS_END

ioport_constructor pm3585_kbd_device::device_input_ports() const
{
	return INPUT_PORTS_NAME(pm3585_kbd);
}

const tiny_rom_entry *pm3585_kbd_device::device_rom_region() const
{
	return ROM_NAME(pm3585_kbd);
}

void pm3585_kbd_device::program_map(address_map &map)
{
	// 4K EPROM, A12 not connected
	map(0x0000, 0x0fff).mirror(0x1000).rom().region("mcu", 0);
}

void pm3585_kbd_device::device_add_mconfig(machine_config &config)
{
	MAB8401(config, m_mcu, clock());
	m_mcu->set_addrmap(AS_PROGRAM, &pm3585_kbd_device::program_map);
	m_mcu->p0_in_cb().set(FUNC(pm3585_kbd_device::p0_r));
	m_mcu->p0_out_cb().set([this] (uint8_t data) { m_p0 = data; });
	m_mcu->p1_in_cb().set(FUNC(pm3585_kbd_device::p1_r));
	m_mcu->p1_out_cb().set([this] (uint8_t data) { m_p1 = data; });
	m_mcu->p2_in_cb().set(FUNC(pm3585_kbd_device::p2_r));
	m_mcu->scl_out_cb().set([this] (int state) { m_scl_cb(state); });
	m_mcu->sda_out_cb().set([this] (int state) { m_sda_cb(state); });
}


void pm3585_kbd_device::device_start()
{
	m_dial_timer = timer_alloc(FUNC(pm3585_kbd_device::dial_tick), this);

	save_item(NAME(m_p0));
	save_item(NAME(m_p1));
	save_item(NAME(m_dial_pos));
	save_item(NAME(m_dial_phase));
	save_item(NAME(m_dial_up));
}

void pm3585_kbd_device::device_reset()
{
	m_dial_pos = m_dial->read();
	m_dial_phase = 0;
	m_mcu->set_input_line(MCS48_INPUT_IRQ, CLEAR_LINE);
	m_dial_timer->adjust(DIAL_PHASE, 0, DIAL_PHASE);
}


/***************************************************************************
    Key matrix: a key connects a P1 line to a P0 row
***************************************************************************/

uint8_t pm3585_kbd_device::p0_r()
{
	uint8_t data = 0xff;
	for (int line = 0; line < 8; line++)
	{
		if (!BIT(m_p1, line))
			data &= ~m_lines[line]->read();
	}
	return data;
}

uint8_t pm3585_kbd_device::p1_r()
{
	uint8_t data = 0xff;
	for (int line = 0; line < 8; line++)
	{
		if (m_lines[line]->read() & ~m_p0)
			data &= ~(1 << line);
	}
	return data;
}

uint8_t pm3585_kbd_device::p2_r()
{
	// P2.0: direction of the last dial step
	return m_dial_up ? 0xff : 0xfe;
}


/***************************************************************************
    Dial: one /INT pulse per step
***************************************************************************/

TIMER_CALLBACK_MEMBER(pm3585_kbd_device::dial_tick)
{
	switch (m_dial_phase)
	{
	case 0:
		{
			uint8_t const pos = m_dial->read();
			if (pos != m_dial_pos)
			{
				m_dial_up = int8_t(uint8_t(pos - m_dial_pos)) > 0;
				m_dial_pos += m_dial_up ? 1 : -1;
				m_mcu->set_input_line(MCS48_INPUT_IRQ, ASSERT_LINE);
				m_dial_phase = 1;
			}
		}
		break;

	case 1:
		m_mcu->set_input_line(MCS48_INPUT_IRQ, CLEAR_LINE);
		m_dial_phase = 2;
		break;

	default:
		m_dial_phase = 0;
		break;
	}
}


/***************************************************************************
    I2C bus
***************************************************************************/

void pm3585_kbd_device::scl_w(int state)
{
	m_mcu->scl_w(state);
}

void pm3585_kbd_device::sda_w(int state)
{
	m_mcu->sda_w(state);
}
