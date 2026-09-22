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
#include "machine/6522via.h"
#include "machine/timer.h"
#include "imagedev/cassette.h"
#include "imagedev/snapquik.h"
#include "video/hd43160.h"
#include "screen.h"
#include "emupal.h"

// Firmware analysis (Ghidra, PPG Wave 2.0 OS ROM) summary - see individual comments below:
//  - IRQ (vector @FFF8 -> C561) is a pure timebase; FIRQ/SWI2/SWI3/NMI vectors are
//    uninitialized filler, not wired to anything on real hardware.
//  - Keyboard matrix scan and pot/ADC scan are both cooperatively polled from the
//    main loop (reset entry @C000), not interrupt-driven.
//  - Keyboard matrix: B060=row select (walking bit), B062=column read, 8 rows.
//    B061/B063 are driven alongside but unconfirmed (possible velocity-sense pass).
//  - Pot ADC: B071 (VIA port A, within the known B070-B07F VIA range) selects one of
//    16 analog channels, B068 returns the conversion result, B064 is an R/W
//    echo/mirror of unclear purpose.
//  - B030-B047 (4x PIA-like init idiom) and B050-B053 (UART-like: status-gated
//    data buffer, referenced from note/envelope code - candidate for the real
//    MIDI/cassette interface) are identified but not yet implemented; still fall
//    through to MAME's default unmapped-access logging.
class ppg_state : public driver_device
{
public:
	ppg_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"), // Main CPU
		m_piabtn(*this, "piabtn"),
		m_via(*this, "via"), // Confirmed role in firmware: ADC channel-select (port A -> B071) + IRQ timebase (B078). Cassette wiring is unconfirmed guesswork - see FUN_aeb8 analysis; B050-B053 (UART-like) is a stronger candidate for the real cassette/MIDI interface.
		m_cassette(*this, "cassette"),
		m_lcd(*this, "lcd"),
		m_digkey0(*this, "digkey0"),
		m_digkey1(*this, "digkey1"),
		m_digkey2(*this, "digkey2"),
		m_knob(*this, "knob_%u", 0U)
	{

	}

	required_device<m6809_device> m_maincpu;
	required_device<pia6821_device> m_piabtn;
	required_device<via6522_device> m_via;
	required_device<cassette_image_device> m_cassette;
	required_device<hd43160_device> m_lcd;

	void ppg(machine_config& config);
	void ppg_map(address_map& map);
	void ppg_palette(palette_device &palette);

	// Comparator + transistor amplifier stage between the tape input and VIA CB2
	// (per hardware analysis - no flip-flop/divider on the read path)
	TIMER_DEVICE_CALLBACK_MEMBER(cassette_poll);
	bool m_cass_level = false;

	// Bypasses real-time tape decode: takes the same header+data+checksum+endbyte
	// format ppgwavecassdecode produces and pokes it straight into RAM.
	QUICKLOAD_LOAD_MEMBER(quickload_cb);

	uint8_t lcd_r(offs_t offset); // TODO - make into a device
	void lcd_w(offs_t offset, uint8_t data);

	// Musical keyboard matrix (B060-B063). Row-select/column-read wiring confirmed
	// via firmware analysis; the actual key matrix layout is not yet known, so these
	// are logging stubs (not wired to input ports) pending further investigation.
	uint8_t kbd_r(offs_t offset);
	void kbd_w(offs_t offset, uint8_t data);
	uint8_t m_kbd_row = 0;

	// Pot/ADC scan (B064, B068). B071 (VIA port A) selects the channel.
	// B068 returns the selected knob's ioport value (16 pots, per acvirus.cpp-style
	// channel-select-through-ADC pattern). Knob 0-15 are placeholder names pending
	// mapping against the real panel legend.
	// B064 is implemented as a simple write-then-echo latch per observed R/W pattern.
	uint8_t adc_echo_r();
	void adc_echo_w(uint8_t data);
	uint8_t adc_result_r();
	void adc_result_w(uint8_t data);
	uint8_t m_adc_echo = 0;
	uint8_t m_adc_channel = 0;
	void via_pa_w(uint8_t data);

	void poll_keys();
	DECLARE_INPUT_CHANGED_MEMBER(keyhandler);
	void key_line_select(uint8_t data);
	required_ioport m_digkey0;
	required_ioport m_digkey1;
	required_ioport m_digkey2;
	required_ioport_array<16> m_knob;

	uint8_t m_leds = 0;
};

uint8_t ppg_state::lcd_r(offs_t offset) {
	logerror("LCD read @%04x\n", 0xB006 + offset);
	m_lcd->read(offset);
	return 0;
}

void ppg_state::lcd_w(offs_t offset, uint8_t data) {
	logerror("LCD write @%04x=%02x\n", 0xB006 + offset, data);
	m_lcd->write(offset, data);
}

