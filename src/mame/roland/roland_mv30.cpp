// license:BSD-3-Clause
// copyright-holders:Carl
/****************************************************************************

    Skeleton driver for Roland MV-30 Studio M sequencer & sound module.

    The MV-30 is a combined sequencer and sound module unit using a mix of
    Roland U-220 sound engine and D-70 filters/effects.

    CPU: IC19 Intel 80C196KB-12 (MCS-96 family, 16-bit data bus)
    FDC: IC1 NEC uPD72068GF-389

    Note: the correct CPU is 80C196KB which belongs to the i8xc196 enhanced
    family. MAME does not yet have a concrete device type for 80C196KB, so
    N8097BH (16-bit MCS-96) is used as a stand-in. The 80C196 has additional
    instructions (bmov, cmpl, djnzw, pop, popa, pusha) that are not emulated
    with this substitution.

    Memory banking
    ==============
    The CPU accesses RAM, ROM and I/O through a banking unit controlled by
    8 word-size page registers at SFR addresses 0x100-0x10E:

        0x100: Execute page for 0x0000-0x3FFF  (instruction fetch)
        0x102: Execute page for 0x4000-0x7FFF  (instruction fetch)
        0x104: Execute page for 0x8000-0xBFFF  (instruction fetch)
        0x106: Execute page for 0xC000-0xFFFF  (instruction fetch)
        0x108: Data page for 0x0000-0x3FFF     (read/write data)
        0x10A: Data page for 0x4000-0x7FFF     (read/write data)
        0x10C: Data page for 0x8000-0xBFFF     (read/write data)
        0x10E: Data page for 0xC000-0xFFFF     (read/write data)

    Each register value selects a physical address with 1KB page granularity:
        physical_address = page_register_value * 0x400

    The selected page is the starting address for the 16KB window.

    Examples:
        Value 0x000 in reg 0x102 -> RAM offset 0x00000 at 0x4000-0x7FFF (exec)
        Value 0x001 in reg 0x102 -> RAM offset 0x00400 at 0x4000-0x7FFF (exec)
        Value 0x010 in reg 0x104 -> RAM offset 0x04000 at 0x8000-0xBFFF (exec)
        Value 0x780 in reg 0x100 -> ROM mapped at 0x0000-0x3FFF (exec) (tentative)

    Physical memory layout (inferred):
        0x000000-0x07FFFF: 512KB RAM (pages 0x000-0x1FF)
        0x1E0000-0x1E3FFF: 16KB ROM  (page 0x780, tentative)
        I/O devices are memory-mapped 0x800 bytes apart (in a separate region)

    Startup state (inferred):
        Execute 0x0000-0x3FFF: ROM         (page 0x780?)
        Execute 0x4000-0xFFFF: unknown
        Data    0x0000-0x3FFF: RAM page 0  (value 0x000)
        Data    0x4000-0x7FFF: RAM page 16 (value 0x010)
        Data    0x8000-0xBFFF: RAM page 32 (value 0x020)
        Data    0xC000-0xFFFF: I/O region

    I/O memory map (at C000-FFFF on startup, 0x800 per slot):
        C000-C7FF: Reverb/Effect unit - Roland RCC (TC23SC104AF-007)
        C800-CFFF: Key scan / LED gate array - Roland TC23SC060
        D000-D7FF: FDC - NEC uPD72068GF-389
        D800-DFFF: PCM controller - Roland MB87419 LP-1
        E000-E7FF: FSK gate array - MN53015RRA
        E800-EFFF: LCD controller - T6963C compatible (240x64)
        F000-FFFF: (unmapped)

    Limitation: MAME's MCS-96 uses a single address space for instruction
    fetch and data access, so the dual execute/data banking cannot be
    perfectly emulated. This skeleton maps using the data banks by default
    and overlays ROM at the boot address range so the CPU can reach the
    reset vector at 0x2080.

****************************************************************************/

#include "emu.h"
#include "cpu/mcs96/i8x9x.h"
#include "bus/midi/midiinport.h"
#include "bus/midi/midioutport.h"
#include "imagedev/floppy.h"
#include "machine/upd765.h"
#include "machine/timer.h"
#include "sound/roland_lp.h"
#include "video/t6963c.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"

#include <queue>


namespace {

class roland_mv30_state : public driver_device
{
public:
	roland_mv30_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rom(*this, "maincpu")
		, m_ram(*this, "ram", 512 * 1024, ENDIANNESS_LITTLE)
		, m_view0(*this, "view0")
		, m_view1(*this, "view1")
		, m_view2(*this, "view2")
		, m_view3(*this, "view3")
		, m_keys(*this, "SC%u", 0)
		, m_fdc(*this, "fdc")
		, m_floppy(*this, "fdc:0")
		, m_pcm(*this, "pcm")
		, m_lcd(*this, "lcd")
		, m_midi_timer(*this, "midi_timer")
	{
	}

