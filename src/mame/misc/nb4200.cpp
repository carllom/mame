// license:BSD-3-Clause
// copyright-holders:Carl Lom
/*
 * nb4200.cpp
 * Novix NB4200 Turbo-Frame evaluation board
 *
 * Chip:    Novix NC4016    (according to the manual. Actually, Novix NC4016P(P=prototype?) was on the card)
 * HCMOS
 * 121 pin PGA
 * if RAM/ROM is 120-200ns -> 3-4 MHz. If 50ns -> 8 MHz
 * Resets to 0x1000
 *
 * OS:  cmFORTH
 *
 * Terminal:    cmFORTH bit-bangs RS-232 in software: X0 is TxD and X4 is RxD.
 *      After reset it measures the bit rate from the first key typed
 *      (an even letter such as B) and answers "hi".
 *      The terminal is also the disk: cmFORTH sends 03 and a block number
 *      to read or write a 1024 byte block over the same line.
 *      scripts/nb4200/cmforth_host.py is a terminal and block file server:
 *        cmforth_host.py --blocks src.blk
 *        mame nb4200 -rs232 null_modem -bitb socket.127.0.0.1:4321
 *
 * Ports:   1 x 64 pin edge connector
 *
 * RAM:
 * 8Kword (16Kbyte) program memory (expandable to 32Kwords in steps 8/32)
 * 2kword (4Kbyte) data stack (expandable to 32Kwords in steps 2/8/32)
 * 2Kword (4Kbyte) return stack (expandable to 32Kwords in steps 2/8/32)
 * Jumpers exist to expand main and stack mem up to 32Kword each.
 * RAM is clocked @ 4.7 MHz
 *
 * ROM:
 * 8kword (16kbyte) EPROM (expandable to 32Kwords in steps 8/32)
 * Jumpers exist to expand ROM up to 32Kword.
 * ROM is clocked @ 3.5 MHz
 *
 * Memory map:
 *
 * Standard:
 * 0000-0FFF    RAM
 * 1000-2FFF    EPROM
 * 3000-3FFF    RAM
 *
 * JP1  1-2
 * JP2  1-2
 * JP5  2-3
 * JP7  2-3
 * JP8  1-2
 * JP9  1-2,3-4
 * JP10 1-2,3-4
 * JP11 1-2,3-4
 * JP12 1-2,3-4
 * JP13 1-2,3-4
 * JP14 1-2,3-4
 * JP15 1-2,3-4
 * JP16 1-2,3-4
 * JP17 1-2,3-4
 * JP18 3-4
 *
 * TODO: the memory size jumpers are not emulated:
 * JP1, JP7  main RAM size (2x8K, 8x8K, 32x8K)
 * JP5       data stack size (2x8K, 8x8K)
 * JP18      return stack size (2x8K, 8x8K)
 * JP2, JP8  EPROM size (8x8K, 32x8K)
 *
 * Other hardware:
 * board:NX4, NC4000 processor
 * 0000-0fff    4k RAM
 * 1000-2fff    8k EPROM
 * 3000-4000    4k RAM
 * 4000-7fff    RAM expansion area
 * 8000-ffff    experimental area
 *
 * Instructions
 *
 * stack machines (article)
 * |F E D C|B A 9 8|7 6|5|4 3 2 1 0|
 * ---------------------------------
 * | class |aluctrl|sub|r| ctl/adr |
 * ---------------------------------
 * CALL
 * |0| address                     |
 * ---------------------------------
 * BRANCH
 * |1 0 0 1|
 * ---------------------------------
 * MATH
 * |1 0 0 0|
 * ---------------------------------
 * INTERNAL
 * |1 1 0 0|
 * ---------------------------------
 * LOCAL
 * |1 1 0 0|
 * ---------------------------------
 * LITERAL
 * |1 1 0 0|
 * ---------------------------------
 *
 * forth as machine code (article)
 * |F E D C|B A 9|8 7|6|5|4|3|2|1|0|
 * ---------------------------------
 * |1 0 0 0| ALU | Y |T|;|S|D|/|SLR|
 * ---------------------------------
 *
 * ALU: 0=T 1=T&Y 2=T-Y 3=T|Y 4=T+Y 5=T^Y 6=Y-T 7=Y
 * Y: 0=N 1=N with carry 2=MD 3=SR
 * T: Copy T into N
 * ;: Return from subroutine, exit
 * S: Stack active
 * D: 32 bit shift
 * /: Divide
 * SLR: 0=nothing 1=shift right 2=shift left 3=propagate sign bit
 *
 *
 * 11C5
 *
 * Serial disk control
 * disk blocks are 1024 bytes
 * disk operation header is 0x03 MSB LSB
 * block number is 15 bit. if sign bit (bit 15) is set it is a write: 1024 bytes are sent, then a keypress is expected.
 */