uint8_t ppg_state::kbd_r(offs_t offset) {
	//logerror("KBD read @%04x (row=%02x)\n", 0xB060 + offset, m_kbd_row);
	return 0;
}

void ppg_state::kbd_w(offs_t offset, uint8_t data) {
	//logerror("KBD write @%04x=%02x\n", 0xB060 + offset, data);
	if (offset == 0) // B060: row select (walking bit, 8 rows)
		m_kbd_row = data;
}

uint8_t ppg_state::adc_echo_r() {
	//logerror("ADC echo read @B064=%02x\n", m_adc_echo);
	return m_adc_echo;
}

void ppg_state::adc_echo_w(uint8_t data) {
	//logerror("ADC echo write @B064=%02x\n", data);
	m_adc_echo = data;
}

uint8_t ppg_state::adc_result_r() {
	uint8_t const val = m_knob[m_adc_channel]->read();
	//logerror("ADC result read @B068 (channel=%d)=%02x\n", m_adc_channel, val);
	return val;
}

void ppg_state::adc_result_w(uint8_t data) {
	//logerror("ADC result write @B068=%02x\n", data);
}

void ppg_state::via_pa_w(uint8_t data) {
	m_adc_channel = data & 0x0f; // 16-channel analog mux select, per ADC_Scan_16Channel_Pots firmware analysis
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


TIMER_DEVICE_CALLBACK_MEMBER(ppg_state::cassette_poll)
{
	// Schmitt-trigger style hysteresis, matching the real comparator's need
	// to reject noise/slow slew right at the zero crossing
	double const v = m_cassette->input();
	bool level = m_cass_level;
	if (v > 0.1)
		level = true;
	else if (v < -0.1)
		level = false;

	if (level != m_cass_level)
	{
		m_cass_level = level;
		m_via->write_cb2(level);
		logerror("cass edge: t=%s level=%d v=%f\n", machine().time().as_string(9), level, v);
	}
}


QUICKLOAD_LOAD_MEMBER(ppg_state::quickload_cb)
{
	u32 const size = image.length();
	if (size < 6)
		return std::make_pair(image_error::INVALIDLENGTH, "File too short for header+checksum+endbyte");

	uint8_t hdr[4];
	image.fread(hdr, 4);
	uint16_t const startaddr = (uint16_t(hdr[0]) << 8) | hdr[1];
	uint16_t const endaddr = (uint16_t(hdr[2]) << 8) | hdr[3];
	if (startaddr > endaddr)
		return std::make_pair(image_error::INVALIDIMAGE, "Bad header: start address after end address");

	uint32_t const datalen = uint32_t(endaddr) - startaddr + 1;
	if (size < 4u + datalen + 1u)
		return std::make_pair(image_error::INVALIDLENGTH, "File truncated relative to header address range");

	address_space &program = m_maincpu->space(AS_PROGRAM);
	image.fread(program.get_write_ptr(startaddr), datalen);

	return std::make_pair(std::error_condition(), std::string());
}


void ppg_state::ppg_palette(palette_device &palette)
{
	// HD43160 dot-matrix LCD: silvery blue-green background, dark pixels
	palette.set_pen_color(0, rgb_t(140, 168, 163)); // background
	palette.set_pen_color(1, rgb_t( 30,  35,  38)); // lit pixel
}

void ppg_state::ppg_map(address_map& map)
{
	map(0x0000, 0x2FFF).ram(); // 12K RAM
	map(0x3000, 0x3FFF).ram(); // 4K WRAM
	map(0x4000, 0x7FFF).unmaprw(); // Nothing
	map(0x8000, 0xAFFF).rom().region("os", 0x0000); // Wave ROM
	map(0xB000, 0xB003).rw(m_piabtn, FUNC(pia6821_device::read), FUNC(pia6821_device::write)); // Panel buttons
	map(0xB006, 0xB007).rw(FUNC(ppg_state::lcd_r), FUNC(ppg_state::lcd_w)); // LCD panel
	map(0xB030, 0xB047).unmaprw(); // 4x PIA-like init idiom (write 0x36 then 0x76) - possible keyboard driver/sense chips, not yet identified
	map(0xB050, 0xB053).unmaprw(); // UART-like (status-gated data buffer, wide use from note/envelope code) - candidate for the real MIDI/cassette interface, not yet identified
	map(0xB060, 0xB063).rw(FUNC(ppg_state::kbd_r), FUNC(ppg_state::kbd_w)); // Musical keyboard matrix: row select/column read (logging stub, see class comment)
	map(0xB064, 0xB064).rw(FUNC(ppg_state::adc_echo_r), FUNC(ppg_state::adc_echo_w)); // Pot ADC: R/W echo latch, purpose unclear
	map(0xB068, 0xB068).rw(FUNC(ppg_state::adc_result_r), FUNC(ppg_state::adc_result_w)); // Pot ADC: conversion result (logging stub, see class comment)
	map(0xB070, 0xB07F).rw(m_via, FUNC(via6522_device::read), FUNC(via6522_device::write)); // Confirmed role: ADC channel-select (port A) + IRQ timebase. Cassette wiring still guesswork - see FUN_aeb8 analysis
	map(0xC000, 0xEFFF).rom().region("os", 0x3000); // OS ROM
	map(0xF000, 0xFFFF).rom().region("os", 0x5000); // mirror of E000
}

static INPUT_PORTS_START(ppg_panel)
PORT_START("digkey0")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x01) PORT_CODE(KEYCODE_0) PORT_CHAR('0')
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x02) PORT_CODE(KEYCODE_1) PORT_CHAR('1')
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x04) PORT_CODE(KEYCODE_2) PORT_CHAR('2')
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x08) PORT_CODE(KEYCODE_3) PORT_CHAR('3')
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x10) PORT_CODE(KEYCODE_4) PORT_CHAR('4')
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x20) PORT_CODE(KEYCODE_5) PORT_CHAR('5')
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x40) PORT_CODE(KEYCODE_6) PORT_CHAR('6')
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x80) PORT_CODE(KEYCODE_7) PORT_CHAR('7')

