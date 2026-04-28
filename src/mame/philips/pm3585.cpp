// license:GPL-2.0+
// copyright-holders: Carl Lom
/***************************************************************************
  Philips PM-358x logic analyzer skeleton
  "Quattro"

  Variants:
  PM3580/30 - 32 dual channels @100MHz, 50MHz state, 1K aquisition memory
  PM3580/60 - 64 dual channels @100MHz, 50MHz state, 1K aquisition memory
  PM3585/60 - 64 dual channels @200MHz, 50MHz state, 2K aquisition memory
  PM3585/90 - 96 dual channels @200MHz, 50MHz state, 2K aquisition memory

  1.44M Floppy
  Floppy geometry: 80 tracks, 500Kbit/s, 100ms latency, 3ms access, 15ms settling, motor start 500ms, 300rpm

  Centronics printer interface
  RS-232-C for other peripherals

  ### Main CPU (D102) ###
  Motorola M68070 @ 19.6MHz divided by 2 for 9.8MHz bus operation
  Built-in DMA, MMU, I2C, Timer and RS232
  24-bit address bus, 16-bit data bus
  I2C communicates with keyboard and real-time clock chip

  Static RAM (CPU):
  128KB total, accessed as 64Kx16
  4 chips D401,D402,D404,D405, 32KBx8 each
  
  GAL (PLCC 20v8) chip D406 selects SRAM or EPROM chips based on address lines LDSN,UDSN, ADR16-ADR18.
  ADR01 is the first address line (16-bit bus), so ADR16=A17, ADR17=A18, ADR18=A19 (byte addresses).
  Outputs: CSRM1N (lower bank, D0-D7), CSRM2N (lower bank, D8-D15),
           CSRM3N (upper bank, D0-D7), CSRM4N (upper bank, D8-D15), CSROMN.

  2 buffers D303,D306 are used as latches to communicate with peripherals.
  4 buffers D307,D321,D326 are used to communicate with analyzer.
  D303,D306 are enabled by GAL D802 D303 lower byte, D306 upper byte
  D304,D307 are directly connected to an individual ASIC (D1-D12)
  D320,D321 are also connected to ASICS

  EPROM (CPU):
  256KB total accessed as 128Kx16
  2 chips D400, D403, 128KBx8 each
  GAL chip D703 selects EPROM chips

  Preliminary memory map based on schematics and GAL logic:
  00000000-0017FFFF DRAM
  00180000-0019FFFF EPROM (boot PROM)
  001C0000-001DFFFF SRAM (128KB, D401/D402/D404/D405)
  001FFC00-001FFFBF I/O space
  001FFFE0-001FFFFF VDS (Video Display System) control registers
  80000000-8000FFFF CPU peripheral registers (internal)

  I/O space mapping (preliminary):
  001FFC04-001FFC0F FDC (byte registers on odd addresses: DRIVECTL=1ffc05, STATUS=1ffc09, DATA=1ffc0b, RATE_CHG=1ffc0f)
  001FFCC0-001FFCDF MFP (byte registers on odd addresses: GPDR=1ffcc1 AER=1ffcc3 DDR=1ffcc5 IERA=1ffcc7 and so on.)
  001FFFE0-001FFFE5 VDS (word registers on even addresses: CSR=1fffe0, DCR=1fffe2, VSR=1fffe4)

  ### Video System Controller (D101) ###
  SCC66470 @ 30MHz
  - divided by 2 for 15MHz pixel clock
  - divided by 960 for 31,250Hz horizontal sync
  - divided by 500000 for 60Hz vertical sync

  4 bits of video data is clocked to video output VID4-7 on pin 70-73 of the chip.
  4 bits grey scales.

  680XX family display and system controller
  Provides chip select for system rom (D400,D403) CSROM.
  Peripherals enabled by CSIO.
  On-chip DRAM controller interfaces to DRAM chips (D1000-D1007,D1020-D1023).

  Dynamic RAM (VSC):
  1.5MB total, accessed as 768Kx16
  12 chips D1000-D1007, D1020-D1023, 256KBx4 each

  ### FDC ###
  DP8473, selected by GAL D500 pin 26 CSFDCN
  0000-01FF boot sector (0)
  0200-13FF FAT#1 (1-9)
  1400-2400 FAT#2 sector (10-17,0)

  ### IEEE ###
  TMS9914 (chip D800)
  GAL chip D802 pin26 selects CSGPBN, interrupt to CPU on IN5N (pin 9 on D800)
  D801 & D803 are bidirectional buffers fore IEEE signals to and from X800

  ### Centronics parallel controller ###
  M68230

  ### Multi-Function Peripheral ###
  M68901

  ### RTC (chip D110) ###
  PCF8583 Slave address 0xA0

  ### Panel keyboard module ###
  Controlled by 84C01 @ 4MHz
  The panel keyboard communicates with the main CPU via a custom protocol with keycodes and dial values.
  It is possible to emulate, but omit it from driver to start with. Use regular inputs and a handler function dealing with the protocol specifics.
  Button keycode chart in Service Manual, page 3-18.
*/