	void mv30(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	static constexpr unsigned RAM_SIZE    = 512 * 1024;   // 512KB
	static constexpr unsigned ROM_SIZE    = 16 * 1024;    // 16KB
	static constexpr unsigned WINDOW_SIZE = 16 * 1024;    // 16KB per banking window
	static constexpr unsigned PAGE_SHIFT  = 10;           // 1KB page granularity
	static constexpr u16      ROM_PAGE    = 0x780;        // tentative ROM page value

	void mem_map(address_map &map) ATTR_COLD;
	void io_map(address_map &map) ATTR_COLD;
	void lcd_map(address_map &map) ATTR_COLD;
	void lcd_palette(palette_device &palette) const ATTR_COLD;

	void bank_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 bank_r(offs_t offset, u16 mem_mask = ~0);

	void update_data_bank(int window);

	// I/O device handlers

	// C000: Reverb/Effect RCC
	u8 rcc_r(offs_t offset);
	void rcc_w(offs_t offset, u8 data);

	// C800: Key scan / LED gate array
	void keyscan_w(offs_t offset, u8 data);
	u8 keyscan_r(offs_t offset);

	// E000: FSK gate array
	u8 fsk_r(offs_t offset);
	void fsk_w(offs_t offset, u8 data);

	// MIDI
	void midi_in_w(int state);
	TIMER_DEVICE_CALLBACK_MEMBER(midi_timer_cb);

	required_device<i8x9x_device> m_maincpu;
	required_region_ptr<u8> m_rom;
	memory_share_creator<u8> m_ram;

	memory_view m_view0;   // 0x0000-0x3FFF
	memory_view m_view1;   // 0x4000-0x7FFF
	memory_view m_view2;   // 0x8000-0xBFFF
	memory_view m_view3;   // 0xC000-0xFFFF

	u16 m_bank_reg[8];     // 0x100-0x10E: execute[0..3], data[4..7]

	required_ioport_array<8> m_keys;

	// I/O devices
	required_device<upd72067_device> m_fdc;
	optional_device<floppy_connector> m_floppy;
	required_device<mb87419_mb87420_device> m_pcm;
	required_device<t6963c_device> m_lcd;
	required_device<timer_device> m_midi_timer;

	// Key scan state
	u8 m_keyscan_select;

	// MIDI bit-bang state
	std::queue<u8> m_midi_queue;
	u8 m_midi_rx;
	int m_midi_pos;
};


void roland_mv30_state::mem_map(address_map &map)
{
	// Banking registers at SFR 0x100-0x10F
	map(0x0100, 0x010f).rw(FUNC(roland_mv30_state::bank_r), FUNC(roland_mv30_state::bank_w));

	// Four 16KB banking windows covering the entire 64KB address space.
	// Each window can present ROM, RAM, or I/O depending on the bank
	// register settings.  At boot, ROM needs to be visible at 0x2080+
	// for the MCS-96 reset vector.
	//
	// Note: 0x0000-0x00FF is the CPU internal register file and takes
	// priority over external memory for data access.  Instruction fetch
	// at those addresses goes to external memory (ROM/RAM).

	map(0x0000, 0x3fff).view(m_view0);
	m_view0[0](0x0000, 0x3fff).rom().region("maincpu", 0);          // ROM (boot)
	m_view0[1](0x0000, 0x3fff).ram().share("ram");                   // RAM page 0

	map(0x4000, 0x7fff).view(m_view1);
	m_view1[0](0x4000, 0x7fff).ram().share("ram");                   // placeholder
	m_view1[1](0x4000, 0x7fff).ram().share("ram");                   // placeholder

	map(0x8000, 0xbfff).view(m_view2);
	m_view2[0](0x8000, 0xbfff).ram().share("ram");                   // placeholder
	m_view2[1](0x8000, 0xbfff).ram().share("ram");                   // placeholder

	map(0xc000, 0xffff).view(m_view3);
	m_view3[0](0xc000, 0xffff).ram().share("ram");                   // RAM (when banked to RAM)
	// I/O view: devices mapped 0x800 apart
	m_view3[1](0xc000, 0xc7ff).rw(FUNC(roland_mv30_state::rcc_r), FUNC(roland_mv30_state::rcc_w));
	m_view3[1](0xc800, 0xc801).rw(FUNC(roland_mv30_state::keyscan_r), FUNC(roland_mv30_state::keyscan_w));
	m_view3[1](0xd000, 0xd001).m(m_fdc, FUNC(upd72067_device::map));
	m_view3[1](0xd800, 0xd81f).rw(m_pcm, FUNC(mb87419_mb87420_device::read), FUNC(mb87419_mb87420_device::write));
	m_view3[1](0xe000, 0xe003).rw(FUNC(roland_mv30_state::fsk_r), FUNC(roland_mv30_state::fsk_w));
	m_view3[1](0xe800, 0xe802).rw(m_lcd, FUNC(t6963c_device::read), FUNC(t6963c_device::write)).umask16(0x00ff);
}


void roland_mv30_state::lcd_map(address_map &map)
{
	map(0x0000, 0x1fff).ram();   // 8KB display RAM
}


void roland_mv30_state::lcd_palette(palette_device &palette) const
{
	palette.set_pen_color(0, rgb_t(0x9f, 0xb4, 0x86));   // LCD background
	palette.set_pen_color(1, rgb_t(0x5e, 0x5f, 0x71));   // LCD foreground
}


void roland_mv30_state::machine_start()
{
	save_item(NAME(m_bank_reg));
	save_item(NAME(m_keyscan_select));
	save_item(NAME(m_midi_rx));
	save_item(NAME(m_midi_pos));
}


void roland_mv30_state::machine_reset()
{
	// Startup banking state (inferred from hardware behavior):
	//   Execute 0000-3FFF = ROM,  Data 0000-3FFF = RAM page 0
	//   Data 4000-7FFF = RAM page 0x010
	//   Data 8000-BFFF = RAM page 0x020
	//   Data C000-FFFF = I/O

	m_bank_reg[0] = ROM_PAGE;    // execute 0x0000-0x3FFF -> ROM
	m_bank_reg[1] = 0;           // execute 0x4000-0x7FFF -> unknown
	m_bank_reg[2] = 0;           // execute 0x8000-0xBFFF -> unknown
	m_bank_reg[3] = 0;           // execute 0xC000-0xFFFF -> unknown
	m_bank_reg[4] = 0x000;       // data 0x0000-0x3FFF -> RAM offset 0
	m_bank_reg[5] = 0x010;       // data 0x4000-0x7FFF -> RAM offset 0x4000
	m_bank_reg[6] = 0x020;       // data 0x8000-0xBFFF -> RAM offset 0x8000
	m_bank_reg[7] = 0;           // data 0xC000-0xFFFF -> I/O (special)

	m_keyscan_select = 0;
	m_midi_rx = 0;
	m_midi_pos = 0;
	std::queue<u8>().swap(m_midi_queue);

	// Window 0 starts showing ROM for instruction fetch at reset
	m_view0.select(0);
	m_view1.select(0);
	m_view2.select(0);
	// Window 3 starts in I/O mode
	m_view3.select(1);
}


u16 roland_mv30_state::bank_r(offs_t offset, u16 mem_mask)
{
	return m_bank_reg[offset];
}


void roland_mv30_state::bank_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_bank_reg[offset]);

