// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    Skeleton driver for the E-mu E6400 sampler.

    Hardware:
    - MC68EC020 CPU @ 22.5792 MHz (45.1584 MHz audio XTAL ÷2 via D-latch toggle)
    - S82078 / N82077AA FDC controller (24 MHz crystal)
    - Sharp LM24014H LCD unit (T6963C-based, 240x64)
    - MC68901 MFP (MIDI UART, timers, GPIO)
    - AM85C80-16JC SCSI + SCC (Z85C30) controller
    - 2x HM514260 DRAM (256Kx16-bit, total 1 Mbit)
    - IDT7202SO 1Kx9 dual-port FIFO buffer
    - CS8411-CP Digital Audio Interface Receiver (S/PDIF)
    - CS8402A-CP Digital Audio Interface Transmitter (S/PDIF)
    - AMI E-MU IC413 Rev A 9807NMQ 6753-501
    - 4x AD1861 18-bit DAC

    PALs:
    - W5 IP822D (c)EMU '96 9811 - MEM_PAL
    - W5 IP872A (c)EMU '96 9808 - CS_PAL
    - W5 IP751B.1 EMU 1098

    ROM memory map (CPU addresses, confirmed from MAME debugger cross-reference with IDA):
    000000-0001FF: vector table (hardware mirror of ROM[000000-0001FF] via CS_PAL)
    000200-0003FF: scratch RAM
    010400-0FF3FF: flash ROM (eos30b.raw, base CPU 010400)
      010400-0105FF: vector table bytes (also visible at 000000)
      010600-0107FF: 0xFF padding (erased flash)
      010800-0FF3FF: firmware code and data (IDA "eOS_ROM" segment starts here)

    Peripheral address map (active when A23=0, A22=1, A21=0 via CS_PAL ENBIO1):

    Decoder #3, via CNTRLSEL (A14..A16 select):
    400000-403FFF:  CSWCR1 — control register 1 (16-bit write latch, confirmed)
    404000-407FFF:  CSWCR2 — control register 2 (16-bit write latch, confirmed)
    408000-40BFFF:  CSMSR — misc status register (16-bit read, confirmed)
                      bits[15:12]= EEPROMD,FIFOF,FIFOHF,SROMBSY; bits[11:8]= HW variant
    40C000-40FFFF:  CSLED — LED latch (16-bit write; bits 0-11 LEDs, 12-15 LCD contrast)
    414000-417FFF:  CSAESRX — CS8411 AES/EBU digital audio receiver

    Decoder #2 (A20=0, A17..A19 select):
    420000-43FFFF:  CSG1CHIP — G-chip 1 sound engine (polyphony board)
    440000-45FFFF:  CSG2CHIP — G-chip 2 sound engine (polyphony board)
    460000-47FFFF:  CSHCHIP — H-chip digital filter IC413 (+ polyphony connector)
    480000-49FFFF:  CSHDC — AM85C80 SCSI controller (NCR5380-compatible)
    4A0000-4BFFFF:  CSHDD — SCSI DMA data port (via memory PAL IP822)
    4C0000-4DFFFF:  CSSCC — AM85C80 SCC (Z85C30-compatible DUART)
    4E0000-4FFFFF:  CSRFIFO — IDT7202 1Kx9 sampling FIFO buffer (read)

    Decoder #1 (A20=1, A17..A19 select):
    500000-51FFFF:  CSKCHIP — K-chip key scanner IT433 (A0-A3 addr, D[15:8] data)
    520000-53FFFF:  CSDSP — DSP daughter card (effects processor)
    540000-55FFFF:  CSEXP — expansion daughter card (effects DSP RAM at +0x400)
    560000-57FFFF:  CSFDC — 82078 FDC (confirmed)
    580000-59FFFF:  CSLCD — LM24014H LCD / T6963C (confirmed)
    5A0000-5BFFFF:  CSMFP — MC68901 MFP (confirmed)
    5C0000-5DFFFF:  CSWGAIN — sample gain latch (write, D[15:8])
                      bits 0-5 gain, bit 7 BIGEECS (big EEPROM CS)
    5E0000-5FFFFF:  CSRJACK — jack detection latch (read, D[15:8])
                      bits 2-7 jack status

    F00000-FFFFFF:  CPU DRAM (2x HM514260 256Kx16-bit)

    Boot notes:
    - eos30b.raw IS the cold-boot ROM; no separate bootprom exists.
    - reset() at CPU 0x034FD0 runs immediately on power-on via vector alias at 0x000000.
    - reset() copies 0x6000 bytes of API jump table from ROM (0x0F9400) to DRAM (0xF00400)
      then calls bootSystem() which performs all hardware and OS initialization.
    - "RTC" time base is a software counter (timer_value) incremented by MFP timer ISR.
    - Hardware variant from register at 0x408000 bits[11:8] (4 bits from HW, firmware reads [11:9]).

