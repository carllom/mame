// license:BSD-3-Clause
// copyright-holders:AJR
/****************************************************************************

    Skeleton driver for Roland S-50 and related samplers.

****************************************************************************/

#include "emu.h"
#include "bu3905.h"
#include "sa16.h"
#include "mb63h149.h"

#include "formats/roland_dsk.h"

//#include "bus/midi/midi.h"
#include "bus/nscsi/cd.h"
#include "bus/nscsi/hd.h"
#include "cpu/mcs96/i8x9x.h"
#include "imagedev/floppy.h"
#include "machine/mb87030.h"
#include "machine/nscsi_bus.h"
#include "machine/timer.h"
#include "machine/wd_fdc.h"
#include "video/hd44780.h"
#include "video/tms3556.h"
#include "video/t6963c.h"
#include "emupal.h"
#include "screen.h"

#include "s330.lh"

namespace {

class roland_s50_base_state : public driver_device
{
public:
	roland_s50_base_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_common_ram(*this, "common")
		, m_fdc(*this, "fdc")
		, m_floppy(*this, "fdc:%u", 0U)
		, m_vdp(*this, "vdp")
		, m_wave(*this, "wave")
		, m_keyscan(*this, "keyscan")
		, m_floppy_select(0)
	{
	}

protected:
	u8 floppy_status_r();
	u8 floppy_unknown_r();
	u16 key_r(offs_t offset);
	void key_w(offs_t offset, u16 data);

	TIMER_DEVICE_CALLBACK_MEMBER(vdp_timer);

	void vram_map(address_map &map) ATTR_COLD;

protected:
	required_device<i8x9x_device> m_maincpu;
	required_memory_bank m_common_ram;
	required_device<wd_fdc_digital_device_base> m_fdc;
	required_device_array<floppy_connector, 2> m_floppy;
	optional_device<tms3556_device> m_vdp;
	required_device<sa16_base_device> m_wave;
	optional_device<mb63h149_device> m_keyscan;

	u8 m_floppy_select;
};

class roland_s50_state : public roland_s50_base_state
{
public:
	roland_s50_state(const machine_config &mconfig, device_type type, const char *tag)
		: roland_s50_base_state(mconfig, type, tag)
		, m_sram_bank(*this, "sram")
		, m_sram(*this, "sram", 0x10000U, ENDIANNESS_LITTLE)
		, m_io_view(*this, "io")
	{
	}

	void s50(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	void p2_w(u8 data);

	
	void floppy_select_w(u8 data); 

	void sram_map(address_map &map);

private:
	void ioga_out_w(u8 data);

	void mem_map(address_map &map) ATTR_COLD;

protected:
	required_memory_bank m_sram_bank;
	memory_share_creator<u16> m_sram;
	memory_view m_io_view;
};

class roland_s550_state : public roland_s50_state
{
public:
	roland_s550_state(const machine_config &mconfig, device_type type, const char *tag)
		: roland_s50_state(mconfig, type, tag)
		, m_lowram_bank(*this, "lowram")
		, m_lowmem_view(*this, "lowmem")
		, m_lowram(*this, "lowram", 0x10000U, ENDIANNESS_LITTLE)
	{
	}

	void s550(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	void sram_bank_w(u8 data);

	void mem_map(address_map &map) ATTR_COLD;

	required_memory_bank m_lowram_bank;
	memory_view m_lowmem_view;
	memory_share_creator<u16> m_lowram;
};

class roland_w30_state : public roland_s50_base_state
{
public:
	roland_w30_state(const machine_config &mconfig, device_type type, const char *tag)
		: roland_s50_base_state(mconfig, type, tag)
		, m_bank1_view(*this, "bank1")
		, m_bank2_view(*this, "bank2")
		, m_psram1_bank(*this, "psram1")
		, m_psram2_bank(*this, "psram2")
		, m_psram(*this, "psram", 0x20000U, ENDIANNESS_LITTLE)
		, m_psram_bank(0)
		, m_keysw(*this, "KEYSW%u", 0U)
		, m_keyrow(0)
		, m_leds(*this, "LED%u", 0U)
	{
	}

	void w30(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

	u8 psram_bank_r();
	void psram_bank_w(u8 data);
	void floppy_select_w(u8 data);
	u8 unknown_status_r();

	void w30_mem_map(address_map &map) ATTR_COLD;
	void psram1_map(address_map &map) ATTR_COLD;
	void psram2_map(address_map &map) ATTR_COLD;

	memory_view m_bank1_view;
	memory_view m_bank2_view;
	required_memory_bank m_psram1_bank;
	required_memory_bank m_psram2_bank;
	memory_share_creator<u16> m_psram;

	u8 keysw_r();
	void keysw_w(u8 data);
	void leds_w(u8 data);

	u8 m_psram_bank;

	// Inputs/outputs
	required_ioport_array<4> m_keysw;
	u8 m_keyrow; // M60013: Internal counter for the SCANn outputs
	output_finder<8> m_leds;
private:
	void mem_map(address_map &map);

};

class roland_s330_state : public roland_w30_state
{
public:
	roland_s330_state(const machine_config &mconfig, device_type type, const char *tag)
		: roland_w30_state(mconfig, type, tag)
		, m_lcdc(*this, "lcdc")
		, m_ctrltype(*this, "CTRLTYPE")
		, m_boot_inject_done(false)
	{
	}

	void s330(machine_config &config);
	