	logerror("bank_w: reg %03x = %04x (window %d, %s)\n",
		0x100 + offset * 2, m_bank_reg[offset],
		offset & 3, (offset < 4) ? "execute" : "data");

	// Update the corresponding data bank window
	// Execute banks (offset 0-3) cannot be properly emulated since MAME
	// uses a unified address space.  Data banks (offset 4-7) control
	// actual physical memory mapping.
	if (offset >= 4)
		update_data_bank(offset - 4);

	// If execute bank 0 is written, update view0 for ROM vs RAM
	if (offset == 0)
	{
		u16 page = m_bank_reg[0];
		if (page == ROM_PAGE)
			m_view0.select(0);   // ROM
		else
			m_view0.select(1);   // RAM (same as data bank view)
	}
}


void roland_mv30_state::update_data_bank(int window)
{
	u16 page = m_bank_reg[4 + window];
	u32 phys = u32(page) << PAGE_SHIFT;

	// Window 3: check for I/O mapping
	// Page value 0 for window 3 is assumed to mean I/O (startup default)
	if (window == 3 && page == 0)
	{
		m_view3.select(1);   // I/O view
		return;
	}

	// Check if this page maps to RAM
	if (phys < RAM_SIZE)
	{
		u16 base = window * WINDOW_SIZE;
		memory_view *view = nullptr;
		switch (window)
		{
		case 0: view = &m_view0; break;
		case 1: view = &m_view1; break;
		case 2: view = &m_view2; break;
		case 3: view = &m_view3; break;
		}

		if (view)
		{
			// Switch to RAM view (view 0 for windows 1-3, view 1 for window 0)
			if (window == 0)
				view->select(1);
			else
				view->select(0);

			// Install RAM at the correct physical offset via direct pointer
			m_maincpu->space(AS_PROGRAM).install_ram(base, base + WINDOW_SIZE - 1, &m_ram[phys]);
		}
	}
	else if (page == ROM_PAGE && window == 0)
	{
		// ROM in window 0
		m_view0.select(0);
	}
	else
	{
		logerror("update_data_bank: window %d mapped to unhandled physical page %03x (addr %06x)\n",
			window, page, phys);
	}
}