******************************************************************************/

#include "emu.h"

#include "cpu/m68000/m68020.h"
#include "machine/mc68901.h"
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
		, m_mfp(*this, "mfp")
		, m_lcd(*this, "lcd")
		, m_fdc(*this, "fdc")
	{
	}

	void e6400(machine_config &config);

private:
	required_device<m68ec020_device> m_maincpu;
	required_device<mc68901_device> m_mfp;
	required_device<lm24014h_device> m_lcd;
	required_device<n82077aa_device> m_fdc;

	u16 m_cr1 = 0;
	u16 m_cr2 = 0;

	void mem_map(address_map &map) ATTR_COLD;
	void cpuspace_map(address_map &map) ATTR_COLD;

	void cr1_w(u16 data);
	void cr2_w(u16 data);
	u16 status_r();
	u8 lcd_r();
	void lcd_w(offs_t offset, u8 data);
	void gain_w(u8 data);
	u8 jack_r();
};

void e6400_state::cr1_w(u16 data)
{
	m_cr1 = data;
}

void e6400_state::cr2_w(u16 data)
{
	m_cr2 = data;
}

u16 e6400_state::status_r()
{
	// CSMSR — misc status register
	// bits[15:12] = EEPROMD, FIFOF, FIFOHF, SROMBSY
	// bits[11:8]  = hardware variant (4 bits from schematic; firmware reads [11:9] via bfextu)
	// E6400 schematic: bits[11:8] = 0101 → firmware bfextu{20:3} reads bits[11:9] = 010 = 2 → variant_id 4
	return 0x0500; // bits[11:8] = 0b0101 (E6400)
}

u8 e6400_state::lcd_r()
{
	return m_lcd->read(BIT(m_cr1, 8));
}

void e6400_state::lcd_w(offs_t offset, u8 data)
{
	m_lcd->write(BIT(m_cr1, 8), data);
}

void e6400_state::gain_w(u8 data)
{
	// CSWGAIN — sample gain latch (bits 0-5 = gain, bit 7 = big EEPROM CS)
}

u8 e6400_state::jack_r()
{
	// CSRJACK — jack detection latch (bits 2-7 = jack status)
	return 0x00;
}

