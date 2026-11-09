// license:BSD-3-Clause
// copyright-holders:Carl Lom
/****************************************************************************

    Skeleton driver for the Zoom MS-100BT MultiStomp guitar effects pedal

    Main board D242-MAIN (PCB-0300-03):

    IC1   TMS320C6745 (176-pin HLQFP) under a "ZOOM ZFX-IV" label
    X2    24.000 MHz (DSP OSCIN; the PLL runs it at 300 MHz)
    X1    11.2896 MHz (audio master clock, 256 x 44.1 kHz)
    IC2   EtronTech EM638165TS-7G 4M x 16 SDR SDRAM (8 MB) on EMIFB
    IC4   PMC Pm25LQ032 4 MB SPI NOR flash: boot program, system program
          and presets
    IC9   PMC Pm25LQ032 4 MB SPI NOR flash: flash filesystem holding the
          effect modules (ZDL)
    IC8, IC20  Wolfson WM8782S stereo ADCs (dual inputs)
    IC18  TI PCM5101 stereo DAC
    IC5   Richtek RT9971A PMIC (also drives the LCD backlight)
    IC14  8-pin DFN, unread (possibly the Apple MFi coprocessor on I2C)
    IC15  16-pin TSSOP, unreadable, next to X1
    -     Taiyo Yuden EYSFDCAVE Bluetooth 2.1+EDR module (HCI over UART)
    CN3   128x64 COG LCD with an ST7565-family controller
    CN12A 3 rotary encoders with push switches
    CN5A  5 buttons and 1 LED
    CN1   DSP JTAG, CN5 SPI flash programming header

    There is no USB or MIDI port. MIDI, preset editing and effect
    installation go over Bluetooth (iAP over RFCOMM) from the StompShare
    iOS app.

    Boot: the DSP's ROM bootloader loads the AIS image at the start of IC4
    (the boot/update program, version 1.00). That program then loads the
    system program (version 1.30), the second AIS image at IC4 0x0a0000.
    Both images set up the PLL and EMIFB SDRAM and run from SDRAM and
    L2 RAM under SYS/BIOS 6.

    SPI0 carries both flash chips and the LCD:
    - IC4 is on SPI0_SCS[0], the boot chip select
    - IC9 is on a GPIO chip select (probably GP3[11])
    - the LCD is write-only 4-wire serial: GP5[11] chip select, GP2[5] A0,
      GP4[0] reset. The firmware turns SPI0_SCS[0] into a GPIO and holds it
      high while it writes to the LCD.
    SPI1 reads an 8-bit SPI ADC (battery voltage?) with GP2[10] as chip
    select.
    McASP0 runs the audio codecs, with EDMA3 moving the samples.

    The flash dumps were read from the unit through CN5. They hold the
    owner's presets (IC4) and a Bluetooth pairing record (IC9 PAIR.DAT),
    so they aren't factory-fresh.

    TODO:
    - everything on the TMS320C6745 (core, peripherals, boot ROM)
    - connect the flash chips and the LCD controller to SPI0 and GPIO
    - LCD: the NT7534 device is a stand-in for the ST7565 family (same
      command set); it lacks COM reverse and has the wrong row count
      for this panel
    - inputs: encoders, buttons and footswitch (GPIO bank 1 interrupt)
    - audio: WM8782S, PCM5101 and McASP0
    - battery ADC on SPI1
    - Bluetooth module

****************************************************************************/

#include "emu.h"

#include "cpu/tms320c6x/tms320c6745.h"
#include "machine/generic_spi_flash.h"
#include "video/nt7534.h"

#include "emupal.h"
#include "screen.h"


namespace {

class ms100bt_state : public driver_device
{
public:
	ms100bt_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_flash(*this, { "ic4", "ic9" }),
		m_flash_rom(*this, { "ic4", "ic9" }),
		m_lcdc(*this, "lcdc")
	{
	}

	void ms100bt(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	void main_map(address_map &map) ATTR_COLD;

	required_device<tms320c6745_device> m_maincpu;
	required_device_array<generic_spi_flash_device, 2> m_flash;
	required_memory_region_array<2> m_flash_rom;
	required_device<nt7534_device> m_lcdc;
};


void ms100bt_state::machine_start()
{
	// the dumps are the initial contents; writes are saved as NVRAM
	for (unsigned i = 0; i < 2; i++)
	{
		m_flash[i]->set_rom_ptr(m_flash_rom[i]->base());
		m_flash[i]->set_rom_size(m_flash_rom[i]->bytes());
	}
}


void ms100bt_state::main_map(address_map &map)
{
	map(0xc000'0000, 0xc07f'ffff).ram(); // EMIFB SDRAM
}


void ms100bt_state::ms100bt(machine_config &config)
{
	TMS320C6745(config, m_maincpu, 24_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &ms100bt_state::main_map);

	for (auto &flash : m_flash)
	{
		GENERIC_SPI_FLASH(config, flash);
		// RDID as read from both chips through CN5
		flash->set_jedec_manufacturer(0x7f);
		flash->set_jedec_memtype(0x9d);
		flash->set_jedec_capacity(0x46);
	}

	screen_device &screen(SCREEN(config, "screen"));
	screen.set_lcd();
	screen.set_refresh_hz(60);
	screen.set_size(132, 65);
	screen.set_visarea_full();
	screen.set_screen_update(m_lcdc, FUNC(nt7534_device::screen_update));
	screen.set_palette("palette");

	PALETTE(config, "palette", palette_device::MONOCHROME_INVERTED);

	NT7534(config, m_lcdc);
}


ROM_START(ms100bt)
	// boot program 1.00 at 0x000000, system program 1.30 at 0x0a0000,
	// presets and settings at 0x1b0000-0x1fffff
	ROM_REGION(0x400000, "ic4", 0)
	ROM_LOAD("pm25lq032.ic4", 0x000000, 0x400000, CRC(04474314) SHA1(0f79f7522497da15602c099a4b8c72fbcb8250ba))

	// flash filesystem: effect modules, effect list, Bluetooth pairing
	ROM_REGION(0x400000, "ic9", 0)
	ROM_LOAD("pm25lq032.ic9", 0x000000, 0x400000, CRC(32a00b84) SHA1(3ea63e891f0cc86d1df8461690834bc85c919982))
ROM_END

} // anonymous namespace


SYST(2013, ms100bt, 0, 0, ms100bt, 0, ms100bt_state, empty_init, "Zoom", "MS-100BT MultiStomp", MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