PORT_START("digkey1")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x01) PORT_CODE(KEYCODE_8) PORT_CHAR('8')
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x02) PORT_CODE(KEYCODE_9) PORT_CHAR('9')
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Left") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x04) PORT_CODE(KEYCODE_LEFT)
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Right") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x08) PORT_CODE(KEYCODE_RIGHT)
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Program") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x10) PORT_CODE(KEYCODE_Q) PORT_CHAR('Q')
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Digital") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x20) PORT_CODE(KEYCODE_W) PORT_CHAR('W')
PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Tuning") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x40) PORT_CODE(KEYCODE_E) PORT_CHAR('E')
PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Analog") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x80) PORT_CODE(KEYCODE_R) PORT_CHAR('R')

PORT_START("digkey2")
PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sequence") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x01) PORT_CODE(KEYCODE_T) PORT_CHAR('T')
PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Group") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x02) PORT_CODE(KEYCODE_A) PORT_CHAR('A')
PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Datat.") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x04) PORT_CODE(KEYCODE_S) PORT_CHAR('S')
PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Keyb.") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x08) PORT_CODE(KEYCODE_D) PORT_CHAR('D')
PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Panel") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x10) PORT_CODE(KEYCODE_F) PORT_CHAR('F')
PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Run/Stop") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ppg_state::keyhandler), 0x20) PORT_CODE(KEYCODE_G) PORT_CHAR('G')
PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

// Front-panel pots, read through the 16-channel ADC (B071 channel-select / B068 result).
// Placeholder names - channel-to-knob mapping not yet confirmed against the real panel legend.
PORT_START("knob_0")
PORT_ADJUSTER(64, "Release 2 (CH8)") PORT_MINMAX(0, 255) // ADSR-Envelope 2 group; panel2 function not yet identified
PORT_START("knob_1")
PORT_ADJUSTER(64, "LFO Rate") PORT_MINMAX(0, 255) // LFO/Sequ. group; panel2: LFOR (LFO rate)
PORT_START("knob_2")
PORT_ADJUSTER(64, "Pot 2") PORT_MINMAX(0, 255)
PORT_START("knob_3")
PORT_ADJUSTER(64, "Partial-Waves (Mod P)") PORT_MINMAX(0, 255) // Modifiers group
PORT_START("knob_4")
PORT_ADJUSTER(64, "VCF-Cutoff (Mod C)") PORT_MINMAX(0, 255) // Modifiers group
PORT_START("knob_5")
PORT_ADJUSTER(64, "Env1 > VCF (Mod F)") PORT_MINMAX(0, 255) // Modifiers Control group
PORT_START("knob_6")
PORT_ADJUSTER(64, "Env1 > Waves (Mod W)") PORT_MINMAX(0, 255) // Modifiers Control group
PORT_START("knob_7")
PORT_ADJUSTER(64, "VCF-Emphasis (Mod E)") PORT_MINMAX(0, 255) // Modifiers group
PORT_START("knob_8")
PORT_ADJUSTER(64, "Env2 > Loudn. (Mod L)") PORT_MINMAX(0, 255) // Modifiers Control group
// Pots 9-15 confirmed against the real front panel legend (dual-labeled: primary
// function / panel2 alternate function / CHn group position). See
// ppg20_panel_pots.md for the full writeup.
PORT_START("knob_9")
PORT_ADJUSTER(64, "A1 / Delay (CH1)") PORT_MINMAX(0, 255) // ADSR-Envelope 1 group; panel2: LFODL (LFO delay)
PORT_START("knob_10")
PORT_ADJUSTER(64, "A2 / A3 (CH2)") PORT_MINMAX(0, 255) // ADSR-Envelope 2 group; panel2: ATK3 (envelope 3 attack)
PORT_START("knob_11")
PORT_ADJUSTER(64, "D1 / WaveSp (CH3)") PORT_MINMAX(0, 255) // ADSR-Envelope 1 group; panel2: LFOW (LFO waveshape)
PORT_START("knob_12")
PORT_ADJUSTER(64, "D2 / D3 (CH4)") PORT_MINMAX(0, 255) // ADSR-Envelope 2 group; panel2: DEC3 (envelope 3 decay)
PORT_START("knob_13")
PORT_ADJUSTER(64, "S1 / Mod.Int. (CH5)") PORT_MINMAX(0, 255) // ADSR-Envelope 1 group; panel2: LFOIN (LFO mod intensity)
PORT_START("knob_14")
PORT_ADJUSTER(64, "S2 / E3>Pitch (CH6)") PORT_MINMAX(0, 255) // ADSR-Envelope 2 group; panel2: AP3 (envelope 3 to pitch)
PORT_START("knob_15")
PORT_ADJUSTER(64, "Release 1 (CH7)") PORT_MINMAX(0, 255) // ADSR-Envelope 1 group; panel2 function not yet identified
INPUT_PORTS_END