	u16 analog_vol_ctrl();
	u16 analog_dac_value();

protected:
	virtual void machine_reset() override ATTR_COLD;
	TIMER_DEVICE_CALLBACK_MEMBER(vdp_timer);
	void s330_mem_map(address_map &map) ATTR_COLD;
	void waveram_map(address_map &map);
	void psram_bank_w(u8 data);
	u8 keysw_r();

	HD44780_PIXEL_UPDATE(lcd_pixel_update);
	void init_lcd_palette(palette_device &palette) const;
	void init_vdp_palette(palette_device &palette) const;

	required_device<hd44780_device> m_lcdc;
	required_ioport m_ctrltype;
	bool m_boot_inject_done; // true after first boot-scan row 1 has been served

	u8 floppy_unknown_r();
	u16 key_r(offs_t offset);
	void key_w(offs_t offset, u16 data);
	
private:
	void mem_map(address_map &map);
};

void roland_s50_state::machine_start()
{
	floppy_select_w(0);
	m_fdc->dden_w(0);

	m_sram_bank->configure_entries(0, 4, &m_sram[0], 0x4000);
	m_sram_bank->set_entry(0);
	m_common_ram->set_base(&m_sram[0]);

	save_item(NAME(m_floppy_select));
}

void roland_s550_state::machine_start()
{
	roland_s50_state::machine_start();

	m_lowram_bank->configure_entries(0, 8, &m_lowram[0], 0x2000);
	m_lowram_bank->set_entry(0);
}

void roland_s50_state::machine_reset()
{
	floppy_select_w(0);
}

void roland_w30_state::machine_start()
{
	m_fdc->dden_w(0);

	m_psram1_bank->configure_entries(0, 8, &m_psram[0x10000 / 2], 0x2000);
	m_psram2_bank->configure_entries(0, 4, &m_psram[0], 0x4000);
	m_common_ram->set_base(&m_psram[0]);

	save_item(NAME(m_psram_bank));
	m_leds.resolve();
	save_item(NAME(m_floppy_select));
}

void roland_w30_state::machine_reset()
{
	floppy_select_w(0);
	psram_bank_w(0);
}

void roland_s330_state::machine_reset()
{
	roland_w30_state::machine_reset();
	m_boot_inject_done = false; // allow boot-scan injection on next reset/power-on
}

TIMER_DEVICE_CALLBACK_MEMBER(roland_s50_base_state::vdp_timer)
{
	// FIXME: internalize this ridiculousness
	m_vdp->interrupt();
}

// SA-16 ENVINT (HSI0) is normally pulsed by the wave chip at the audio frame rate to
// release the firmware's voice-processing spin loop (15F2: jbc hsi_status, 1, 1584).
// Since SA-16 synthesis is not yet emulated, assert HSI0 on every VDP scanline so
// the voice loop exits promptly and the main UI loop (keyscan, panel keys etc.) runs.
TIMER_DEVICE_CALLBACK_MEMBER(roland_s330_state::vdp_timer)
{
	roland_s50_base_state::vdp_timer(timer, param);
	m_maincpu->set_input_line(i8x9x_device::HSI0_LINE, ASSERT_LINE);
}void roland_s50_state::p2_w(u8 data)
{
	m_io_view.select(BIT(data, 5));
}

void roland_s50_state::ioga_out_w(u8 data)
{
	m_sram_bank->set_entry(BIT(data, 6, 2));
}

void roland_s550_state::sram_bank_w(u8 data)
{
	m_sram_bank->set_entry(BIT(data, 0, 2));
	m_lowram_bank->set_entry(BIT(data, 2, 3));
	m_lowmem_view.select(BIT(data, 2, 3) == 0 ? 0 : 1);
}

u8 roland_w30_state::psram_bank_r()
{
	return m_psram_bank;
}

void roland_w30_state::psram_bank_w(u8 data)
{
	m_psram_bank = data;
	m_bank1_view.select(BIT(data, 3, 4) == 0 ? 0 : 1);
	m_bank2_view.select(BIT(data, 0, 3) == 0 ? 0 : 1);
	m_psram1_bank->set_entry(BIT(data, 3, 3));
	m_psram2_bank->set_entry(BIT(data, 0, 2));
}

// S-330 bank switching (C600 write)
//
// C600 format: -AAAA-BB
//   AAAA (bits 6:3) = LoBank: selects 8K overlay at 0100-1FFF
//                    0 = ROM, 1-7 = banked RAM overlays
//   BB   (bits 1:0) = HiBank: selects 16K chunk at 8000-BFFF
//   bit 2 (BC pin)  = not connected
void roland_s330_state::psram_bank_w(u8 data)
{
	m_psram_bank = data;
	const u8 lobank = BIT(data, 3, 4); // bits 6:3
	const u8 hibank = BIT(data, 0, 2); // bits 1:0

	if (lobank == 0)
		m_bank1_view.select(0); // ROM at 0000-1FFF (boot context)
	else
	{
		m_bank1_view.select(1); // RAM overlay
		m_psram1_bank->set_entry((lobank - 1) & 7);
	}

	// 8000-BFFF is always banked RAM on S-330 (no ROM mirrored there)
	m_bank2_view.select(1);
	m_psram2_bank->set_entry(hibank & 3);
}

u8 roland_s50_base_state::floppy_status_r()
{
	floppy_image_device *floppy = nullptr;
	if (BIT(m_floppy_select, 2))
		floppy = m_floppy[0]->get_device();
	else if (BIT(m_floppy_select, 3))
		floppy = m_floppy[1]->get_device();

	u8 status = m_fdc->intrq_r() << 2 | m_fdc->drq_r() << 3;
	if (floppy)
	{
		if (!floppy->ready_r())
			status |= 1;
		if (!floppy->dskchg_r())
			status |= 2;
	}
	return status;
}

void roland_s50_state::floppy_select_w(u8 data)
{
	floppy_image_device *floppy = nullptr;
	if (BIT(data, 6))
		floppy = m_floppy[0]->get_device();
	else if (BIT(data, 7))
		floppy = m_floppy[1]->get_device();

	m_fdc->set_floppy(floppy);
	if (floppy)
		floppy->ss_w(BIT(data, 4));

	m_floppy_select = data >> 4;
}

void roland_w30_state::floppy_select_w(u8 data)
{
	floppy_image_device *floppy = nullptr;
	if (BIT(data, 2))
		floppy = m_floppy[0]->get_device();
	else if (BIT(data, 3))
		floppy = m_floppy[1]->get_device();

	m_fdc->set_floppy(floppy);
	if (floppy)
		floppy->ss_w(BIT(data, 0));

	m_floppy_select = data;
}

u8 roland_s50_base_state::floppy_unknown_r()
{
	return 1;
}

u16 roland_s50_base_state::key_r(offs_t offset)
{
	return m_keyscan->read(offset) << 1;
}

void roland_s50_base_state::key_w(offs_t offset, u16 data)
{
	m_keyscan->write(offset, data >> 1);
}

u8 roland_w30_state::unknown_status_r()
{
	return 0x1c;
}

// Keyswitch read register (M60013)
//
// Repeated reads from this register will read from consecutive keyswitch rows
u8 roland_w30_state::keysw_r()
{
	u8 value = m_keysw[m_keyrow]->read();
	m_keyrow = (m_keyrow + 1) % 4;
	return value;
}

// S-330 boot controller selection override.
//
// On real hardware the user holds Left/Down/Right before power-on to select
// None/Mouse/RC-100. MAME keyboard input has at least one frame of latency so
// the held key never reaches the port in time. This override injects the
// appropriate key value into the first boot scan (row 1 only) based on the
// "Boot controller type" configuration DIP, unless the user is actually holding
// a key on row 1 themselves.
u8 roland_s330_state::keysw_r()
{
	u8 row = m_keyrow; // peek before parent increments
	u8 value = roland_w30_state::keysw_r();

	if (!m_boot_inject_done && row == 1)
	{
		m_boot_inject_done = true;
		if (value == 0xff) // no real key held — apply DIP setting
		{
			switch (m_ctrltype->read() & 0x03)
			{
			case 0: value = 0xfb; break; // Left  = None (panel keys)
			case 2: value = 0xef; break; // Right = RC-100
			// case 1: Mouse — leave as 0xff (firmware defaults to mouse anyway)
			}
			if (value != 0xff)
				logerror("keysw_r: boot inject row 1 => %02x (ctrltype=%d)\n",
					value, m_ctrltype->read() & 0x03);
		}
	}
	return value;
}

// Keyswitch command register (M60013)
//
// A write to this register will reset the keyscan counter
void roland_w30_state::keysw_w(u8 data)
{
	if (data) logerror("KEYPORT Write: %02x\n", data);
	m_keyrow = 0; // Reset keyscan row counter
}

// LED indicator register (M60013)
//
// There are 8 indicator outputs, each corresponding to one bit in the register.
// A set bit turns LED off, an unset bit turns LED on.
void roland_w30_state::leds_w(u8 data)
{
	if (data) logerror("LEDS Write: %02x\n", data);
	m_leds[0] = BIT(data, 0) ? 0 : 1;
	m_leds[1] = BIT(data, 1) ? 0 : 1;
	m_leds[2] = BIT(data, 2) ? 0 : 1;
	m_leds[3] = BIT(data, 3) ? 0 : 1;
	m_leds[4] = BIT(data, 4) ? 0 : 1;
	m_leds[5] = BIT(data, 5) ? 0 : 1;
	m_leds[6] = BIT(data, 6) ? 0 : 1;
	m_leds[7] = BIT(data, 7) ? 0 : 1;
}

void roland_s330_state::init_lcd_palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(131, 136, 139));
	palette.set_pen_color(1, rgb_t( 92,  83,  88));
}

// TMS3556 attribute byte encodes color as bit0=R, bit1=G, bit2=B (confirmed from
// legacy s330.cpp and bitmap plane order: name_r→bit0, name_g→bit1, name_b→bit2).
// Standard RGB_3BIT would give bit2=R, swapping red and blue — use explicit table.
// Use pal1bit() (0 or 255) so that black (index 0) is true black, not dark gray.
void roland_s330_state::init_vdp_palette(palette_device &palette) const
{
	for (int i = 0; i < 8; i++)
		palette.set_pen_color(i,
			pal1bit(i >> 0),  // R = bit0
			pal1bit(i >> 1),  // G = bit1
			pal1bit(i >> 2)); // B = bit2
}

HD44780_PIXEL_UPDATE(roland_s330_state::lcd_pixel_update)
{
	if (x < 5 && y < 8 && line < 2 && pos < 16)
		bitmap.pix(line * 8 + y, pos * 6 + x) = state;
}

u16 roland_s330_state::analog_vol_ctrl()
{
	return 0x1FF; // TODO: hookup to dial (10 bit value)
}

u16 roland_s330_state::analog_dac_value()
{
	// find_neg_sample (4946) polls ACH7 1022× and checks ADC_MSB (R29 = v>>2)
	// against a threshold that alternates by iteration phase (R32):
	//   R32==1: cmpb R29,#7F + jh → needs R29 <= 0x7F → v <= 0x1FF
	//   R32!=1: cmpb R29,#80 + jnh → needs R29 >= 0x81 → v >= 0x204
	// R29==0x80 (v=0x200..0x203) always fails both — never use those values.
	// TODO: replace with real SA-16 DAC output once wave chip output is emulated.
	const u8 r32 = m_maincpu->space(AS_DATA).read_byte(0x32);
	return (r32 == 1) ? 0x1FF : 0x204;
}

void roland_s50_state::mem_map(address_map &map)
{
	map(0x0000, 0x3fff).rom().region("program", 0);
	map(0x4000, 0x7fff).bankrw(m_common_ram);
	map(0x8000, 0xbfff).bankrw(m_sram_bank);
	map(0xc000, 0xffff).view(m_io_view);
	m_io_view[0](0xc000, 0xc000).w(FUNC(roland_s50_state::ioga_out_w));
	m_io_view[0](0xc200, 0xc200).rw(FUNC(roland_s50_state::floppy_status_r), FUNC(roland_s50_state::floppy_select_w));
	m_io_view[0](0xc300, 0xc300).r(FUNC(roland_s50_state::floppy_unknown_r));
	m_io_view[0](0xc800, 0xc807).rw(m_fdc, FUNC(wd1772_device::read), FUNC(wd1772_device::write)).umask16(0x00ff);
	m_io_view[0](0xd200, 0xd200).r(m_vdp, FUNC(tms3556_device::vram_r));
	m_io_view[0](0xd202, 0xd202).rw(m_vdp, FUNC(tms3556_device::initptr_r), FUNC(tms3556_device::vram_w));
	m_io_view[0](0xd204, 0xd204).rw(m_vdp, FUNC(tms3556_device::reg_r), FUNC(tms3556_device::reg_w));
	m_io_view[0](0xc000, 0xffff).rw(m_wave, FUNC(rf5c36_device::read), FUNC(rf5c36_device::write)).umask16(0xff00);
	m_io_view[1](0xc000, 0xcfff).mirror(0x3000).rw(FUNC(roland_s50_state::key_r), FUNC(roland_s50_state::key_w));
}

void roland_s550_state::mem_map(address_map &map)
{
	map(0x0000, 0x1fff).view(m_lowmem_view);
	m_lowmem_view[0](0x0000, 0x1fff).rom().region("program", 0);
	m_lowmem_view[1](0x0000, 0x1fff).bankrw(m_lowram_bank);
	map(0x2000, 0x3fff).rom().region("program", 0x2000);
	map(0x4000, 0x7fff).bankrw(m_common_ram);
	map(0x8000, 0xbfff).bankrw(m_sram_bank);
	map(0xc000, 0xffff).view(m_io_view);
	m_io_view[0](0xc200, 0xc200).rw(FUNC(roland_s550_state::floppy_status_r), FUNC(roland_s550_state::floppy_select_w));
	m_io_view[0](0xc300, 0xc300).r(FUNC(roland_s550_state::floppy_unknown_r));
	m_io_view[0](0xc800, 0xc807).rw(m_fdc, FUNC(wd1772_device::read), FUNC(wd1772_device::write)).umask16(0x00ff);
	m_io_view[0](0xd000, 0xd000).r(m_vdp, FUNC(tms3556_device::vram_r));
	m_io_view[0](0xd002, 0xd002).rw(m_vdp, FUNC(tms3556_device::initptr_r), FUNC(tms3556_device::vram_w));
	m_io_view[0](0xd004, 0xd004).rw(m_vdp, FUNC(tms3556_device::reg_r), FUNC(tms3556_device::reg_w));
	//m_io_view[0](0xd800, 0xd81f).rw(m_tvf, FUNC(mb654419u_device::read), FUNC(mb654419u_device::write)).umask16(0x00ff);
	m_io_view[0](0xe000, 0xe000).w(FUNC(roland_s550_state::sram_bank_w));
	m_io_view[0](0xe800, 0xe81f).w("outas", FUNC(bu3905_device::write)).umask16(0x00ff);
	m_io_view[0](0xf800, 0xf81f).m("scsic", FUNC(mb89352_device::map)).umask16(0x00ff);
	m_io_view[0](0xc000, 0xffff).rw(m_wave, FUNC(rf5c36_device::read), FUNC(rf5c36_device::write)).umask16(0xff00);
	m_io_view[1](0xc000, 0xffff).unmaprw();
}

void roland_w30_state::mem_map(address_map &map)
{
	map(0x0000, 0x1fff).view(m_bank1_view);
	m_bank1_view[0](0x0000, 0x1fff).rom().region("program", 0);
	m_bank1_view[1](0x0000, 0x1fff).bankrw(m_psram1_bank);
	map(0x2000, 0x3fff).rom().region("program", 0x2000);
	map(0x4000, 0x7fff).bankrw(m_common_ram);
	map(0x8000, 0xbfff).view(m_bank2_view);
	m_bank2_view[0](0x8000, 0xbfff).rom().region("program", 0);
	m_bank2_view[1](0x8000, 0xbfff).bankrw(m_psram2_bank);
	map(0xc200, 0xc200).rw(FUNC(roland_w30_state::floppy_status_r), FUNC(roland_w30_state::floppy_select_w));
	map(0xc600, 0xc600).rw(FUNC(roland_w30_state::psram_bank_r), FUNC(roland_w30_state::psram_bank_w));
	map(0xc800, 0xc807).rw(m_fdc, FUNC(wd1772_device::read), FUNC(wd1772_device::write)).umask16(0x00ff);
	map(0xd806, 0xd806).r(FUNC(roland_w30_state::unknown_status_r));
	map(0xe000, 0xe01f).m("scsic", FUNC(mb89352_device::map)).umask16(0x00ff);
	map(0xe400, 0xe403).rw("lcd", FUNC(lm24014h_device::read), FUNC(lm24014h_device::write)).umask16(0x00ff);
	//map(0xe800, 0xe83f).w("output", FUNC(upd65006gf_376_3b8_device::write)).umask16(0x00ff);
	//map(0xf000, 0xf01f).rw(m_tvf, FUNC(mb654419u_device::read), FUNC(mb654419u_device::write)).umask16(0x00ff);
	map(0xc000, 0xf7ff).rw(m_wave, FUNC(sa16_device::read), FUNC(sa16_device::write)).umask16(0xff00);
	map(0xf800, 0xffff).rw(FUNC(roland_w30_state::key_r), FUNC(roland_w30_state::key_w));
}

void roland_s330_state::s330_mem_map(address_map &map)
{
	map(0x0000, 0x1fff).view(m_bank1_view);
	m_bank1_view[0](0x0000, 0x1fff).rom().region("program", 0);
	m_bank1_view[1](0x0000, 0x1fff).bankrw(m_psram1_bank);
	map(0x2000, 0x3fff).rom().region("program", 0x2000);
	map(0x4000, 0x7fff).bankrw(m_common_ram);
	map(0x8000, 0xbfff).view(m_bank2_view);
	m_bank2_view[0](0x8000, 0xbfff).rom().region("program", 0);
	m_bank2_view[1](0x8000, 0xbfff).bankrw(m_psram2_bank);
	map(0xc000, 0xffff).rw(m_wave, FUNC(sa16_device::read), FUNC(sa16_device::write)).umask16(0xff00);

	map(0xc200, 0xc200).rw(FUNC(roland_s330_state::floppy_status_r), FUNC(roland_s330_state::floppy_select_w));
	map(0xc300, 0xc302).rw(m_lcdc, FUNC(hd44780_device::read), FUNC(hd44780_device::write)).umask16(0x00ff);
	//map(0xc400, 0xc400) Ext port pins 1-4,6-8 are mapped to bit 0-6 respectively
	//map(0xc500, 0xc500) Ext port pindir bit 0,1 controls direction of pin 6,7
	map(0xc600, 0xc600).rw(FUNC(roland_s330_state::psram_bank_r), FUNC(roland_s330_state::psram_bank_w));
	map(0xc800, 0xc806).rw(m_fdc, FUNC(wd1772_device::read), FUNC(wd1772_device::write)).umask16(0x00ff);
	map(0xd000, 0xd000).r(m_vdp, FUNC(tms3556_device::vram_r));
	// D002 read used as dummy init-read by firmware (value is discarded and overwritten
	// with #21h immediately after). Must be initptr_r() to set m_init_read=true so the
	// first sequential vram_r() from D000 starts at VDP_BAMP, not VDP_BAMP-1.
	map(0xd002, 0xd002).rw(m_vdp, FUNC(tms3556_device::initptr_r), FUNC(tms3556_device::vram_w));
	map(0xd004, 0xd004).rw(m_vdp, FUNC(tms3556_device::reg_r), FUNC(tms3556_device::reg_w));

	map(0xd806, 0xd806).rw(FUNC(roland_s330_state::keysw_r), FUNC(roland_s330_state::keysw_w));

	map(0xf00c, 0xf00c).w(FUNC(roland_s330_state::leds_w));
}

void roland_s330_state::waveram_map(address_map &map)
{
	map(0x00000, 0xfffff).ram().share("waveram"); // 512k 12 bit data bus
}

void roland_s50_base_state::vram_map(address_map &map)
{
	map(0x0000, 0xffff).ram();
}

static INPUT_PORTS_START(s50)
INPUT_PORTS_END

static INPUT_PORTS_START(s550)
INPUT_PORTS_END

static INPUT_PORTS_START(w30)
	PORT_START("KEYSW0")
	PORT_BIT(0x03, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Start/Stop") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F3") PORT_CODE(KEYCODE_F3) 
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("0 ,._") PORT_CODE(KEYCODE_0) PORT_CODE(KEYCODE_0_PAD)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("- -*/") PORT_CODE(KEYCODE_MINUS) PORT_CODE(KEYCODE_MINUS_PAD) 
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD)

