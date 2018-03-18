/*
 * nb4200.c
 * Novix NB4200 Turbo Frame evaluation board
 *
 * Chip:	Novix NC4016	(according to the manual. Actually, Novix NC4016P(P=prototype?) was on the card)
 * HCMOS
 * 121 pin PGA
 * if RAM/ROM is 120-200ns -> 3-4MHz. If 50ns -> 8Mhz
 * Resets to 0x1000h
 *
 * OS:	cmFORTH
 *
 * Ports:	1 x 64 pin edge connector
 *
 * RAM:
 * 8Kword (16Kbyte) program memory (expandable to 32Kwords in steps 8/32)
 * 2kword (4Kbyte) data stack (expandable to 32Kwords in steps 2/8/32)
 * 2Kword (4Kbyte) return stack (expandable to 32Kwords in steps 2/8/32)
 * Jumpers exist to expand main and stack mem up to 32Kword each.
 * RAM is clocked @ 4.7MHz
 *
 * ROM:
 * 8kword (16kbyte) EPROM (expandable to 32Kwords in steps 8/32)
 * Jumpers exist to expand ROM up to 32Kword.
 * ROM is clocked @ 3.5MHz
 *
 * Memory map:
 *
 * Standard:
 * 0000-0FFF	RAM
 * 1000-2FFF	EPROM
 * 3000-3FFF	RAM
 *
 * JP1	1-2
 * JP2	1-2
 * JP5	2-3
 * JP7	2-3
 * JP8	1-2
 * JP9	1-2,3-4
 * JP10	1-2,3-4
 * JP11	1-2,3-4
 * JP12	1-2,3-4
 * JP13	1-2,3-4
 * JP14	1-2,3-4
 * JP15	1-2,3-4
 * JP16	1-2,3-4
 * JP17	1-2,3-4
 * JP18	3-4
 *
 * Other hardware:
 * board:NX4, NC4000 processor
 * 0000-0fff	4k RAM
 * 1000-2fff	8k EPROM
 * 3000-4000	4k RAM
 * 4000-7fff	RAM expansion area
 * 8000-ffff	experimental area
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
 * ALU: 0=T 1=T&Y 2=T-Y 3=T|Y 4=T-Y 5=T^Y 6=Y-T 7=Y
 * Y: 0=N 1=N with carry 2=MD 3=SR
 * T: Copy T into N
 * ;: Return from subroutine, exit
 * S: Stack active
 * D: 32 bit shift
 * /: Divide
 * SLR: 0=nothing 1=shift left 2=shift right 3=propagate sign bit
 *
 *
 * 11C5
 *
 * Serial disk control
 * disk blocks are 1024 bytes
 * disk operation header is 0x03 MSB LSB
 * block number is 15 bit. if sign bit (bit 15) is set it is a write and a keypress is expected before transmitting.
 */
#define ADDRESS_MAP_MODERN

#include "emu.h" // Core emulation goodies
#include "cpu/nc4000/nc4000.h"
#include "ioport.h"

// Terminal emulation includes
#include "machine/terminal.h"
#include "machine/6850acia.h"
#include "video/vtvideo.h"

// TX and RX are from the CPU perspective here
#define MASK_SER_TX 0x01
#define MASK_SER_RX 0x10

#define TAG_CPU "cpu"
#define TAG_PRG_RAM "prgram"
#define TAG_DAT_RAM "datastack"
#define TAG_RET_RAM "retstack"

enum
{
	START = 0,
	STOP = 9
};
class nb4200_state : public driver_device
{
public:
    nb4200_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
    	m_cpu(*this, TAG_CPU),
    	m_terminal(*this, TERMINAL_TAG)
    {
    }
	required_device<nc4000_device> m_cpu;
	required_device<serial_terminal_device> m_terminal;

	emu_timer *m_terminaltimer;
	static const device_timer_id TIMER_TERMINAL = 0;
	int m_terminal_r_bit;
	int m_terminal_state;
	UINT8 m_terminal_shift;
	DECLARE_WRITE_LINE_MEMBER(terminal_serial_w);

    virtual void machine_start();
    virtual void machine_reset();
	virtual void device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr);

    DECLARE_WRITE8_MEMBER(term_kbd_out);
    DECLARE_READ8_MEMBER(cpu_readx);
    DECLARE_WRITE8_MEMBER(cpu_writex);
    DECLARE_READ_LINE_MEMBER(cpu_irq);
};

void nb4200_state::machine_start()
{
	m_terminaltimer = timer_alloc(TIMER_TERMINAL);
	m_terminaltimer->adjust(attotime::zero, 0, attotime::from_hz(9600));
}

void nb4200_state::machine_reset()
{
	machine().firstcpu->reset();
}

void nb4200_state::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	switch(id)
	{
		case TIMER_TERMINAL:
			m_terminal_r_bit = m_terminal->tx_r(); // terminal_serial_r(m_terminal);
//			logerror("terminal->timer: %i\n", m_terminal_r_bit);
			break;
	}
}

WRITE_LINE_MEMBER( nb4200_state::terminal_serial_w )
{
	m_terminal->rx_w(state);

//	switch (m_terminal_state)
//	{
//	case START:
//		if (!state) m_terminal_state++;
//		break;
//
//	case STOP:
//		terminal_write(m_terminal, 0, m_terminal_shift);
//		m_terminal_state = START;
//		break;
//
//	default:
//		m_terminal_shift >>= 1;
//		m_terminal_shift |= (state << 7);
//		m_terminal_state++;
//		break;
//	}
}