void ppg_state::ppg(machine_config& config)
{
	M6809(config, m_maincpu, 6_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &ppg_state::ppg_map);

	PIA6821(config, m_piabtn);
	m_piabtn->writepa_handler().set(FUNC(ppg_state::key_line_select));

	// Not derived from the CPU clock - likely its own oscillator. Backed out from
	// measured tape pulse widths (8/16 samples @ 44.1kHz) against FUN_aeb8's
	// 1024-tick short/long threshold; PA/PB/CA/CB wiring still guesswork.
	MOS6522(config, m_via, 4100000); // derived from ppgwavecassdecode fskdemoddurthres (249.4us) vs FUN_aeb8 1024-tick threshold
	m_via->writepa_handler().set(FUNC(ppg_state::via_pa_w)); // Port A = ADC channel select (confirmed via firmware ADC_Scan_16Channel_Pots analysis)

	// Tape in: comparator + transistor amplifier straight into CB2, no divider on read.
	// Write side (SR shift-out under T2, plus a J/K flip-flop pair) not yet modeled.
	CASSETTE(config, m_cassette);
	m_cassette->set_default_state(CASSETTE_STOPPED | CASSETTE_MOTOR_ENABLED);
	TIMER(config, "cass_poll").configure_periodic(FUNC(ppg_state::cassette_poll), attotime::from_hz(44100));

	// Loads a ppgwavecassdecode-format .bin (4-byte addr header + data + checksum + endbyte)
	// straight into RAM, bypassing the real-time tape decode entirely
	QUICKLOAD(config, "quickload", "bin").set_load_callback(FUNC(ppg_state::quickload_cb));

	HD43160(config, m_lcd, 0);
	m_lcd->set_lcd_size(2, 40); // 2*16 internal

	screen_device& screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(72);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	screen.set_size(6 * 40, 9 * 2); // 40 chars * 6px, 2 lines * (8px + 1px spacing)
	screen.set_visarea_full();
	screen.set_screen_update(m_lcd, FUNC(hd43160_device::screen_update));
	screen.set_palette("palette");
	PALETTE(config, "palette", FUNC(ppg_state::ppg_palette), 2);

}

ROM_START(ppg)
	ROM_REGION(0x6000, "os", ROMREGION_ERASEFF)
	ROM_LOAD("w20_181.bin", 0x0000, 0x1000, CRC(6BB5B573))
	ROM_LOAD("w20_191.bin", 0x1000, 0x1000, CRC(46739F8C))
	ROM_LOAD("w20_1a1.bin", 0x2000, 0x1000, CRC(85AACADA))
	ROM_LOAD("w20_1c1.bin", 0x3000, 0x1000, CRC(38290BD7))
	ROM_LOAD("w20_1d1.bin", 0x4000, 0x1000, CRC(C7800926))
	ROM_LOAD("w20_1e1.bin", 0x5000, 0x1000, CRC(40E43C10))
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE INPUT CLASS          INIT  COMPANY               FULLNAME          FLAGS */
COMP(1981, ppg, 0, 0, ppg, ppg_panel, ppg_state, empty_init, "Palm Products Gmbh", "PPG Wave 2", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