void e6400_state::mem_map(address_map &map)
{
	map(0x000000, 0x0001ff).rom().region("eos_flash", 0);        // vector table mirror (CS_PAL)
	map(0x000200, 0x0003ff).ram();                                // scratch RAM
	map(0x010400, 0x0ff3ff).rom().region("eos_flash", 0);        // full ROM at its CPU base

	// Decoder #3 (via CNTRLSEL, A14..A16 select) — 0x400000-0x41FFFF
	map(0x400000, 0x400001).w(FUNC(e6400_state::cr1_w));         // CSWCR1 — control register 1
	map(0x404000, 0x404001).w(FUNC(e6400_state::cr2_w));         // CSWCR2 — control register 2
	map(0x408000, 0x408001).r(FUNC(e6400_state::status_r));      // CSMSR — misc status register
	// 0x40C000: CSLED — front panel LED latch + LCD contrast DAC
	// 0x414000: CSAESRX — CS8411 AES/EBU receiver

	// Decoder #2 (A20=0, A17..A19 select) — 0x420000-0x4FFFFF
	// 0x420000: CSG1CHIP — G-chip 1 sound engine (polyphony board)
	// 0x440000: CSG2CHIP — G-chip 2 sound engine (polyphony board)
	// 0x460000: CSHCHIP — H-chip digital filter IC413
	// 0x480000: CSHDC — AM85C80 SCSI controller (NCR5380)
	// 0x4A0000: CSHDD — SCSI DMA data port (via memory PAL IP822)
	// 0x4C0000: CSSCC — AM85C80 SCC (Z85C30 DUART)
	// 0x4E0000: CSRFIFO — IDT7202 sampling FIFO

	// Decoder #1 (A20=1, A17..A19 select) — 0x500000-0x5FFFFF
	// 0x500000: CSKCHIP — K-chip key scanner IT433 (A0-A3, D[15:8])
	// 0x520000: CSDSP — DSP daughter card (effects processor)
	// 0x540000: CSEXP — expansion daughter card
	map(0x560000, 0x560007).m(m_fdc, FUNC(n82077aa_device::map)); // CSFDC
	map(0x580000, 0x580003).rw(FUNC(e6400_state::lcd_r), FUNC(e6400_state::lcd_w)); // CSLCD
	map(0x5a0000, 0x5a002f).rw(m_mfp, FUNC(mc68901_device::read), FUNC(mc68901_device::write)).umask16(0xff00); // CSMFP
	map(0x5c0000, 0x5c0001).w(FUNC(e6400_state::gain_w)).umask16(0xff00);  // CSWGAIN
	map(0x5e0000, 0x5e0001).r(FUNC(e6400_state::jack_r)).umask16(0xff00);  // CSRJACK

	map(0xf00000, 0xffffff).ram(); // 2x HM514260 256Kx16-bit DRAM
}

void e6400_state::cpuspace_map(address_map &map)
{
	// MC68020 CPU space: autovectors for all levels, MFP gets level-6 IACK slot
	// Level N IACK address = 0xfffffff1 + N*2; level 6 → 0xfffffffd
	// TODO: confirm IPL6 from schematic (assumed by analogy with MC68901/VR=0x48 designs)
	map(0xfffffff0, 0xffffffff).m(m_maincpu, FUNC(m68ec020_device::autovectors_map));
	map(0xfffffffd, 0xfffffffd).r(m_mfp, FUNC(mc68901_device::get_vector));
}

static void e6400_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

void e6400_state::e6400(machine_config &config)
{
	M68EC020(config, m_maincpu, 45.1584_MHz_XTAL / 2); // 22.5792 MHz — 45.1584 MHz audio XTAL ÷2 via D-latch toggle
	m_maincpu->set_addrmap(AS_PROGRAM, &e6400_state::mem_map);
	m_maincpu->set_addrmap(m68000_base_device::AS_CPU_SPACE, &e6400_state::cpuspace_map);

	N82077AA(config, m_fdc, 24_MHz_XTAL, n82077aa_device::mode_t::AT);
	FLOPPY_CONNECTOR(config, "fdc:0", e6400_floppies, "35hd", floppy_image_device::default_pc_floppy_formats);

	// MC68901 MFP — MIDI UART, timers (system tick), GPIO
	// Timer clock: 16 MHz XTAL (U56/ZX314) ÷4 via HC393 binary counter Q1 = 4 MHz
	// VR=0x48 → vectors 0x120-0x15F; timer ISR at 0x120 increments software timer
	MC68901(config, m_mfp, 16_MHz_XTAL / 4); // 4 MHz — 16 MHz XTAL ÷4 via HC393 Q1
	m_mfp->set_timer_clock(16_MHz_XTAL / 4);
	m_mfp->out_irq_cb().set_inputline(m_maincpu, M68K_IRQ_6); // IPL6 (assumed; IACK at 0xfffffffd)

	LM24014H(config, m_lcd, 0);
	m_lcd->set_fs(0); // font size 6x8
}

ROM_START( e6400 )
	ROM_REGION32_BE(0x100000, "eos_flash", 0)
	ROM_LOAD( "eos30b.raw", 0x000000, 0x0ef000, CRC(69e5d16e) SHA1(97AE737FCF7E9EA876C3BD3DE9CD568458F448AF) )
ROM_END

} // anonymous namespace


//    YEAR  NAME   PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY  FULLNAME               FLAGS
SYST( 1996, e6400, 0,      0,      e6400,   0,     e6400_state, empty_init, "E-mu",  "E-6400 Sampler", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