#include "emu.h"
#include "machine/scc68070.h"
#include "machine/scc66470.h"
#include "machine/mc68901.h"
#include "machine/pcf8583.h"
#include "machine/tms9914.h"
#include "machine/upd765.h"
#include "imagedev/floppy.h"
#include "formats/pc_dsk.h"
#include "screen.h"

namespace {

class pm3585_state : public driver_device
{
public:
	pm3585_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_vsc(*this, "vsc")
		, m_fdc(*this, "fdc")
		, m_mfp(*this, "mfp")
		, m_rtc(*this, "rtc")
		, m_ieee(*this, "ieee")
		, m_screen(*this, "screen")
	{ }

	void pm3585(machine_config &config) ATTR_COLD;

private:
	virtual void machine_reset() override ATTR_COLD;

	void mem_map(address_map &map) ATTR_COLD;
	void vsc_map(address_map &map) ATTR_COLD;

	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);

	void vsc_irq(int state);
	void fdc_drq_w(int state);

	required_device<scc68070_device> m_maincpu;
	required_device<scc66470_device> m_vsc;
	required_device<dp8473_device>   m_fdc;
	required_device<mc68901_device>  m_mfp;
	required_device<pcf8583_device>  m_rtc;
	required_device<tms9914_device>  m_ieee;
	required_device<screen_device>   m_screen;
};


/***************************************************************************
  Machine init
***************************************************************************/

void pm3585_state::machine_reset()
{
	// The SCC66470 maps DRAM at 0x000000. On real hardware it overlays the
	// first four ROM words (reset SP + PC) for the initial fetch, then
	// switches to DRAM. The emulation has no such overlay, so seed DRAM
	// manually — identical to how magicard/cdi solve this.
	uint16_t *const src = (uint16_t *)memregion("maincpu")->base();
	m_vsc->set_vectors(src);
}


/***************************************************************************
  Address maps
***************************************************************************/

void pm3585_state::vsc_map(address_map &map)
{
	// SCC66470 internal address space: backing RAM for dram_r/dram_w
	// 1.5MB DRAM (D1000-D1007, D1020-D1023)
	map(0x000000, 0x17ffff).ram();
}

void pm3585_state::mem_map(address_map &map)
{
	// 00000000-001FFFFF: SCC66470 managed space
	//   00000000-0017FFFF  DRAM (1.5MB, D1000-D1007, D1020-D1023)
	//   001FFC00-001FFFBF  I/O space (gaps unmapped by scc66470_device::map)
	//   001FFFE0-001FFFFF  VDS control registers
	map(0x000000, 0x1fffff).m(m_vsc, FUNC(scc66470_device::map));

	// 00180000-0019FFFF: EPROM boot ROM (D400, D403 — 2x 128KBx8, big-endian 16-bit)
	map(0x180000, 0x19ffff).rom().region("maincpu", 0);

	// 001C0000-001DFFFF: 128KB SRAM (D401, D402, D404, D405 — 4x 32KBx8, word-wide pairs)
	// GAL D406 decodes ADR16(=A17)/ADR17(=A18)/ADR18(=A19) + LDSN/UDSN:
	//   A19=1,A18=1,A17=0 → CSRM1N (D0-D7) / CSRM2N (D8-D15) — 0x1C0000-0x1CFFFF
	//   A19=1,A18=1,A17=1 → CSRM3N (D0-D7) / CSRM4N (D8-D15) — 0x1D0000-0x1DFFFF
	map(0x1c0000, 0x1dffff).ram();

	// 001FFC04-001FFC0F: FDC DP8473 (byte registers at odd addresses)
	//   DRIVECTL=1ffc05  STATUS=1ffc09  DATA=1ffc0b  RATE_CHG=1ffc0f
	map(0x1ffc04, 0x1ffc05).w( m_fdc, FUNC(dp8473_device::dor_w)).umask16(0x00ff);                                // DRIVECTL
	map(0x1ffc08, 0x1ffc09).r( m_fdc, FUNC(dp8473_device::msr_r)).umask16(0x00ff);                                // STATUS
	map(0x1ffc0a, 0x1ffc0b).rw(m_fdc, FUNC(dp8473_device::fifo_r), FUNC(dp8473_device::fifo_w)).umask16(0x00ff); // DATA
	map(0x1ffc0e, 0x1ffc0f).rw(m_fdc, FUNC(dp8473_device::dir_r), FUNC(dp8473_device::ccr_w)).umask16(0x00ff);   // DIR (read) / RATE_CHG (write)

	// TODO: IEEE-488 TMS9914 (D800) and M68230 PIT — addresses TBD within 001FFC10-001FFCBF

	// 001FFCC0-001FFCDF: MFP MC68901 (byte registers at odd addresses)
	//   GPDR=1ffcc1  AER=1ffcc3  DDR=1ffcc5  IERA=1ffcc7 ...
	map(0x1ffcc0, 0x1ffcdf).rw(m_mfp, FUNC(mc68901_device::read), FUNC(mc68901_device::write)).umask16(0x00ff);
}


