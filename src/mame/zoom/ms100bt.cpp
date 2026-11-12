// license:BSD-3-Clause
// copyright-holders:Carl Lom
/****************************************************************************

    Zoom MS-100BT MultiStomp guitar effects pedal

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

    Boot: the DSP's ROM boot loader loads the AIS image at the start of IC4
    (the boot/update program, version 1.00). That program then loads the
    system program (version 1.30), the second AIS image at IC4 0x0a0000.
    Both images set up the PLL and EMIFB SDRAM and run from SDRAM and
    L2 RAM under SYS/BIOS 6.

    SPI0 carries both flash chips and the LCD:
    - IC4 is on SPI0_SCS[0], the boot chip select
    - IC9 is on GP2[7], found by tracing the firmware's file system reads
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
    - the rest of the TMS320C6745 (core, peripherals)
    - GP4[14] is polled with the switches; what drives it is unknown
    - the ON/OFF LED
    - audio: WM8782S, PCM5101 and McASP0
    - battery ADC on SPI1
    - Bluetooth module

****************************************************************************/

#include "emu.h"

#include "cpu/tms320c6x/tms320c6745.h"
#include "machine/generic_spi_flash.h"
#include "video/st7565.h"

#include "emupal.h"
#include "screen.h"

#include <algorithm>