	PORT_START("KEYSW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Performance") PORT_CODE(KEYCODE_P)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Rec") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Tempo") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F2") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F4") PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("1 ABC") PORT_CODE(KEYCODE_1) PORT_CODE(KEYCODE_1_PAD)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("2 DEF") PORT_CODE(KEYCODE_2) PORT_CODE(KEYCODE_2_PAD)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("3 GHI") PORT_CODE(KEYCODE_3) PORT_CODE(KEYCODE_3_PAD)

	PORT_START("KEYSW2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sequencer") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("|<") PORT_CODE(KEYCODE_UP) PORT_CODE(KEYCODE_HOME)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("<") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F1") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("F5") PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("4 JKL") PORT_CODE(KEYCODE_4) PORT_CODE(KEYCODE_4_PAD)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("5 MNO") PORT_CODE(KEYCODE_5) PORT_CODE(KEYCODE_5_PAD)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("6 PQR") PORT_CODE(KEYCODE_6) PORT_CODE(KEYCODE_6_PAD)

	PORT_START("KEYSW3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sound") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(">|") PORT_CODE(KEYCODE_DOWN) PORT_CODE(KEYCODE_END)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME(">") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("User") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Exit") PORT_CODE(KEYCODE_BACKSPACE)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("7 STU") PORT_CODE(KEYCODE_7) PORT_CODE(KEYCODE_7_PAD)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("8 VWX") PORT_CODE(KEYCODE_8) PORT_CODE(KEYCODE_8_PAD)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("9 YZ#") PORT_CODE(KEYCODE_9) PORT_CODE(KEYCODE_9_PAD)

	// PORT_START("DIAL1")
	// PORT_BIT(0x03ff, 0x0000, IPT_DIAL) PORT_NAME("Cursor") PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_CODE_DEC(KEYCODE_LEFT) PORT_CODE_INC(KEYCODE_RIGHT)

	// PORT_START("DIAL2")
	// PORT_BIT(0x03ff, 0x0000, IPT_DIAL) PORT_NAME("Value") PORT_SENSITIVITY(50) PORT_KEYDELTA(8) PORT_CODE_DEC(KEYCODE_MINUS) PORT_CODE_INC(KEYCODE_PLUS)
