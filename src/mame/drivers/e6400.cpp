#include "emu.h"

#include "cpu/m68000/m68000.h"

// hardware
#include "machine/upd765.h"

// devices and buses
#include "formats/pc_dsk.h"
#include "imagedev/floppy.h"

// display
#include "video/t6963c.h"
#include "emupal.h"

//#include "bus/midi/midiinport.h"
//#include "bus/midi/midioutport.h"

#define XTAL_24MHz 24000000
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
		m_lcd(*this, "lcd"),
		m_fdc(*this, "fdc"),
		m_csel(0)
	{
	}
	void e6400(machine_config &config);

private:
	required_device<cpu_device> m_maincpu;
	required_device<lm24014h_device> m_lcd;
	optional_device<n82077aa_device> m_fdc;

	int m_csel; // Chip select register

	void mem_map(address_map &map);
	void lcd_map(address_map &map); // not sure yet what to do with this

	void chipsel_w(offs_t offset, uint16_t data);
	uint8_t t696x_r();
	void t696x_w(offs_t offset, uint8_t data);

	// DECLARE_FLOPPY_FORMATS( floppy_formats );

	virtual void machine_reset() override;
	virtual void machine_start() override;
};

void e6400_state::machine_reset()
{
	m_maincpu->reset();
}

void e6400_state::machine_start()
{
}

void e6400_state::chipsel_w(offs_t offset, uint16_t data)
{
	m_csel = data;
}

uint8_t e6400_state::t696x_r()
{
	if (m_csel & 0x100)
	{
		return m_lcd->read(1); // Status
	}
	else
	{
		return m_lcd->read(0); // Data
	}
}

void e6400_state::t696x_w(offs_t offset, uint8_t data)
{
	if (m_csel & 0x100)
	{
		m_lcd->write(1, data); // Command
	}
	else
	{
		m_lcd->write(0, data); // Data
	}
}

void e6400_state::mem_map(address_map &map) {
// static ADDRESS_MAP_START( mem_map, AS_PROGRAM, 32, e6400_state )

	map(0x000000, 0x0001FF).rom().region("eos_flash", 0);
	map(0x000200, 0x0003FF).ram();
	map(0x010800, 0x0FF3FF).rom().region("eos_flash", 0x400);

	map(0x400000, 0x400003).w(FUNC(e6400_state::chipsel_w));

	map(0x560000, 0x560007).m(m_fdc, FUNC(n82077aa_device::map));
	map(0x580000, 0x580003).rw(FUNC(e6400_state::t696x_r), FUNC(e6400_state::t696x_w));

	// 0x5A0000 - isr

	map(0xF00000, 0xFFFFFF).ram(); // 2x256k 16 bit DRAM
//	AM_RANGE(0xF00400, 0xF063FF) AM_RAM // RAM copy of flash data portion
//	AM_RANGE(0xFF0000, 0xFFBFFF) AM_RAM // Stack from FFC000 down
}

void e6400_state::lcd_map(address_map &map) {
	map(0x0000, 0x1fff).ram();
}

static void e6400_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

void e6400_state::e6400(machine_config &config)
{
	M68EC020(config, m_maincpu, XTAL_24MHz );
	m_maincpu->set_addrmap(AS_PROGRAM, &e6400_state::mem_map);

	N82077AA(config, m_fdc, XTAL_24MHz, n82077aa_device::mode_t::AT);
	// m_fdc->intrq_wr_callback().set(FUNC(next_state::fdc_irq));
	// m_fdc->drq_wr_callback().set(FUNC(next_state::fdc_drq));
	FLOPPY_CONNECTOR(config, "fdc:0", e6400_floppies, "35hd", floppy_image_device::default_pc_floppy_formats);

	LM24014H(config, m_lcd, 0);
	m_lcd->set_fs(1); // font size 6x8
}

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
	ROM_REGION(0x400, "lcdc:cgrom", ROMREGION_ERASE00) // chargen data
ROM_END
//    YEAR,NAME,PARENT,COMPAT,MACHINE,INPUT,     CLASS,INIT, COMPANY,FULLNAME,FLAGS)
SYST( 1996,e6400,     0,     0,   e6400, 0,e6400_state, empty_init,"E-mu", "e6400", MACHINE_NOT_WORKING|MACHINE_NO_SOUND)
