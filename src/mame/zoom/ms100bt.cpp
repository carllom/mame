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

#include "multibyte.h"

#include <algorithm>
#include <string>

#define LOG_AIS (1U << 1) // HLE boot loader commands

#define VERBOSE (0)
#include "logmacro.h"


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
	virtual void machine_reset() override ATTR_COLD;

private:
	void main_map(address_map &map) ATTR_COLD;

	void hle_ais_boot() ATTR_COLD;

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

void ms100bt_state::machine_reset()
{
	hle_ais_boot();
}


/*
    Stand-in for the TMS320C6745 ROM boot loader until it's dumped
    (SPRAB04G "Using the OMAP-L1x7 Bootloader").

    Assumes the BOOT straps select SPI0 flash boot. Runs the AIS image at
    the start of the boot flash (IC4) the way the ROM would, then starts
    the DSP at the Jump & Close entry point. The ROM also sets up pin
    multiplexing, PSC and SPI0 for the boot peripheral and restores some of
    that on Jump & Close; none of that is reproduced.
*/
void ms100bt_state::hle_ais_boot()
{
	enum : u32
	{
		AIS_MAGIC = 0x4150'4954,

		AIS_SECTION_LOAD = 0x5853'5901,
		AIS_REQUEST_CRC = 0x5853'5902,
		AIS_ENABLE_CRC = 0x5853'5903,
		AIS_DISABLE_CRC = 0x5853'5904,
		AIS_JUMP = 0x5853'5905,
		AIS_JUMP_CLOSE = 0x5853'5906,
		AIS_SET = 0x5853'5907,
		AIS_SECTION_FILL = 0x5853'590a,
		AIS_FUNCTION_EXEC = 0x5853'590d,
		AIS_SEQ_READ_ENABLE = 0x5853'5963
	};

	static char const *const FUNCTION_NAMES[] =
	{
		"PLL configuration", "clock configuration", "EMIFB SDRAM configuration",
		"EMIFA SDRAM configuration", "EMIFA CE space configuration",
		"PLL and clock configuration", "PSC configuration", "pinmux configuration"
	};

	u8 const *const ais = m_flash_rom[0]->base();
	u32 const length = m_flash_rom[0]->bytes();
	address_space &space = m_maincpu->space(AS_PROGRAM);

	u32 pos = 0;
	auto const fetch =
			[ais, length, &pos] (u32 &word) -> bool
			{
				if ((length - pos) < 4)
					return false;
				word = get_u32le(&ais[pos]);
				pos += 4;
				return true;
			};

	u32 word;
	if (!fetch(word) || (word != AIS_MAGIC))
	{
		logerror("AIS: no boot image in boot flash\n");
		return;
	}

	u32 op;
	while (fetch(op))
	{
		switch (op)
		{
		case AIS_SECTION_LOAD:
			{
				u32 address, size;
				if (!fetch(address) || !fetch(size) || (size > (length - pos)))
					break;
				LOGMASKED(LOG_AIS, "AIS: section load %08x-%08x\n", address, address + size - 1);
				for (u32 i = 0; i < size; i++)
					space.write_byte(address + i, ais[pos + i]);
				pos += (size + 3) & ~u32(3);
			}
			continue;

		case AIS_SECTION_FILL:
			{
				u32 address, size, type, pattern;
				if (!fetch(address) || !fetch(size) || !fetch(type) || !fetch(pattern))
					break;
				LOGMASKED(LOG_AIS, "AIS: section fill %08x-%08x type %u pattern %08x\n", address, address + size - 1, type, pattern);
				switch (type)
				{
				case 0:
					for (u32 i = 0; i < size; i++)
						space.write_byte(address + i, u8(pattern));
					break;
				case 1:
					for (u32 i = 0; i < size; i += 2)
						space.write_word(address + i, u16(pattern));
					break;
				default:
					for (u32 i = 0; i < size; i += 4)
						space.write_dword(address + i, pattern);
					break;
				}
			}
			continue;

		case AIS_SET:
			{
				u32 type, address, data, sleep;
				if (!fetch(type) || !fetch(address) || !fetch(data) || !fetch(sleep))
					break;
				LOGMASKED(LOG_AIS, "AIS: set type %06x %08x = %08x\n", type, address, data);
				switch (type & 0xff)
				{
				case 0:
					space.write_byte(address, u8(data));
					break;
				case 1:
					space.write_word(address, u16(data));
					break;
				case 2:
					space.write_dword(address, data);
					break;
				case 3:
				case 4:
					{
						// field from bit START to bit STOP, preserving the rest
						unsigned const start = BIT(type, 8, 5);
						unsigned const stop = std::max<unsigned>(BIT(type, 16, 5), start);
						u32 const mask = make_bitmask<u32>(stop - start + 1) << start;
						u32 const old = space.read_dword(address);
						space.write_dword(address, (old & ~mask) | ((data << start) & mask));
					}
					break;
				default:
					logerror("AIS: set with reserved type %06x\n", type);
					break;
				}
			}
			continue;

		case AIS_FUNCTION_EXEC:
			{
				u32 header;
				if (!fetch(header))
					break;
				unsigned const index = header & 0xffff;
				unsigned const count = header >> 16;
				std::string args;
				for (unsigned i = 0; i < count; i++)
				{
					u32 arg;
					if (!fetch(arg))
						break;
					args += util::string_format(" %08x", arg);
				}
				// TODO: do what the ROM function does once the PLL, EMIFB and PSC exist
				LOGMASKED(LOG_AIS, "AIS: function %u (%s):%s\n",
						index, (index < std::size(FUNCTION_NAMES)) ? FUNCTION_NAMES[index] : "unknown", args);
			}
			continue;

		case AIS_SEQ_READ_ENABLE:
		case AIS_ENABLE_CRC:
		case AIS_DISABLE_CRC:
			LOGMASKED(LOG_AIS, "AIS: %08x ignored\n", op);
			continue;

		case AIS_REQUEST_CRC:
			// the CRC isn't calculated, so take every check as a match
			if (!fetch(word) || !fetch(word))
				break;
			continue;

		case AIS_JUMP_CLOSE:
			if (!fetch(word))
				break;
			LOGMASKED(LOG_AIS, "AIS: jump and close to %08x\n", word);
			m_maincpu->set_state_int(STATE_GENPC, word);
			return;

		case AIS_JUMP:
			// would need the DSP to run the code and return to the loader
			if (fetch(word))
				logerror("AIS: jump to %08x not supported\n", word);
			return;

		default:
			logerror("AIS: unknown command %08x at %06x\n", op, pos - 4);
			return;
		}

		break; // a command ran past the end of the flash
	}

	logerror("AIS: boot image ends without Jump & Close\n");
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