/***************************************************************************
  Video
***************************************************************************/

uint32_t pm3585_state::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	// PM3585 uses the SCC66470 in 8-bit mode (DCR CM=0). Each DRAM byte holds
	// 4×2-bit pixels clocked out at 30MHz via external demux + resistor DAC.
	// Bits [7:6]=px0, [5:4]=px1, [3:2]=px2, [1:0]=px3  (MSB first)
	// Exact grey levels from resistor DAC voltage divider (+5V, 3.3K series resistor):
	//   00: 4.7K to GND → 2.9375V → 255   01: 2.61K to GND → 2.2081V → 150
	//   10: 3.3K to GND → 2.5000V → 192   11: 1.0K  to GND → 1.1628V →   0
	static constexpr uint32_t k_grey[4] = { 0xffffff, 0x969696, 0xc0c0c0, 0x000000 };

	if (!m_vsc->display_enabled())
		return 0;

	if (cliprect.min_y == cliprect.max_y)
	{
		// VSR is the 20-bit byte address of the first pixel; advances 192 bytes/line
		// (768 physical pixels / 4 pixels-per-byte = 192 bytes).
		// Compute the byte address for this scanline relative to visible top.
		const int vis_top = screen.visible_area().min_y;
		const uint32_t vsr_base = m_vsc->get_vsr() & 0xfffff;
		const uint32_t line_addr = (vsr_base + (cliprect.min_y - vis_top) * 192) & 0xfffff;

		address_space &vram = m_vsc->space(0);
		uint32_t *dest = &bitmap.pix(cliprect.min_y, 0);
		for (int b = 0; b < 192; b++)
		{
			const uint8_t byte = vram.read_byte(line_addr + b);
			*dest++ = k_grey[(byte >> 6) & 3];
			*dest++ = k_grey[(byte >> 4) & 3];
			*dest++ = k_grey[(byte >> 2) & 3];
			*dest++ = k_grey[(byte >> 0) & 3];
		}
	}
	return 0;
}

void pm3585_state::vsc_irq(int state)
{
	m_maincpu->in2_w(state);
}

// FDC DRQ handler: service the SCC68070 DMA one byte at a time so the FDC
// can complete its execution phase and enter the result phase.  The SCC68070
// DMA stub already sets CSR_COC when CCR_SO is written (no real transfer
// engine), so we only need to drain / fill the FIFO here.
void pm3585_state::fdc_drq_w(int state)
{
	if (!state)
		return;

	auto &ch = m_maincpu->dma().channel[0];
	address_space &mem = m_maincpu->space(AS_PROGRAM);

	if (ch.operation_control & SCC68070_OCR_D_D2M)
	{
		// Device → Memory (FDC read): pull one byte from the FDC FIFO and
		// store it at the DMA destination address.
		uint8_t const data = m_fdc->dma_r();
		mem.write_byte(ch.memory_address_counter, data);
	}
	else
	{
		// Memory → Device (FDC write): fetch one byte from the DMA source
		// address and push it into the FDC FIFO.
		uint8_t const data = mem.read_byte(ch.memory_address_counter);
		m_fdc->dma_w(data);
	}

	ch.memory_address_counter++;
	if (ch.transfer_counter > 0)
		ch.transfer_counter--;
}