INPUT_PORTS_END

static INPUT_PORTS_START(s330)
	PORT_START("KEYSW0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Mode") PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Menu") PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Dec/No") PORT_CODE(KEYCODE_MINUS) PORT_CODE(KEYCODE_MINUS_PAD)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Inc/Yes") PORT_CODE(KEYCODE_EQUALS) PORT_CODE(KEYCODE_PLUS_PAD)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Command") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("KEYSW1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Page") PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Sub Menu") PORT_CODE(KEYCODE_F4)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT) PORT_CODE(KEYCODE_4_PAD)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN) PORT_CODE(KEYCODE_2_PAD)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT) PORT_CODE(KEYCODE_6_PAD)
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_KEYBOARD) PORT_NAME("Execute") PORT_CODE(KEYCODE_ENTER) PORT_CODE(KEYCODE_ENTER_PAD)
	PORT_BIT(0xc0, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("KEYSW2")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	PORT_START("KEYSW3")
	PORT_BIT(0xff, IP_ACTIVE_LOW, IPT_UNUSED)

	// The S-330 determines the input controller at boot by checking which panel key
	// is held during power-on (Left=None, Down=Mouse, Right=RC-100). Real hardware
	// detects the physically-held key before power-up; emulation cannot replicate
	// that timing, so this setting injects the appropriate key into the first boot scan.
	PORT_START("CTRLTYPE")
	PORT_CONFNAME(0x03, 0x00, "Boot controller type")
	PORT_CONFSETTING(0x00, "None (panel keys)")
	PORT_CONFSETTING(0x01, "Mouse")
	PORT_CONFSETTING(0x02, "RC-100")
INPUT_PORTS_END

static void s50_floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

static void floppy_formats(format_registration &fr)
{
	fr.add_mfm_containers();
	fr.add(FLOPPY_ROLAND_SDISK_FORMAT);
}

static void scsi_devices(device_slot_interface &device)
{
	device.option_add("cdrom", NSCSI_CDROM);
	device.option_add("harddisk", NSCSI_HARDDISK);
}

void roland_s50_state::s50(machine_config &config)
{
	C8095_90(config, m_maincpu, 24_MHz_XTAL / 2);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_s50_state::mem_map);
	m_maincpu->out_p2_cb().set(FUNC(roland_s50_state::p2_w));

	MB63H149(config, m_keyscan, 24_MHz_XTAL / 2);
	m_keyscan->int_callback().set_inputline(m_maincpu, i8x9x_device::EXTINT_LINE);

	WD1772(config, m_fdc, 8_MHz_XTAL); // WD1770-00 or WD1772-02
	m_fdc->drq_wr_callback().set_inputline(m_maincpu, i8x9x_device::HSI2_LINE);
	m_fdc->intrq_wr_callback().set_inputline(m_maincpu, i8x9x_device::HSI3_LINE);

	// Floppy unit: FDD4261A0K or FDD4251G0K
	FLOPPY_CONNECTOR(config, m_floppy[0], s50_floppies, "35dd", &floppy_formats).enable_sound(true);
	FLOPPY_CONNECTOR(config, m_floppy[1], s50_floppies, nullptr, &floppy_formats).enable_sound(true);

	//UPD7538A(config, "fipcpu", 600_kHz_XTAL);

	TMS3556(config, m_vdp, 14.3496_MHz_XTAL); // TMS3556NL
	m_vdp->set_addrmap(0, &roland_s50_state::vram_map);
	//m_vdp->vsync_callback().set_inputline(m_maincpu, i8x9x_device::HSI1_LINE).invert();
	m_vdp->set_screen("screen");

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_video_attributes(VIDEO_UPDATE_BEFORE_VBLANK);
	screen.set_screen_update(m_vdp, FUNC(tms3556_device::screen_update));
	screen.set_size(tms3556_device::TOTAL_WIDTH, tms3556_device::TOTAL_HEIGHT*2);
	screen.set_visarea(0, tms3556_device::TOTAL_WIDTH-1, 0, tms3556_device::TOTAL_HEIGHT-1);
	screen.set_refresh_hz(60);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	screen.set_palette("palette");

	PALETTE(config, "palette", palette_device::RGB_3BIT);

	TIMER(config, "vdp_timer").configure_scanline(FUNC(roland_s50_state::vdp_timer), "screen", 0, 1);

	RF5C36(config, m_wave, 26.88_MHz_XTAL);
	m_wave->int_callback().set_inputline(m_maincpu, i8x9x_device::HSI0_LINE);
}

