// license:BSD-3-Clause
// copyright-holders:
/******************************************************************************

    Skeleton driver for the E-mu E6400 Ultra sampler.

    Hardware:
    - MC68EC020 CPU @ 24 MHz
    - S82078 / N82077AA FDC controller (24 MHz crystal)
    - Sharp LM24014H LCD unit (T6963C-based, 240x64)
    - 2x HM514260 DRAM (256Kx16-bit, total 1 Mbit)
    - IDT7202SO 1Kx9 dual-port FIFO buffer
    - AM85C80-16JC SCSI + serial comms controller
    - CS8411-CP Digital Audio Interface Receiver (S/PDIF)
    - CS8402A-CP Digital Audio Interface Transmitter (S/PDIF)
    - AMI E-MU IC413 Rev A 9807NMQ 6753-501
    - 4x AD1861 18-bit DAC

    PALs:
    - W5 IP822D (c)EMU '96 9811 - MEM_PAL
    - W5 IP872A (c)EMU '96 9808 - CS_PAL
    - W5 IP751B.1 EMU 1098

    ROM memory map:
    000000-0001FF: vectors
    000200-0003FF: scratch RAM
    000400-0EF000: code (mapped to CPU address 010800)

******************************************************************************/

#include "emu.h"

#include "cpu/m68000/m68020.h"
#include "machine/upd765.h"
#include "video/t6963c.h"

#include "formats/pc_dsk.h"
#include "imagedev/floppy.h"


namespace {

class e6400_state : public driver_device
{
public:
	e6400_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_lcd(*this, "lcd")
		, m_fdc(*this, "fdc")
	{
	}

	void e6400(machine_config &config);

private:
	required_device<cpu_device> m_maincpu;
	required_device<lm24014h_device> m_lcd;
	required_device<n82077aa_device> m_fdc;

	u16 m_csel = 0;

	void mem_map(address_map &map) ATTR_COLD;

	void chipsel_w(offs_t offset, u16 data);
	u8 lcd_r();
	void lcd_w(offs_t offset, u8 data);
};

void e6400_state::chipsel_w(offs_t offset, u16 data)
{
	m_csel = data;
}

u8 e6400_state::lcd_r()
{
	return m_lcd->read(BIT(m_csel, 8));
}

void e6400_state::lcd_w(offs_t offset, u8 data)
{
	m_lcd->write(BIT(m_csel, 8), data);
}

void e6400_state::mem_map(address_map &map)
{
	map(0x000000, 0x0001ff).rom().region("eos_flash", 0);
	map(0x000200, 0x0003ff).ram();
	map(0x010800, 0x0ff3ff).rom().region("eos_flash", 0x400);

	map(0x400000, 0x400003).w(FUNC(e6400_state::chipsel_w));

	map(0x560000, 0x560007).m(m_fdc, FUNC(n82077aa_device::map));
	map(0x580000, 0x580003).rw(FUNC(e6400_state::lcd_r), FUNC(e6400_state::lcd_w));

	// 0x5a0000: ISR

	map(0xf00000, 0xffffff).ram(); // 2x HM514260 256Kx16-bit DRAM
}

static void e6400_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

void e6400_state::e6400(machine_config &config)
{
	M68EC020(config, m_maincpu, 24_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &e6400_state::mem_map);

	N82077AA(config, m_fdc, 24_MHz_XTAL, n82077aa_device::mode_t::AT);
	FLOPPY_CONNECTOR(config, "fdc:0", e6400_floppies, "35hd", floppy_image_device::default_pc_floppy_formats);

	LM24014H(config, m_lcd, 0);
	m_lcd->set_fs(1); // font size 6x8
}

ROM_START( e6400 )
	ROM_REGION32_BE(0x100000, "eos_flash", 0)
	ROM_LOAD( "eos30b.raw", 0x000000, 0x0ef000, CRC(69e5d16e) SHA1(97AE737FCF7E9EA876C3BD3DE9CD568458F448AF) )
ROM_END

} // anonymous namespace


//    YEAR  NAME   PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY  FULLNAME               FLAGS
SYST( 1996, e6400, 0,      0,      e6400,   0,     e6400_state, empty_init, "E-mu",  "E-6400 Sampler", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
