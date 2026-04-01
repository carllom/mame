// license:BSD-3-Clause
// copyright-holders:
/******************************************************************************

    Skeleton driver for the E-mu E6400 Ultra sampler.

    Hardware:
    - MC68EC020 CPU @ 24 MHz
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

    Peripheral address map (from ROM analysis, all tentative unless noted):
    400000:         CS_PAL chip-select control register 1 (write, confirmed)
    404000:         CS_PAL chip-select control register 2 (write, confirmed)
    408000:         Hardware ID/variant register (read word, bits[11:9], confirmed)
    480000-48000E:  AM85C80 SCSI port — NCR5380-compatible (word-spaced, confirmed
                      from 48000A = reg5 Bus&Status bit6=DRQ)
    4A0000:         NCR5380 DMA data port (confirmed from DMA transfer loop)
    4C0000:         AM85C80 SCC port A — Z85C30 channel A control (tentative)
    500000-50000E:  Unknown device, word-spaced byte registers (tentative; several
                      byte-access patterns seen, referenced in SCSI boot probe)
    540000:         Single word write (variant configuration?) (tentative)
    540400-540407:  Effects DSP RAM probe area (read-write test on boot) (tentative)
    54FF00:         AM85C80 SCC port B — Z85C30 channel B control (tentative, baud
                      rate register WR13 readback-test confirms Z85C30 present)
    560000-560007:  82078 FDC (confirmed)
    580000-580003:  LM24014H LCD / T6963C (confirmed)
    5A0000-5A002F:  MC68901 MFP (confirmed — full init in boot ROM sub_200F2):
                      VR=0x48 (vector base 0x48, MFP vecs at CPU 0x120-0x15F)
                      AER=0x0B, DDR=0x00 (all GPIO inputs)
                      IMRA=IMRB=0xFF, IERA=0xE1, IERB=0xFF
    5C0000:         Single byte write (variant config to unknown chip) (tentative)
    5E0000:         Hardware config register bits[6:1] and bit[0] (tentative)
    F00000-F7FFFF:  CPU DRAM low bank (2x HM514260)
    F80000-FFBFFE:  CPU DRAM high bank
    FFBFFE-FFFFFF:  ISP / stack (top of DRAM, ISP=0xFFBFFE from vector table)

    Boot notes:
    - eos30b.raw IS the cold-boot ROM; no separate bootprom exists.
    - reset() at CPU 0x034FD0 runs immediately on power-on via vector alias at 0x000000.
    - reset() copies 0x6000 bytes of API jump table from ROM (0x0F9400) to DRAM (0xF00400)
      then calls bootSystem() which performs all hardware and OS initialization.
    - "RTC" time base is a software counter (timer_value) incremented by MFP timer ISR.
    - Hardware variants 0-4 decoded from register at 0x408000 bits[11:9].

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

	u16 m_csel = 0;

	void mem_map(address_map &map) ATTR_COLD;
	void cpuspace_map(address_map &map) ATTR_COLD;

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
	map(0x000000, 0x0001ff).rom().region("eos_flash", 0);        // vector table mirror (CS_PAL)
	map(0x000200, 0x0003ff).ram();                                  // scratch RAM
	map(0x010400, 0x0ff3ff).rom().region("eos_flash", 0);        // full ROM at its CPU base

	map(0x400000, 0x400003).w(FUNC(e6400_state::chipsel_w));     // CS_PAL chip-select register
	// 0x404000: second chip-select register
	// 0x408000: hardware ID register (bits[2:0] = variant)

	map(0x560000, 0x560007).m(m_fdc, FUNC(n82077aa_device::map));
	map(0x580000, 0x580003).rw(FUNC(e6400_state::lcd_r), FUNC(e6400_state::lcd_w));

	// MFP registers are byte-wide on even addresses (word-spaced).
	// TODO: verify umask — MFP data likely on D[15:8] (upper byte of 16-bit bus)
	map(0x5a0000, 0x5a002f).rw(m_mfp, FUNC(mc68901_device::read), FUNC(mc68901_device::write)).umask16(0xff00);

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
	M68EC020(config, m_maincpu, 24_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &e6400_state::mem_map);
	m_maincpu->set_addrmap(m68000_base_device::AS_CPU_SPACE, &e6400_state::cpuspace_map);

	N82077AA(config, m_fdc, 24_MHz_XTAL, n82077aa_device::mode_t::AT);
	FLOPPY_CONNECTOR(config, "fdc:0", e6400_floppies, "35hd", floppy_image_device::default_pc_floppy_formats);

	// MC68901 MFP — MIDI UART, timers (system tick), GPIO
	// Clock source: 24 MHz CPU crystal (exact MFP input divider TODO)
	// VR=0x48 → vectors 0x120-0x15F; timer ISR at 0x120 increments software timer
	// TODO: determine which CPU IPL level the MFP IRQ output connects to
	MC68901(config, m_mfp, 24_MHz_XTAL);
	m_mfp->set_timer_clock(24_MHz_XTAL);
	m_mfp->out_irq_cb().set_inputline(m_maincpu, M68K_IRQ_6); // IPL6 (assumed; IACK at 0xfffffffd)

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