READ8_MEMBER(nb4200_state::cpu_readx)
{
	UINT8 data = (m_cpu->portx_r() & 0xEF) | ((m_terminal_r_bit<< 4) & 0x10 );
//	logerror("terminal->cpu: %i %02x\n", m_terminal_r_bit, data);
	return data;
}

WRITE8_MEMBER(nb4200_state::cpu_writex)
{
//	logerror("cpu->terminal: %02x\n", data);
	terminal_serial_w(data & 1);
}

READ_LINE_MEMBER(nb4200_state::cpu_irq)
{
	return CLEAR_LINE;
}

static ADDRESS_MAP_START(cpu_prg_mem, AS_PROGRAM, 16, nb4200_state)
	AM_RANGE( 0x0000, 0x0fff ) AM_RAM
    AM_RANGE( 0x1000, 0x1fff ) AM_ROM
	AM_RANGE( 0x2000, 0x2fff ) AM_RAM
ADDRESS_MAP_END

static ADDRESS_MAP_START(datastack_ram, AS_1, 16, nb4200_state)
	AM_RANGE( 0x00, 0xff ) AM_RAM AM_REGION(TAG_DAT_RAM, 0)
ADDRESS_MAP_END

static ADDRESS_MAP_START(retstack_ram, AS_2, 16, nb4200_state)
	AM_RANGE( 0x00, 0xff ) AM_RAM AM_REGION(TAG_RET_RAM, 0)
ADDRESS_MAP_END

INPUT_PORTS_START(nb4200)
	PORT_START("jumpers")

	PORT_DIPNAME(0x03, 0x00, "RAM size")
	PORT_DIPLOCATION("JP:1,JP:7")
	PORT_DIPSETTING( 0x00, "2x8K")
	PORT_DIPSETTING( 0x01, "8x8K")
	PORT_DIPSETTING( 0x02, "32x8K")

	PORT_DIPNAME(0x04, 0x00, "Data stack size")
	PORT_DIPLOCATION("JP:5")
	PORT_DIPSETTING( 0x00, "2x8K")
	PORT_DIPSETTING( 0x04, "8x8K")

	PORT_DIPNAME(0x08, 0x00, "Return stack size")
	PORT_DIPLOCATION("JP:18")
	PORT_DIPSETTING( 0x00, "2x8K")
	PORT_DIPSETTING( 0x08, "8x8K")

	PORT_DIPNAME(0x30, 0x00, "EPROM size")
	PORT_DIPLOCATION("JP:2,JP:8")
	PORT_DIPSETTING( 0x00, "8x8K")
	PORT_DIPSETTING( 0x30, "32x8K")
INPUT_PORTS_END

//-------------------------------------------------
//  GENERIC_TERMINAL_INTERFACE( terminal_intf )
//-------------------------------------------------

WRITE8_MEMBER( nb4200_state::term_kbd_out )
{
	logerror("key typed: %c, %02x\n", data, data);
//	if (data == 0x0D)
//		terminal_write(m_terminal, 0, 0x0A); // Append line feed on carriage return
//
//	terminal_write(m_terminal, 0, data); // Echo input
	// this is here only so that terminal.c will initialize the keyboard scan timer
}

static GENERIC_TERMINAL_INTERFACE( terminal_intf )
{
	DEVCB_DRIVER_MEMBER(nb4200_state, term_kbd_out)
};

NC4000_INTERFACE(nb4200_cpu_config)
{
	DEVCB_NULL,  // PortB read16 cb
	DEVCB_NULL, // PortB write16 cb
	DEVCB_DRIVER_MEMBER(nb4200_state, cpu_readx),
	DEVCB_DRIVER_MEMBER(nb4200_state, cpu_writex),
	DEVCB_NULL // IRQ
};

MACHINE_CONFIG_START(nb4200, nb4200_state)
	MCFG_CPU_ADD(TAG_CPU, NC4000, 4000000)
	MCFG_CPU_PROGRAM_MAP(cpu_prg_mem)
	MCFG_DEVICE_ADDRESS_MAP(AS_1, datastack_ram)
	MCFG_DEVICE_ADDRESS_MAP(AS_2, retstack_ram)
	MCFG_CPU_CONFIG( nb4200_cpu_config )

//	MCFG_FRAGMENT_ADD( generic_terminal )
	MCFG_GENERIC_TERMINAL_ADD(TERMINAL_TAG, terminal_intf)
MACHINE_CONFIG_END

ROM_START(nb4200)
	ROM_REGION16_BE(0x20000, TAG_CPU, ROMREGION_ERASEFF)
	ROM_LOAD16_BYTE( "2764_h.bin", 0x0000, 0x2000, CRC(42bdb30f) )
	/*ROM_LOAD16_BYTE( "2764_l.bin", 0x0001, 0x2000, CRC(26a5728c) )*/
	ROM_LOAD16_BYTE( "lo.bin", 0x0001, 0x2000, CRC(386ea113) )


	ROM_REGION16_BE( 0x1000, TAG_DAT_RAM, ROMREGION_ERASEFF)
	ROM_REGION16_BE( 0x1000, TAG_RET_RAM, ROMREGION_ERASEFF)
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT  CLASS   INIT    COMPANY   FULLNAME       FLAGS */
COMP( 1987, nb4200, 0, 0, nb4200, nb4200, driver_device, 0, "Novix, Inc.", "NB4200 Turbo Frame", GAME_NO_SOUND )