namespace {

class ms100bt_state : public driver_device
{
public:
	ms100bt_state(machine_config const &mconfig, device_type type, char const *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_flash(*this, { "ic4", "ic9" }),
		m_flash_rom(*this, { "ic4", "ic9" }),
		m_lcdc(*this, "lcdc"),
		m_gp_in(*this, "GP%u", 0U),
		m_encoder(*this, "ENC%u", 0U)
	{
	}

	void ms100bt(machine_config &config) ATTR_COLD;

	DECLARE_INPUT_CHANGED_MEMBER(input_changed) { update_inputs(); }

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	// encoder phase pins on GP1
	static constexpr u8 ENCODER_A[3] = { 1, 2, 12 };
	static constexpr u8 ENCODER_B[3] = { 14, 13, 10 };
	static constexpr attotime ENCODER_EDGE_TIME = attotime::from_msec(2);

	void main_map(address_map &map) ATTR_COLD;

	void lcd_palette(palette_device &palette) const ATTR_COLD;

	void update_selects();
	void update_inputs();
	void vblank_w(int state);
	TIMER_CALLBACK_MEMBER(encoder_step);
	u32 spi0_transfer(u32 data, u32 bits);
	u32 spi1_transfer(u32 data, u32 bits);

	// Apple MFi authentication coprocessor on I2C0
	bool mfi_start(u8 address, bool read);
	bool mfi_write(u8 data);
	u8 mfi_read();

	required_device<tms320c6745_device> m_maincpu;
	required_device_array<generic_spi_flash_device, 2> m_flash;
	required_memory_region_array<2> m_flash_rom;
	required_device<st7565_device> m_lcdc;
	required_ioport_array<5> m_gp_in;
	required_ioport_array<3> m_encoder;

	emu_timer *m_encoder_timer = nullptr;

	u8 m_spi0_scs0 = 1;
	bool m_selected[2] = { false, false };
	bool m_lcd_reset = false;
	u8 m_adc_byte = 0;
	bool m_adc_selected = false;
	u8 m_mfi_reg[0x100] = { };
	u8 m_mfi_ptr = 0;
	bool m_mfi_address_phase = false;
	u8 m_dial[3] = { };
	s32 m_steps[3] = { };      // detents still to send, positive means phase A leads
	u8 m_phase[3] = { };       // bit 0 = phase A level, bit 1 = phase B level
	bool m_half_step[3] = { }; // one phase has moved, the other is next
};


void ms100bt_state::machine_start()
{
	// the dumps are the initial contents; writes are saved as NVRAM
	for (u32 i = 0; i < 2; i++)
	{
		m_flash[i]->set_rom_ptr(m_flash_rom[i]->base());
		m_flash[i]->set_rom_size(m_flash_rom[i]->bytes());
	}

	save_item(NAME(m_spi0_scs0));
	save_item(NAME(m_selected));
	save_item(NAME(m_lcd_reset));
	save_item(NAME(m_adc_byte));
	save_item(NAME(m_adc_selected));
	save_item(NAME(m_mfi_reg));
	save_item(NAME(m_mfi_ptr));
	save_item(NAME(m_mfi_address_phase));
	save_item(NAME(m_dial));
	save_item(NAME(m_steps));
	save_item(NAME(m_phase));
	save_item(NAME(m_half_step));

	m_encoder_timer = timer_alloc(FUNC(ms100bt_state::encoder_step), this);

	// device version 2.0C, firmware 1, protocol 2.0, device ID 0x0000'0200
	static constexpr u8 MFI_ID[] = { 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x02, 0x00 };
	std::copy(std::begin(MFI_ID), std::end(MFI_ID), m_mfi_reg);
}

void ms100bt_state::update_selects()
{
	u16 const gp2 = m_maincpu->gpio_output(2);
	u16 const gp4 = m_maincpu->gpio_output(4);
	u16 const gp5 = m_maincpu->gpio_output(5);

	// pin 9 is SPI0_SCS[0] or GP5[4], depending on PINMUX7[31:28]
	bool const ic4 = (BIT(m_maincpu->pinmux(7), 28, 4) == 1) ? !m_spi0_scs0 : !BIT(gp5, 4);
	bool const ic9 = !BIT(gp2, 7);
	bool const selected[2] = { ic4, ic9 };
	for (u32 i = 0; i < 2; i++)
	{
		if (m_selected[i] && !selected[i])
			m_flash[i]->set_ready();
		m_selected[i] = selected[i];
	}

	m_lcd_reset = !BIT(gp4, 0);
	m_lcdc->reset_w(BIT(gp4, 0));

	bool const adc = !BIT(gp2, 10);
	if (adc && !m_adc_selected)
		m_adc_byte = 0;
	m_adc_selected = adc;
}

u32 ms100bt_state::spi0_transfer(u32 data, u32 bits)
{
	update_selects();

	u32 result = 0xff;
	for (u32 i = 0; i < 2; i++)
	{
		if (m_selected[i])
		{
			m_flash[i]->write(u8(data));
			result &= m_flash[i]->read();
		}
	}

	// the LCD is write-only; GP2[5] is A0
	if (!BIT(m_maincpu->gpio_output(5), 11) && !m_lcd_reset)
	{
		if (BIT(m_maincpu->gpio_output(2), 5))
			m_lcdc->data_w(u8(data));
		else
			m_lcdc->control_w(u8(data));
	}
	return result;
}

u32 ms100bt_state::spi1_transfer(u32 data, u32 bits)
{
	// 8-bit ADC (battery voltage?): 3 leading zeros, 8 data bits, then zeros
	// TODO: what it measures and the right value
	update_selects();
	if (!m_adc_selected)
		return 0xff;
	u8 const value = 0xff;
	u8 const result = m_adc_byte ? u8(value << 5) : u8(value >> 3);
	m_adc_byte++;
	return result;
}

/*
    Stand-in for the MFi coprocessor (version 2.0C, I2C address 0x10), with
    the identification registers the firmware checks at boot. The
    challenge and certificate registers need the real chip.
*/
bool ms100bt_state::mfi_start(u8 address, bool read)
{
	if (address != 0x10)
		return false;
	m_mfi_address_phase = !read;
	return true;
}

bool ms100bt_state::mfi_write(u8 data)
{
	if (m_mfi_address_phase)
	{
		m_mfi_ptr = data;
		m_mfi_address_phase = false;
	}
	else
		m_mfi_reg[m_mfi_ptr++] = data;
	return true;
}

u8 ms100bt_state::mfi_read()
{
	return m_mfi_reg[m_mfi_ptr++];
}

void ms100bt_state::machine_reset()
{
	for (u32 i = 0; i < 3; i++)
	{
		m_dial[i] = m_encoder[i]->read();
		m_steps[i] = 0;
		m_phase[i] = 3; // detents sit with both phases equal
		m_half_step[i] = false;
	}
	m_encoder_timer->adjust(attotime::never);
	update_inputs();
}


/*
    Inputs, from the firmware's input code; switches pull low when pressed.
    - the knobs are encoders on GP1 (1/14, 2/13, 12/10), interrupting on
      both edges (bank 1, INT8). The decoder counts when the second phase
      catches up with the first, so a detent is two edges; phase A leading
      counts up.
    - the switches are polled. Knob 2 and 3 pushes (GP1[9], GP1[11]), the
      cursor keys (GP0[4-7]) and GP4[14] make one group; the footswitch
      (GP2[3]) and knob 1 push (GP1[8]) make another with hold timing.
*/
void ms100bt_state::update_inputs()
{
	for (u32 bank = 0; bank < m_gp_in.size(); bank++)
	{
		u16 data = m_gp_in[bank]->read();
		if (bank == 1)
		{
			for (u32 e = 0; e < 3; e++)
			{
				data = (data & ~(1U << ENCODER_A[e])) | (BIT(m_phase[e], 0) << ENCODER_A[e]);
				data = (data & ~(1U << ENCODER_B[e])) | (BIT(m_phase[e], 1) << ENCODER_B[e]);
			}
		}
		m_maincpu->gpio_in_w(bank, data);
	}
}

void ms100bt_state::vblank_w(int state)
{
	if (!state)
		return;

	// turn dial movement into detents for the encoder timer to play out
	for (u32 i = 0; i < 3; i++)
	{
		u8 const dial = m_encoder[i]->read();
		m_steps[i] += s8(dial - m_dial[i]);
		m_dial[i] = dial;
	}
	if ((m_steps[0] || m_steps[1] || m_steps[2]) && m_encoder_timer->remaining().is_never())
		m_encoder_timer->adjust(ENCODER_EDGE_TIME);
}

TIMER_CALLBACK_MEMBER(ms100bt_state::encoder_step)
{
	// one edge per encoder each time, so the firmware sees the phases change in turn
	bool more = false;
	for (u32 i = 0; i < 3; i++)
	{
		if (!m_steps[i])
			continue;
		bool const a_leads = m_steps[i] > 0;
		m_phase[i] ^= (a_leads != m_half_step[i]) ? 1 : 2;
		m_half_step[i] = !m_half_step[i];
		if (!m_half_step[i])
			m_steps[i] += a_leads ? -1 : 1;
		more = more || m_steps[i];
	}
	update_inputs();
	if (more)
		m_encoder_timer->adjust(ENCODER_EDGE_TIME);
}


void ms100bt_state::lcd_palette(palette_device &palette) const
{
	// sampled from a photo of the backlit panel
	palette.set_pen_color(0, rgb_t(0x83, 0xa9, 0x9a)); // background
	palette.set_pen_color(1, rgb_t(0x2d, 0x2f, 0x29)); // dot on, averaged with the gaps between dots
}

void ms100bt_state::main_map(address_map &map)
{
	map(0xc000'0000, 0xc07f'ffff).ram(); // EMIFB SDRAM
}


void ms100bt_state::ms100bt(machine_config &config)
{
	TMS320C6745(config, m_maincpu, 24_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &ms100bt_state::main_map);
	m_maincpu->set_boot_pins(0xfb7d); // BOOTCFG as read on the unit: SPI0 flash boot
	m_maincpu->set_mcasp_ahclk(0, 11.2896_MHz_XTAL); // X1, 256 x 44.1 kHz
	m_maincpu->set_spi_transfer_callback(0, FUNC(ms100bt_state::spi0_transfer));
	m_maincpu->set_spi_transfer_callback(1, FUNC(ms100bt_state::spi1_transfer));
	m_maincpu->set_i2c_start_callback(0, FUNC(ms100bt_state::mfi_start));
	m_maincpu->set_i2c_write_callback(0, FUNC(ms100bt_state::mfi_write));
	m_maincpu->set_i2c_read_callback(0, FUNC(ms100bt_state::mfi_read));
	m_maincpu->spi_scs0(0).set([this] (int state) { m_spi0_scs0 = state; update_selects(); });
	m_maincpu->gpio_out<2>().set([this] (u16 data) { update_selects(); });
	m_maincpu->gpio_out<4>().set([this] (u16 data) { update_selects(); });
	m_maincpu->gpio_out<5>().set([this] (u16 data) { update_selects(); });

	for (auto &flash : m_flash)
	{
		GENERIC_SPI_FLASH(config, flash);
		// RDID as read from both chips through CN5
		flash->set_jedec_manufacturer(0x7f);
		flash->set_jedec_memtype(0x9d);
		flash->set_jedec_capacity(0x46);
	}

	auto &palette(PALETTE(config, "palette", FUNC(ms100bt_state::lcd_palette), 2));

	screen_device &screen(SCREEN(config, "screen"));
	screen.set_lcd();
	screen.set_refresh_hz(60);
	screen.set_size(132, 65);
	screen.set_visarea(0, 127, 0, 63); // the firmware writes columns 4-131 with the segment order reversed
	screen.set_physical_aspect(4, 3); // the panel's dots are 2:3, taller than wide
	screen.set_screen_update(m_lcdc, FUNC(st7565_device::screen_update));
	screen.set_palette(palette);
	screen.screen_vblank().set(FUNC(ms100bt_state::vblank_w));

	ST7565(config, m_lcdc);
}


INPUT_PORTS_START(ms100bt)
	PORT_START("GP0")
	PORT_BIT(0xff0f, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x0010, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)
	PORT_BIT(0x0020, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)
	PORT_BIT(0x0040, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)
	PORT_BIT(0x0080, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Up") PORT_CODE(KEYCODE_UP) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)

	PORT_START("GP1")
	PORT_BIT(0xf4ff, IP_ACTIVE_LOW, IPT_UNUSED) // encoder phases are added by update_inputs()
	PORT_BIT(0x0100, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Knob 1 Push (Menu)") PORT_CODE(KEYCODE_E) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)
	PORT_BIT(0x0200, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Knob 2 Push (Memory)") PORT_CODE(KEYCODE_D) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)
	PORT_BIT(0x0800, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Knob 3 Push (Page)") PORT_CODE(KEYCODE_C) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)

	PORT_START("GP2")
	PORT_BIT(0xfff7, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x0008, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Footswitch") PORT_CODE(KEYCODE_SPACE) PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)

	PORT_START("GP3")
	PORT_BIT(0xffff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("GP4")
	PORT_BIT(0xbfff, IP_ACTIVE_LOW, IPT_UNUSED)
	// polled with the cursor keys, but not a panel control; no visible effect
	PORT_BIT(0x4000, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Unknown (GP4[14])") PORT_CHANGED_MEMBER(DEVICE_SELF, FUNC(ms100bt_state::input_changed), 0)

	PORT_START("ENC0")
	PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_NAME("Knob 1") PORT_SENSITIVITY(50) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_Q) PORT_CODE_INC(KEYCODE_W)

	PORT_START("ENC1")
	PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_NAME("Knob 2") PORT_SENSITIVITY(50) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_A) PORT_CODE_INC(KEYCODE_S) PORT_PLAYER(2)

	PORT_START("ENC2")
	PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_NAME("Knob 3") PORT_SENSITIVITY(50) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_Z) PORT_CODE_INC(KEYCODE_X) PORT_PLAYER(3)
INPUT_PORTS_END


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


SYST(2013, ms100bt, 0, 0, ms100bt, ms100bt, ms100bt_state, empty_init, "Zoom", "MS-100BT MultiStomp", ROT180 | MACHINE_NOT_WORKING | MACHINE_NO_SOUND)
