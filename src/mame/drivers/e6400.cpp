#include "machine/upd765.h"
#include "formats/pc_dsk.h"
#include "cpu/m68000/m68000.h"
#include "video/t6963.h"
//#include "video/tms3556.h"
//#include "s330.lh"
//#include "audio/sa16.h"
//#include "bus/midi/midiinport.h"
//#include "bus/midi/midioutport.h"

//#include "formats/ql_dsk.h"

#include "emu.h"

#define	XTAL_14_3496MHz 14349600
#define XTAL_26_88MHz 26880000
/*
 * MC68EC020 CPU
 * S82078 FDC controller
 * - 24MHz crystal (next to FDC)
 * T6961/T6963(?) LCD driver (not my info)
 *
 *
 * (W5 IP822D (c)EMU '96 9811) MEM_PAL
 * (W5 IP872A (c)EMU '96 9808) CS_PAL
 * (W5 IP751B.1 EMU 1098) ???
 *
 * 2x HM514260 DRAM 256k*16bit (total 512k)
 * IDT7202SO 1Kx9 dual port fifo buffer
 * AM85C80-16JC SCSI + serial comms controller
 * CS8411-CP Digital Audio Interface Receiver (s/pdif etc)
 * CS8402A-CP Digital Audio Interface Transmitter (s/pdif etc)
 * AMI E-MU IC413 Rev A 9807NMQ 6753-501
 * 4?x AD1861 18bit DAC
 */
class e6400_state : public driver_device
{
public:
	e6400_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_lcdc(*this, "t6963"),
		m_fdc(*this, "fdc"),
		m_csel(0)
	{
	}

	required_device<cpu_device> m_maincpu;
	required_device<t6963_device> m_lcdc;
	optional_device<n82077aa_device> m_fdc;


	int m_csel;

	DECLARE_DRIVER_INIT( e6400 );
//	DECLARE_READ8_MEMBER(d800_read);
//	DECLARE_WRITE8_MEMBER(d800_write);
	DECLARE_WRITE16_MEMBER(chipsel_w);
	DECLARE_READ8_MEMBER(t696x_r);
	DECLARE_WRITE8_MEMBER(t696x_w);

	DECLARE_FLOPPY_FORMATS( floppy_formats );

	virtual void machine_reset();
	virtual void machine_start();
};

DRIVER_INIT_MEMBER( e6400_state, e6400 )
{
}

void e6400_state::machine_reset()
{
	m_maincpu->reset();
}

void e6400_state::machine_start()
{
}

/*** Floppy interface start ***/

static SLOT_INTERFACE_START( e6400_floppies )
	SLOT_INTERFACE( "35hd", FLOPPY_35_HD )
SLOT_INTERFACE_END

FLOPPY_FORMATS_MEMBER( e6400_state::floppy_formats )
	FLOPPY_PC_FORMAT
FLOPPY_FORMATS_END

/*** Floppy interface end ***/

WRITE16_MEMBER(e6400_state::chipsel_w)
{
	m_csel = data;
}

READ8_MEMBER(e6400_state::t696x_r)
{
	if (m_csel & 0x100)
	{
		return m_lcdc->status_r(space, offset/2, mem_mask);
	}
	else
	{
		return m_lcdc->data_r(space, offset, mem_mask);

	}
	//logerror("%s: LCD Read\n", space.machine().describe_context());
	//return 3;
	return -1;
}

WRITE8_MEMBER(e6400_state::t696x_w)
{
	if (m_csel & 0x100)
	{
		m_lcdc->control_w(space, offset, data, mem_mask);
	}
	else
	{
		m_lcdc->data_w(space, offset, data, mem_mask);
	}
	//logerror("%s: LCD Write %02x\n", space.machine().describe_context(), data);
}
//READ8_MEMBER( s330_state::d800_read )
//{
//	logerror("%s: $D800 read\n", space.machine().describe_context());
//	return m_d800;
//}
//WRITE8_MEMBER( s330_state::d800_write )
//{
//	logerror("%s: $D800 write (data=%02x)\n", space.machine().describe_context(), data);
//	m_d800 = data;
//}

static ADDRESS_MAP_START( e6400_map, AS_PROGRAM, 32, e6400_state )

	AM_RANGE(0x000000, 0x0001FF) AM_ROM AM_REGION("eos_flash", 0x0)
	AM_RANGE(0x000200, 0x0003FF) AM_RAM
	AM_RANGE(0x010800, 0x0FF3FF) AM_ROM AM_REGION("eos_flash", 0x400)