void roland_s550_state::s550(machine_config &config)
{
	s50(config);

	m_maincpu->set_addrmap(AS_PROGRAM, &roland_s550_state::mem_map);
	m_fdc->intrq_wr_callback().set_nop();

	//UPD7537(config.device_replace(), "fipcpu", 400_kHz_XTAL);

	config.device_remove("keyscan");

	// SCSI controller on Option Board
	auto &scsi(NSCSI_BUS(config, "scsi"));
	NSCSI_CONNECTOR(config, "scsi:0", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:1", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:2", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:3", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:4", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:5", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:6", scsi_devices, nullptr);
	auto &scsic(MB89352(config, "scsic", 8_MHz_XTAL));
	scsi.set_external_device(7, scsic);
	scsic.out_irq_callback().set_inputline(m_maincpu, i8x9x_device::EXTINT_LINE);

	BU3905(config, "outas");

	//MB654419U(config, m_tvf, 20_MHz_XTAL);

	m_wave->sh_callback().set("outas", FUNC(bu3905_device::axi_w));
}

void roland_w30_state::w30(machine_config &config)
{
	N8097BH(config, m_maincpu, 24_MHz_XTAL / 2);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_w30_state::mem_map);

	MB63H149(config, m_keyscan, 24_MHz_XTAL / 2);
	m_keyscan->int_callback().set_inputline(m_maincpu, i8x9x_device::EXTINT_LINE);

	WD1772(config, m_fdc, 8_MHz_XTAL); // WD1772-02

	// Floppy unit: FX-354 (307F1JC)
	FLOPPY_CONNECTOR(config, m_floppy[0], s50_floppies, "35dd", &floppy_formats).enable_sound(true);
	FLOPPY_CONNECTOR(config, m_floppy[1], s50_floppies, nullptr, &floppy_formats).enable_sound(true);

	// SCSI controller on main board, by option (KW-30)
	auto &scsi(NSCSI_BUS(config, "scsi"));
	NSCSI_CONNECTOR(config, "scsi:0", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:1", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:2", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:3", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:4", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:5", scsi_devices, nullptr);
	NSCSI_CONNECTOR(config, "scsi:6", scsi_devices, nullptr);
	auto &scsic(MB89352(config, "scsic", 8_MHz_XTAL)); // INTR & DREQ not connected
	scsi.set_external_device(7, scsic);

	LM24014H(config, "lcd"); // LCD unit: LM240142

	SA16(config, m_wave, 26.88_MHz_XTAL);
	m_wave->int_callback().set_inputline(m_maincpu, i8x9x_device::HSI0_LINE);

	//UPD65006GF_376_3B8(config, "output", 26.88_MHz_XTAL);

	//MB654419U(config, m_tvf, 20_MHz_XTAL);
}