#include "emu.h"

#include "bus/rs232/rs232.h"
#include "cpu/nc4000/nc4000.h"


namespace {

class nb4200_state : public driver_device
{
public:
	nb4200_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rs232(*this, "rs232")
	{
	}

	void nb4200(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<nc4000_device> m_maincpu;
	required_device<rs232_port_device> m_rs232;

	uint8_t m_rxd = 1;

	uint8_t portx_r();
	void portx_w(offs_t offset, uint8_t data, uint8_t mem_mask);

	void prg_mem_map(address_map &map) ATTR_COLD;
	void datastack_map(address_map &map) ATTR_COLD;
	void retstack_map(address_map &map) ATTR_COLD;
};

void nb4200_state::machine_start()
{
	save_item(NAME(m_rxd));
}

// X4 is the serial input; the other X pins are pulled low
uint8_t nb4200_state::portx_r()
{
	return m_rxd << 4;
}

// X0 is the serial output; an undriven pin idles at mark
void nb4200_state::portx_w(offs_t offset, uint8_t data, uint8_t mem_mask)
{
	m_rs232->write_txd(BIT(mem_mask, 0) ? BIT(data, 0) : 1);
}

void nb4200_state::prg_mem_map(address_map &map)
{
	map(0x0000, 0x0fff).ram();
	map(0x1000, 0x1fff).rom().region("maincpu", 0x2000);
	map(0x2000, 0x2fff).ram();
}

void nb4200_state::datastack_map(address_map &map)
{
	map(0x00, 0xff).ram();
}

void nb4200_state::retstack_map(address_map &map)
{
	map(0x00, 0xff).ram();
}

void nb4200_state::nb4200(machine_config &config)
{
	NC4000(config, m_maincpu, 4'000'000);
	m_maincpu->set_addrmap(AS_PROGRAM, &nb4200_state::prg_mem_map);
	m_maincpu->set_addrmap(AS_DATA, &nb4200_state::datastack_map);
	m_maincpu->set_addrmap(AS_IO, &nb4200_state::retstack_map);
	m_maincpu->in_portx_callback().set(FUNC(nb4200_state::portx_r));
	m_maincpu->out_portx_callback().set(FUNC(nb4200_state::portx_w));

	RS232_PORT(config, m_rs232, default_rs232_devices, "terminal");
	m_rs232->rxd_handler().set([this] (int state) { m_rxd = state; });
}

ROM_START(nb4200)
	ROM_REGION16_BE(0x4000, "maincpu", 0)
	ROM_LOAD16_BYTE("2764_h.bin", 0x0000, 0x2000, CRC(42bdb30f) SHA1(d80b098911c786abaf899fffa1beb616e382b24d))
	ROM_LOAD16_BYTE("2764_l.bin", 0x0001, 0x2000, CRC(26a5728c) SHA1(48e4e67a49ec75102b9aa818dac1352dc96dc40e))
ROM_END

} // anonymous namespace

/*    YEAR  NAME    PARENT  COMPAT  MACHINE  INPUT   CLASS         INIT        COMPANY        FULLNAME              FLAGS */
COMP( 1986, nb4200, 0,      0,      nb4200,  0,      nb4200_state, empty_init, "Novix",       "NB4200 Turbo-Frame", MACHINE_NO_SOUND_HW )