//
// I/O device handlers
//

// C000-C7FF: Reverb/Effect unit - Roland RCC (TC23SC104AF-007)
u8 roland_mv30_state::rcc_r(offs_t offset)
{
	logerror("rcc_r: offset %03x\n", offset);
	return 0;
}

void roland_mv30_state::rcc_w(offs_t offset, u8 data)
{
	logerror("rcc_w: offset %03x = %02x\n", offset, data);
}

// C800-C8FF: Key scan / LED gate array - Roland TC23SC060
// C800 (write): KEY_SCAN register - selects which SCn row to scan or LED register
// C801 (read):  KEY_DATA register - returns SB bits for selected scan row
// C801 (write): LED data register
void roland_mv30_state::keyscan_w(offs_t offset, u8 data)
{
	if (offset == 0)
	{
		// KEY_SCAN register select (which SC row)
		m_keyscan_select = data;
		logerror("keyscan_w: select row %02x\n", data);
	}
	else
	{
		// LED data register
		logerror("keyscan_w: LED data %02x (row %02x)\n", data, m_keyscan_select);
	}
}

u8 roland_mv30_state::keyscan_r(offs_t offset)
{
	if (offset == 1)
	{
		// KEY_DATA: return SB bits for the selected SC row
		if (m_keyscan_select < 8)
			return m_keys[m_keyscan_select]->read();

		logerror("keyscan_r: read key data for unknown row %02x\n", m_keyscan_select);
	}
	else
	{
		logerror("keyscan_r: unexpected read at offset %x\n", offset);
	}
	return 0xff;
}

// E000-E003: FSK gate array - MN53015RRA (tape sync)
u8 roland_mv30_state::fsk_r(offs_t offset)
{
	logerror("fsk_r: offset %x\n", offset);
	return 0;
}

void roland_mv30_state::fsk_w(offs_t offset, u8 data)
{
	logerror("fsk_w: offset %x = %02x\n", offset, data);
}

// MIDI IN bit-bang receiver
void roland_mv30_state::midi_in_w(int state)
{
	if (m_midi_pos == 0 || m_midi_pos == 9)
	{
		m_midi_pos += 1;
	}
	else if (m_midi_pos == 10)
	{
		m_midi_queue.push(m_midi_rx);
		m_midi_rx = 0;
		m_midi_pos = 0;
	}
	else
	{
		m_midi_rx |= state << (m_midi_pos - 1);
		m_midi_pos += 1;
	}
}

TIMER_DEVICE_CALLBACK_MEMBER(roland_mv30_state::midi_timer_cb)
{
	if (m_midi_queue.empty())
		return;

	u8 midi = m_midi_queue.front();
	m_midi_queue.pop();
	m_maincpu->serial_w(midi);
}