// void roland_s330_state::s330(machine_config &config)
// {
// 	N8097BH(config, m_maincpu, 24_MHz_XTAL / 2); // P8097-90
// 	m_maincpu->set_addrmap(AS_PROGRAM, &roland_s330_state::mem_map);
// 	m_maincpu->ach4_cb().set(FUNC(roland_s330_state::analog_vol_ctrl)); // Volume control
// 	m_maincpu->ach7_cb().set(FUNC(roland_s330_state::analog_dac_value)); // A/D compare level

// 	ADDRESS_MAP_BANK(config, m_psram[0]);
// 	m_psram[0]->set_endianness(ENDIANNESS_LITTLE);
// 	m_psram[0]->set_data_width(16);
// 	m_psram[0]->set_addr_width(16);
// 	m_psram[0]->set_stride(0x2000);
// 	m_psram[0]->set_addrmap(0, &roland_s330_state::psram1_map);

// 	ADDRESS_MAP_BANK(config, m_psram[1]);
// 	m_psram[1]->set_endianness(ENDIANNESS_LITTLE);
// 	m_psram[1]->set_data_width(16);
// 	m_psram[1]->set_addr_width(17);
// 	m_psram[1]->set_stride(0x4000);
// 	m_psram[1]->set_addrmap(0, &roland_s330_state::psram2_map);