/***************************************************************************
  Input ports
***************************************************************************/

static INPUT_PORTS_START(pm3585)
	// TODO: panel keyboard inputs (84C01 MCU, custom keycode protocol)
INPUT_PORTS_END


/***************************************************************************
  Floppy types
***************************************************************************/

static void pm3585_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD); // 1.44MB 3.5"
}

static void pm3585_floppy_formats(format_registration &fr)
{
	fr.add(FLOPPY_PC_FORMAT);
}


/***************************************************************************
  Machine config
***************************************************************************/

void pm3585_state::pm3585(machine_config &config)
{
	// D102: Motorola/Philips SCC68070 @ 19.6608MHz (9.8304MHz bus)
	SCC68070(config, m_maincpu, XTAL(19'660'800));
	m_maincpu->set_addrmap(AS_PROGRAM, &pm3585_state::mem_map);
	// TODO: connect I2C callbacks to PCF8583 RTC (slave address 0xA0)

	// Physical display: 30MHz pixel clock, 768×480 visible, 960/521 total (~60Hz).
	// We render 192 DRAM bytes × 4 pixels/byte = 768 px/line directly from DRAM,
	// so the pixel clock for MAME's screen_device is the physical 30MHz.
	// Visible area starts at y=32 to sit inside the SCC66470 PAL-mode scanline
	// window used by its internal timers; lines 32–511 = 480 visible lines.
	SCREEN(config, m_screen, SCREEN_TYPE_RASTER);
	m_screen->set_raw(XTAL(30'000'000), 960, 0, 768, 521, 32, 512);
	m_screen->set_video_attributes(VIDEO_UPDATE_SCANLINE);
	m_screen->set_screen_update(FUNC(pm3585_state::screen_update));

	SCC66470(config, m_vsc, XTAL(30'000'000));
	m_vsc->set_addrmap(0, &pm3585_state::vsc_map);
	m_vsc->set_screen("screen");
	m_vsc->irq().set(FUNC(pm3585_state::vsc_irq));

	// DP8473 FDC — 1.44MB 3.5" floppy, 500Kbit/s MFM
	DP8473(config, m_fdc, XTAL(24'000'000)); // TODO: verify clock source
	m_fdc->intrq_wr_callback().set(m_maincpu, FUNC(scc68070_device::int1_w));
	m_fdc->drq_wr_callback().set(FUNC(pm3585_state::fdc_drq_w));
	FLOPPY_CONNECTOR(config, "fdc:0", pm3585_floppies, "35hd", pm3585_floppy_formats);

	// MFP MC68901 — UART, timers, GPIO, interrupt controller
	MC68901(config, m_mfp, XTAL(19'660'800)); // TODO: verify clock source

	// D110: PCF8583 RTC @ 32.768kHz, I2C address 0xA0
	PCF8583(config, m_rtc, XTAL(32'768));

	// D800: TMS9914 IEEE-488 GPIB controller
	// IRQ -> CPU IN5; address mapping TBD
	TMS9914(config, m_ieee, XTAL(19'660'800)); // TODO: map registers, verify clock, connect IRQ
}


/***************************************************************************
  ROM definitions
***************************************************************************/

ROM_START(pm3585)
	// 2x 128KBx8 EPROM (D400=even/high, D403=odd/low), big-endian 16-bit bus
	ROM_REGION16_BE(0x20000, "maincpu", 0)
	ROM_LOAD16_BYTE("rom04273.bin", 0x00000, 0x10000, CRC(9dbbb5c9) SHA1(24ba2a7afaef857c557c71614e040a6c431d5459)) // D400, even (D8-D15)
	ROM_LOAD16_BYTE("rom04283.bin", 0x00001, 0x10000, CRC(ffe40f5d) SHA1(5d6559b767170d2f94b17f7b42452b993442e5d7)) // D403, odd  (D0-D7)
ROM_END

} // anonymous namespace


SYST( 1989, pm3585, 0, 0, pm3585, pm3585, pm3585_state, empty_init, "Philips", "PM 3585 Logic Analyzer", MACHINE_NOT_WORKING | MACHINE_NO_SOUND_HW )