static INPUT_PORTS_START(mv30)
	PORT_START("SC0")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PTN EDIT")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("CTRL")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("0") PORT_CODE(KEYCODE_0)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("1/9") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("JUMP")

	PORT_START("SC1")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("TRK EDIT")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("3") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("COMPU")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("2") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("2/10")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F1") PORT_CODE(KEYCODE_F1)

	PORT_START("SC2")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("SYSTEM")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("6") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("MANUAL")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("5") PORT_CODE(KEYCODE_5)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("3/11")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F2") PORT_CODE(KEYCODE_F2)

	PORT_START("SC3")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DISK")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("9") PORT_CODE(KEYCODE_9)
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("SONG SELECT")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("8") PORT_CODE(KEYCODE_8)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("4/12")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F3") PORT_CODE(KEYCODE_F3)

	PORT_START("SC4")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PTN MICROSCOPE")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PTN REALTIME")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("INS") PORT_CODE(KEYCODE_INSERT)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("REC")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("STATUS")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("1") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("5/13")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F4") PORT_CODE(KEYCODE_F4)

	PORT_START("SC5")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("TRK MICROSCOPE")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("TRK REALTIME")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("DEL") PORT_CODE(KEYCODE_DEL)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Rewind")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("LOCATE")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("4") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("6/14")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("F5") PORT_CODE(KEYCODE_F5)

	PORT_START("SC6")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("TIMBRE EDIT")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("COMPU MIX")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Play/Stop")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("MARK")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("7") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("7/15")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("EXIT") PORT_CODE(KEYCODE_BACKSPACE)

	PORT_START("SC7")
	PORT_BIT(0x01, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("CHAIN PLAY")
	PORT_BIT(0x02, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("PLAY")
	PORT_BIT(0x04, IP_ACTIVE_LOW, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("Fast Forward")
	PORT_BIT(0x10, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("TEMPO")
	PORT_BIT(0x20, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_LSHIFT)
	PORT_BIT(0x40, IP_ACTIVE_LOW, IPT_OTHER) PORT_NAME("8/16")
	PORT_BIT(0x80, IP_ACTIVE_LOW, IPT_UNUSED)
INPUT_PORTS_END


static void mv30_floppies(device_slot_interface &device) ATTR_COLD;
static void mv30_floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

void roland_mv30_state::mv30(machine_config &config)
{
	// CPU: 80C196KB-12 (using N8097BH as stand-in, both 16-bit MCS-96)
	N8097BH(config, m_maincpu, 12_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_mv30_state::mem_map);
	m_maincpu->serial_tx_cb().set("mdout", FUNC(midi_port_device::write_txd));

	// FDC: NEC uPD72068GF-389 (using uPD72067 as closest available stand-in)
	UPD72067(config, m_fdc, 32_MHz_XTAL);
	m_fdc->intrq_wr_callback().set_inputline(m_maincpu, INPUT_LINE_IRQ0);
	FLOPPY_CONNECTOR(config, m_floppy, mv30_floppies, "35dd", floppy_image_device::default_mfm_floppy_formats);

	// PCM: Roland MB87419/MB87420 LP-1 sound engine (U-220 compatible)
	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();
	MB87419_MB87420(config, m_pcm, 32.768_MHz_XTAL);
	m_pcm->int_callback().set_inputline(m_maincpu, INPUT_LINE_IRQ1);
	m_pcm->add_route(0, "lspeaker", 1.0);
	m_pcm->add_route(1, "rspeaker", 1.0);

	// LCD: T6963C compatible, 240x64 pixels
	T6963C(config, m_lcd, 0);
	m_lcd->set_addrmap(0, &roland_mv30_state::lcd_map);

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_LCD));
	screen.set_refresh_hz(60);
	screen.set_size(240, 64);
	screen.set_visarea(0, 240 - 1, 0, 64 - 1);
	screen.set_screen_update(m_lcd, FUNC(t6963c_device::screen_update));
	screen.set_palette("palette");

	PALETTE(config, "palette", FUNC(roland_mv30_state::lcd_palette), 2);

	// MIDI
	TIMER(config, m_midi_timer).configure_periodic(FUNC(roland_mv30_state::midi_timer_cb), attotime::from_hz(1250));

	midi_port_device &mdin(MIDI_PORT(config, "mdin", midiin_slot, "midiin"));
	mdin.rxd_handler().set(FUNC(roland_mv30_state::midi_in_w));
	mdin.rxd_handler().append("mdthru", FUNC(midi_port_device::write_txd));

	MIDI_PORT(config, "mdout", midiout_slot, "midiout");
	MIDI_PORT(config, "mdthru", midiout_slot, "midiout");
}


ROM_START(mv30)
	ROM_REGION16_LE(0x4000, "maincpu", 0)
	ROM_LOAD("mv30rom103.bin", 0x0000, 0x4000, CRC(001e3ded) SHA1(dc09c839c774e8190c0dbfaa3f2fce3049be735e))

	ROM_REGION(0x400, "lcd:cgrom", 0)
	ROM_LOAD("t6963c_0101.bin", 0x000, 0x400, CRC(547d118b) SHA1(accb5570228a7a5b4a1f9c70e89dcc14e5d3a4a1))

	ROM_REGION(0x800000, "pcm:waverom", 0)
	ROM_LOAD("roland_mv30_waverom.bin", 0x000000, 0x800000, NO_DUMP)   // TODO: actual wave ROM
ROM_END

} // anonymous namespace


SYST(1990, mv30, 0, 0, mv30, mv30, roland_mv30_state, empty_init, "Roland", "MV-30 Studio M", MACHINE_NOT_WORKING)