// 	WD1772(config, m_fdc, 8_MHz_XTAL); // WD1772-02

// 	// Floppy unit: ND-362S-A
// 	FLOPPY_CONNECTOR(config, m_floppy, s50_floppies, "35dd", floppy_image_device::default_pc_floppy_formats).enable_sound(true);

// 	config.set_default_layout(layout_s330);
void roland_s330_state::s330(machine_config &config)
{
	C8095_90(config, m_maincpu, 24_MHz_XTAL / 2); // P8097-90
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_s330_state::s330_mem_map);
	m_maincpu->ach4_cb().set(FUNC(roland_s330_state::analog_vol_ctrl)); // Volume control
 	m_maincpu->ach7_cb().set(FUNC(roland_s330_state::analog_dac_value)); // A/D compare level


	WD1772(config, m_fdc, 8_MHz_XTAL); // WD1772-02

	// Floppy unit: ND-362S-A
	FLOPPY_CONNECTOR(config, m_floppy[0], s50_floppies, "35dd", &floppy_formats).enable_sound(true);
	FLOPPY_CONNECTOR(config, m_floppy[1], s50_floppies, nullptr, &floppy_formats).enable_sound(true);

	// LCD unit: DM1620-5BL7 (MW-5F)
	HD44780(config, m_lcdc, 270'000); // TODO: clock not measured, datasheet typical clock used
	m_lcdc->set_lcd_size(2, 16);
	m_lcdc->set_pixel_update_cb(FUNC(roland_s330_state::lcd_pixel_update));
	m_lcdc->set_busy_factor(0.005f);

	screen_device &lcd(SCREEN(config, "lcdpanel", SCREEN_TYPE_LCD));
	lcd.set_refresh_hz(50);
	lcd.set_vblank_time(ATTOSECONDS_IN_USEC(2500));
	lcd.set_size(6*16, 9*2);
	lcd.set_visarea(0, 6*16-1, 0, 9*2-1);
	lcd.set_screen_update("lcdc", FUNC(hd44780_device::screen_update));
	lcd.set_palette("lcd_pal");
	PALETTE(config, "lcd_pal", FUNC(roland_s330_state::init_lcd_palette), 2);

	TMS3556(config, m_vdp, 14.3496_MHz_XTAL); // TMS3556NL
	m_vdp->set_addrmap(0, &roland_s330_state::vram_map);
	m_vdp->set_screen("screen");

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_video_attributes(VIDEO_UPDATE_BEFORE_VBLANK);
	screen.set_screen_update(m_vdp, FUNC(tms3556_device::screen_update));
	screen.set_size(tms3556_device::TOTAL_WIDTH, tms3556_device::TOTAL_HEIGHT*2);
	screen.set_visarea(0, tms3556_device::TOTAL_WIDTH-1, 0, tms3556_device::TOTAL_HEIGHT-1);
	screen.set_refresh_hz(60);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(2500)); /* not accurate */
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(roland_s330_state::init_vdp_palette), 8);

	TIMER(config, "vdp_timer").configure_scanline(FUNC(roland_s330_state::vdp_timer), "screen", 0, 1);

	SA16(config, m_wave, 26.88_MHz_XTAL);
	m_wave->set_addrmap(0, &roland_s330_state::waveram_map);
	m_wave->int_callback().set_inputline(m_maincpu, i8x9x_device::HSI0_LINE);
	m_wave->sh_callback().set("outas", FUNC(bu3905_device::axi_w));

	BU3905(config, "outas");

	//MB654419U(config, m_tvf, 20_MHz_XTAL);
}