//	AM_RANGE(0x400000, 0x5FFFFF) AM_UNMAP
	AM_RANGE(0x400000, 0x400003) AM_WRITE16(chipsel_w, 0xffffffff)

	AM_RANGE(0x560000, 0x560007) AM_DEVICE8("fdc", n82077aa_device, map, 0xffffffff)
	AM_RANGE(0x580000, 0x580003) AM_READWRITE8(t696x_r, t696x_w, 0xffffffff)

	// 0x5A0000 - isr

//	AM_RANGE(0xC300, 0xC300) AM_DEVREADWRITE("hd44780", hd44780_device, control_read, control_write)
//	AM_RANGE(0xC302, 0xC302) AM_DEVREADWRITE("hd44780", hd44780_device, data_read, data_write)

	AM_RANGE(0xF00000, 0xFFFFFF) AM_RAM // 2x256k 16 bit DRAM
//	AM_RANGE(0xF00400, 0xF063FF) AM_RAM // RAM copy of flash data portion
//	AM_RANGE(0xFF0000, 0xFFBFFF) AM_RAM // Stack from FFC000 down
	//
	// I/O area
	// The I/O controller maps odd addresses to SR-16 (Wave RAM interface)
	// Even addresses are mapped to other chips/ports
	//
//	AM_RANGE(0xC200, 0xC200) AM_READWRITE(fdc_r, fdc_w)

//	AM_RANGE(0xC300, 0xC300) AM_DEVREADWRITE("hd44780", hd44780_device, control_read, control_write)
//	AM_RANGE(0xC302, 0xC302) AM_DEVREADWRITE("hd44780", hd44780_device, data_read, data_write)
	// Catch for odd/unmapped addresses
//	AM_RANGE(0xC000, 0xFFFF) AM_READWRITE(m60013_r, m60013_w)
ADDRESS_MAP_END

//static ADDRESS_MAP_START( cpu_io_map, AS_IO, 16, s330_state )
//	AM_RANGE(i8x9x_device::A4, i8x9x_device::A4) AM_READ(ad_adj_r)
//	AM_RANGE(i8x9x_device::A7, i8x9x_device::A7) AM_READ(ad_conv_r)
//	AM_RANGE(i8x9x_device::SERIAL, i8x9x_device::SERIAL) AM_READWRITE(midi_r, midi_w)
//ADDRESS_MAP_END

static MACHINE_CONFIG_START( e6400, e6400_state )
	MCFG_CPU_ADD( "maincpu", M68EC020, XTAL_24MHz )
	MCFG_CPU_PROGRAM_MAP( e6400_map )
//	MCFG_CPU_IO_MAP( cpu_io_map )

	MCFG_PALETTE_ADD_BLACK_AND_WHITE("lcd_pal")
	MCFG_SCREEN_ADD("screen", LCD)
	MCFG_SCREEN_REFRESH_RATE(10)
	MCFG_SCREEN_UPDATE_DEVICE("t6963", t6963_device, screen_update)
//	MCFG_SCREEN_SIZE(240, 64)
//	MCFG_SCREEN_VISIBLE_AREA(0, 240-1, 0, 64-1)
	MCFG_SCREEN_PALETTE("lcd_pal")

	MCFG_N82077AA_ADD("fdc", n82077aa_device::MODE_AT)
	MCFG_FLOPPY_DRIVE_ADD("fdc:0", e6400_floppies, "35hd", e6400_state::floppy_formats)

	MCFG_T6963_ADD("t6963", "screen", 240, 64)

MACHINE_CONFIG_END

/*
 * Memory
 * eos30b.raw
 * 000000-000200 -> 000000 vectors
 * 000200-000400 -> ??? Scratch RAM
 * 000400-0EF000 -> 010800
 */

ROM_START( e6400 )
	ROM_REGION32_BE(0x100000, "eos_flash", 0)
	ROM_LOAD( "eos30b.raw", 0x000000, 0x0EF000, CRC(69E5D16E) )
//	ROM_REGION(0x20000, "bankram", ROMREGION_ERASE) // 128kbyte bank RAM
//	ROM_REGION16_LE(0x100000, "waveram", ROMREGION_ERASE) // 512kword 12-bit wave RAM (768kbyte)
ROM_END
//    YEAR,NAME,PARENT,COMPAT,MACHINE,INPUT,     CLASS,INIT, COMPANY,FULLNAME,FLAGS)
CONS( 1996,e6400,     0,     0,   e6400, 0,e6400_state,e6400,"E-mu", "e6400", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