ROM_START(s50)
	ROM_REGION16_LE(0x4000, "program", 0)
	// PROMs contain identical data but are mapped with even bytes loaded from IC64 and odd bytes from IC65
	ROM_LOAD("s-50.ic64", 0x0000, 0x4000, CRC(9a911016) SHA1(00a829d7921556c41d872c10b7bbb82b62b6c5cf))
	ROM_LOAD("s-50.ic65", 0x0000, 0x4000, CRC(9a911016) SHA1(00a829d7921556c41d872c10b7bbb82b62b6c5cf))

	ROM_REGION(0x1000, "fipcpu", 0) // on panel board
	ROM_LOAD("upd7538a-013_15179240.ic1", 0x0000, 0x1000, NO_DUMP)
ROM_END

ROM_START(s550)
	ROM_REGION16_LE(0x4000, "program", 0)
	// PROMs contain identical data but are mapped with even bytes loaded from IC6 and odd bytes from IC3
	ROM_LOAD("s-550_2-0-0.ic6", 0x0000, 0x4000, CRC(9dbc93b7) SHA1(bd9219772773f51e5ad7872daa1eaf03ec23f2c5))
	ROM_LOAD("s-550_2-0-0.ic3", 0x0000, 0x4000, CRC(9dbc93b7) SHA1(bd9219772773f51e5ad7872daa1eaf03ec23f2c5))

	ROM_REGION(0x800, "fipcpu", 0)
	ROM_LOAD("upd7537c-014_15179201.ic35", 0x000, 0x800, NO_DUMP)
ROM_END

ROM_START(w30)
	ROM_REGION16_LE(0x4000, "program", 0)
	ROM_LOAD16_BYTE("w-30_1-0-3.ic19", 0x0000, 0x2000, CRC(4aa83074) SHA1(6d6f3f9dc58a4aed7cbc5d8cfce4a8b3bc2a276a))
	ROM_LOAD16_BYTE("w-30_1-0-3.ic20", 0x0001, 0x2000, CRC(9c5e3c7f) SHA1(42a0463322be5f965967d531d3636376785c9820))

	ROM_REGION16_LE(0x100000, "wave", 0)
	ROM_LOAD16_BYTE("lh534146_15179935.ic30", 0x00000, 0x80000, NO_DUMP) // D0-D3 not connected
	ROM_LOAD16_BYTE("lh534145_15179936.ic29", 0x00001, 0x80000, NO_DUMP)
ROM_END

[[maybe_unused]] ROM_START(s330)
	ROM_REGION16_LE(0x4000, "program", 0)
	ROM_LOAD16_BYTE("s-330.ic15", 0x0000, 0x2000, CRC(20AA7CE0) SHA1(554839c48aa8851988d86ce1b59cb32e92588c48))
	ROM_LOAD16_BYTE("s-330.ic14", 0x0001, 0x2000, CRC(32A00F31) SHA1(d764651299273ec8f4801e13e20f98690b491992))
ROM_END

} // anonymous namespace


SYST(1987, s50,  0,   0, s50,  s50,  roland_s50_state,  empty_init, "Roland", "S-50 Digital Sampling Keyboard", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
SYST(1987, s550, s50, 0, s550, s550, roland_s550_state, empty_init, "Roland", "S-550 Digital Sampler", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
SYST(1988, w30,  0,   0, w30,  w30,  roland_w30_state,  empty_init, "Roland", "W-30 Music Workstation", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
SYST(1988, s330, w30, 0, s330, s330, roland_s330_state, empty_init, "Roland", "S-330 Digital Sampler", MACHINE_NO_SOUND | MACHINE_NOT_WORKING)
