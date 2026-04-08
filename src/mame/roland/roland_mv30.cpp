// license:BSD-3-Clause
// copyright-holders:Carl
/****************************************************************************

    Skeleton driver for Roland MV-30 Studio M sequencer & sound module.

    The MV-30 is a combined sequencer and sound module unit using a mix of
    Roland U-220 sound engine and D-70 filters/effects.

    CPU: IC19 Intel 80C196KB-12 (MCS-96 family, 16-bit data bus)
    FDC: IC1 NEC uPD72068GF-389

    The CPU is an 80C196KB, which combines the i8x9x peripheral set with
    the enhanced i8xc196 instruction set (bmov, cmpl, djnzw, popa, pusha).

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

// PCM ROM address/data descrambling (same as D-70)
template <typename T> constexpr auto UNSCRAMBLE_ADDR_INT(T offset) {
	return bitswap<19>(offset, 18, 17, 15, 14, 16, 12, 11, 7, 9, 13, 10, 8, 3, 2, 1, 6, 4, 5, 0);
}
constexpr u8 UNSCRAMBLE_DATA(u8 data) { return bitswap<8>(data, 1, 2, 7, 3, 5, 0, 4, 6); }

class roland_mv30_state : public driver_device
{
public:
	roland_mv30_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_rom(*this, "maincpu")
		, m_ram(*this, "ram", 1024 * 1024, ENDIANNESS_LITTLE)
		, m_view0(*this, "view0")
		, m_view1(*this, "view1")
		, m_view2(*this, "view2")
		, m_view3(*this, "view3")
		, m_keys(*this, "SC%u", 0)
		, m_sliders(*this, "SL%u", 0)
		, m_encoder(*this, "ENCODER")
		, m_fdc(*this, "fdc")
		, m_floppy(*this, "fdc:0")
		, m_pcm(*this, "pcm")
		, m_lcd(*this, "lcd")
		, m_midi_timer(*this, "midi_timer")
	{
	}

	void mv30(machine_config &config) ATTR_COLD;
	void init_mv30() ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	static constexpr unsigned RAM_SIZE_MAX = 1024 * 1024;  // 1MB (max with expansion)
	static constexpr unsigned ROM_SIZE    = 16 * 1024;    // 16KB
	static constexpr unsigned WINDOW_SIZE = 16 * 1024;    // 16KB per banking window
	static constexpr unsigned PAGE_SHIFT  = 10;           // 1KB page granularity
	static constexpr u16      ROM_PAGE    = 0x780;        // tentative ROM page value
	static constexpr u16      IO_PAGE     = 0x474;        // I/O region page value (set by boot code)

	void mem_map(address_map &map) ATTR_COLD;
	void opcodes_map(address_map &map) ATTR_COLD;
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

	// C800: Key scan / LED gate array (TC23SC060)
	void keyscan_w(offs_t offset, u8 data);
	u8 keyscan_r(offs_t offset);
	TIMER_CALLBACK_MEMBER(keyscan_scan_tick);
	void keyscan_update_irq();

	// E000: FSK gate array
	u8 fsk_r(offs_t offset);
	void fsk_w(offs_t offset, u8 data);

	// F000: TVF (Time Variant Filter)
	u16 tvf_r(offs_t offset);
	void tvf_w(offs_t offset, u16 data, u16 mem_mask = ~0);

	// Gate array DMA registers (SFR 0x118-0x11D)
	void dma_reg_w(offs_t offset, u16 data, u16 mem_mask = ~0);
	u16 dma_reg_r(offs_t offset, u16 mem_mask = ~0);

	// Gate array DMA controller (FDC <-> RAM)
	void dma_drq_w(int state);
	void bcc_dma_irq();

	// BCC gate array interrupt controller
	// The BCC multiplexes KEY, FDC, FSK, and DMA-complete onto the
	// CPU's EXTINT pin.  When the CPU fetches the EXTINT vector from
	// the vector table the BCC intercepts the read and adds a device-
	// specific offset to the base vector stored in ROM at 0x203A:
	//   KEY +4, FDC +8, FSK +0xC, DMA +0x10
	static constexpr offs_t BCC_BASE_VECTOR_ADDR = 0x203a;   // PTS EXTINT vector in ROM
	static constexpr u16 BCC_KEY_OFFSET = 0x04;
	static constexpr u16 BCC_FDC_OFFSET = 0x08;
	static constexpr u16 BCC_FSK_OFFSET = 0x0c;
	static constexpr u16 BCC_DMA_OFFSET = 0x10;
	void bcc_fdc_irq_w(int state);

	// CPU port I/O
	u16 ach0_r();
	void hso_w(offs_t offset, u8 data, u8 mem_mask);
	u8 port0_r();
	u8 port1_r();
	void port1_w(u8 data);
	u8 port2_r();
	void port2_w(u8 data);

	// MIDI
	void midi_in_w(int state);
	TIMER_DEVICE_CALLBACK_MEMBER(midi_timer_cb);

	required_device<i80c196_device> m_maincpu;
	required_region_ptr<u16> m_rom;
	memory_share_creator<u16> m_ram;

	memory_view m_view0;   // 0x0000-0x3FFF
	memory_view m_view1;   // 0x4000-0x7FFF
	memory_view m_view2;   // 0x8000-0xBFFF
	memory_view m_view3;   // 0xC000-0xFFFF

	u16 m_bank_reg[8];     // 0x100-0x10E: execute[0..3], data[4..7]

	required_ioport_array<8> m_keys;
	required_ioport_array<8> m_sliders;
	required_ioport m_encoder;

	// I/O devices
	required_device<upd72068_device> m_fdc;
	optional_device<floppy_connector> m_floppy;
	required_device<mb87419_mb87420_device> m_pcm;
	required_device<t6963c_device> m_lcd;
	required_device<timer_device> m_midi_timer;

	unsigned m_ram_size;   // effective RAM size in bytes (512KB or 1MB)

	// TC23SC060 key scan / LED gate array state
	u8 m_keyscan_cmd;              // last command written to C800
	u8 m_keyscan_prev[8];          // previous column scan state (for edge detection)
	u8 m_keyscan_fifo[16];         // key event FIFO (6-bit keycodes)
	u8 m_keyscan_fifo_head;        // FIFO read pointer
	u8 m_keyscan_fifo_tail;        // FIFO write pointer
	bool m_keyscan_enabled;        // autonomous scanning enabled (config 0x70 bit 7)
	emu_timer *m_keyscan_timer;    // periodic scan timer

	// Slider mux state
	u8 m_port1_out;

	// Rotary encoder state
	u8 m_encoder_last;     // last read encoder value (to detect direction)
	bool m_encoder_moved;  // Latch 2 output: set on encoder step, cleared by HSO2
	bool m_encoder_dir;    // Latch 1 output: direction of last step

	// LP-1 PCM interrupt (active on PORT0 bit 7)
	int m_pcm_irq;

	// MIDI bit-bang state
	std::queue<u8> m_midi_queue;
	u8 m_midi_rx;
	int m_midi_pos;

	// Gate array DMA registers (SFR 0x118-0x11D)
	u16 m_dma_count;      // 0x118: transfer byte count (decrements to 0)
	u16 m_dma_adr_lo;     // 0x11A: low 16 bits of physical RAM byte address
	u16 m_dma_adr_hi;     // 0x11C: high bits of address (or control; purpose TBD)

	// BCC interrupt controller state
	u8 m_bcc_irq_source;   // Currently active BCC interrupt source (0=none)
	u8 m_bcc_irq_pending;  // Bitmask of pending BCC sources
	void bcc_update_extint();
	void bcc_assert_source(u8 source);
	void bcc_clear_source(u8 source);
	static constexpr u8 BCC_NONE = 0;
	static constexpr u8 BCC_KEY  = 1;
	static constexpr u8 BCC_FDC  = 2;
	static constexpr u8 BCC_FSK  = 3;
	static constexpr u8 BCC_DMA  = 4;
};


void roland_mv30_state::mem_map(address_map &map)
{
	// Banking registers at SFR 0x100-0x10F
	map(0x0100, 0x010f).rw(FUNC(roland_mv30_state::bank_r), FUNC(roland_mv30_state::bank_w));

	// Gate array DMA registers
	map(0x0118, 0x011d).rw(FUNC(roland_mv30_state::dma_reg_r), FUNC(roland_mv30_state::dma_reg_w));

	// Four 16KB banking windows covering the entire 64KB address space.
	// Each window can present ROM, RAM, or I/O depending on the bank
	// register settings.  At boot, ROM needs to be visible at 0x2080+
	// for the MCS-96 reset vector.
	//
	// Note: 0x0000-0x00FF is the CPU internal register file and takes
	// priority over external memory for data access.  Instruction fetch
	// at those addresses goes to external memory (ROM/RAM).
	//
	// RAM view entries use lambdas that read m_bank_reg[] dynamically,
	// so changing the bank register is enough — no install_ram needed.

	map(0x0000, 0x3fff).view(m_view0);
	m_view0[0](0x0000, 0x3fff).rom().region("maincpu", 0);          // ROM (boot)
	// BCC gate array registers visible through program space regardless of bank view.
	m_view0[0](0x0100, 0x010f).rw(FUNC(roland_mv30_state::bank_r), FUNC(roland_mv30_state::bank_w));
	m_view0[0](0x0118, 0x011d).rw(FUNC(roland_mv30_state::dma_reg_r), FUNC(roland_mv30_state::dma_reg_w));
	// BCC gate array: intercept EXTINT1 vector fetch at 0x203A.
	// The CPU's fetch_196_full() dispatches EXTINT1 by reading the
	// vector at 0x203A.  The BCC adds a device-specific offset to
	// the base vector (KEY +4, FDC +8, FSK +0xC, DMA +0x10).
	m_view0[0](0x203a, 0x203b).lr16(
		NAME([this](offs_t offset) -> u16 {
			if (m_bcc_irq_source != BCC_NONE)
			{
				u16 base = m_rom[BCC_BASE_VECTOR_ADDR / 2];
				u16 vec_offset = 0;
				switch (m_bcc_irq_source)
				{
				case BCC_KEY: vec_offset = BCC_KEY_OFFSET; break;
				case BCC_FDC: vec_offset = BCC_FDC_OFFSET; break;
				case BCC_FSK: vec_offset = BCC_FSK_OFFSET; break;
				case BCC_DMA: vec_offset = BCC_DMA_OFFSET; break;
				}
				return u16(base + vec_offset);
			}
			return m_rom[BCC_BASE_VECTOR_ADDR / 2];
		})
	);
	m_view0[1](0x0000, 0x3fff).lrw16(                               // banked RAM
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[4]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		}),
		NAME([this](offs_t offset, u16 data, u16 mem_mask) {
			u32 idx = (u32(m_bank_reg[4]) << (PAGE_SHIFT - 1)) + offset;
			if (idx < m_ram_size / 2) COMBINE_DATA(&m_ram[idx]);
		})
	);
	m_view0[1](0x0100, 0x010f).rw(FUNC(roland_mv30_state::bank_r), FUNC(roland_mv30_state::bank_w));
	m_view0[1](0x0118, 0x011d).rw(FUNC(roland_mv30_state::dma_reg_r), FUNC(roland_mv30_state::dma_reg_w));
	// BCC vector intercept must also be present in RAM view — the BCC
	// gate array always intercepts this address regardless of banking.
	m_view0[1](0x203a, 0x203b).lr16(
		NAME([this](offs_t offset) -> u16 {
			if (m_bcc_irq_source != BCC_NONE)
			{
				u16 base = m_rom[BCC_BASE_VECTOR_ADDR / 2];
				u16 vec_offset = 0;
				switch (m_bcc_irq_source)
				{
				case BCC_KEY: vec_offset = BCC_KEY_OFFSET; break;
				case BCC_FDC: vec_offset = BCC_FDC_OFFSET; break;
				case BCC_FSK: vec_offset = BCC_FSK_OFFSET; break;
				case BCC_DMA: vec_offset = BCC_DMA_OFFSET; break;
				}
				return u16(base + vec_offset);
			}
			return m_rom[BCC_BASE_VECTOR_ADDR / 2];
		})
	);

	map(0x4000, 0x7fff).view(m_view1);
	m_view1[0](0x4000, 0x7fff).lrw16(                               // banked RAM
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[5]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		}),
		NAME([this](offs_t offset, u16 data, u16 mem_mask) {
			u32 idx = (u32(m_bank_reg[5]) << (PAGE_SHIFT - 1)) + offset;
			if (idx < m_ram_size / 2) COMBINE_DATA(&m_ram[idx]);
		})
	);

	map(0x8000, 0xbfff).view(m_view2);
	m_view2[0](0x8000, 0xbfff).lrw16(                               // banked RAM
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[6]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		}),
		NAME([this](offs_t offset, u16 data, u16 mem_mask) {
			u32 idx = (u32(m_bank_reg[6]) << (PAGE_SHIFT - 1)) + offset;
			if (idx < m_ram_size / 2) COMBINE_DATA(&m_ram[idx]);
		})
	);

	map(0xc000, 0xffff).view(m_view3);
	m_view3[0](0xc000, 0xffff).lrw16(                               // banked RAM
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[7]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		}),
		NAME([this](offs_t offset, u16 data, u16 mem_mask) {
			u32 idx = (u32(m_bank_reg[7]) << (PAGE_SHIFT - 1)) + offset;
			if (idx < m_ram_size / 2) COMBINE_DATA(&m_ram[idx]);
		})
	);
	// I/O view: devices mapped 0x800 apart
	m_view3[1](0xc000, 0xc7ff).rw(FUNC(roland_mv30_state::rcc_r), FUNC(roland_mv30_state::rcc_w));
	m_view3[1](0xc800, 0xc801).rw(FUNC(roland_mv30_state::keyscan_r), FUNC(roland_mv30_state::keyscan_w));
	m_view3[1](0xd000, 0xd001).m(m_fdc, FUNC(upd72068_device::map));
	m_view3[1](0xd800, 0xd81f).rw(m_pcm, FUNC(mb87419_mb87420_device::read), FUNC(mb87419_mb87420_device::write));
	m_view3[1](0xe000, 0xe003).rw(FUNC(roland_mv30_state::fsk_r), FUNC(roland_mv30_state::fsk_w));
	m_view3[1](0xe800, 0xe802).rw(m_lcd, FUNC(t6963c_device::read), FUNC(t6963c_device::write)).umask16(0x00ff);
	m_view3[1](0xf000, 0xf0ff).rw(FUNC(roland_mv30_state::tvf_r), FUNC(roland_mv30_state::tvf_w));
}


// Opcodes (instruction fetch) address map.
// The 80C196KB has separate execute and data bank registers.
// Execute banks always map to ROM or RAM (never I/O), so
// instruction fetch uses this simpler map while data access
// uses mem_map above.
void roland_mv30_state::opcodes_map(address_map &map)
{
	// Window 0 (0x0000-0x3FFF): ROM at reset, or execute-bank-0 RAM
	map(0x0000, 0x3fff).lr16(
		NAME([this](offs_t offset) -> u16 {
			u16 page = m_bank_reg[0];
			if (page == ROM_PAGE)
				return m_rom[offset];
			u32 idx = (u32(page) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		})
	);

	// Window 1 (0x4000-0x7FFF)
	map(0x4000, 0x7fff).lr16(
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[1]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		})
	);

	// Window 2 (0x8000-0xBFFF)
	map(0x8000, 0xbfff).lr16(
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[2]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		})
	);

	// Window 3 (0xC000-0xFFFF)
	map(0xc000, 0xffff).lr16(
		NAME([this](offs_t offset) -> u16 {
			u32 idx = (u32(m_bank_reg[3]) << (PAGE_SHIFT - 1)) + offset;
			return (idx < m_ram_size / 2) ? m_ram[idx] : 0;
		})
	);
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
	save_item(NAME(m_keyscan_cmd));
	save_item(NAME(m_keyscan_prev));
	save_item(NAME(m_keyscan_fifo));
	save_item(NAME(m_keyscan_fifo_head));
	save_item(NAME(m_keyscan_fifo_tail));
	save_item(NAME(m_keyscan_enabled));
	save_item(NAME(m_port1_out));

	m_keyscan_timer = timer_alloc(FUNC(roland_mv30_state::keyscan_scan_tick), this);
	save_item(NAME(m_encoder_last));
	save_item(NAME(m_encoder_moved));
	save_item(NAME(m_encoder_dir));
	save_item(NAME(m_midi_rx));
	save_item(NAME(m_midi_pos));
	save_item(NAME(m_dma_count));
	save_item(NAME(m_dma_adr_lo));
	save_item(NAME(m_dma_adr_hi));
	save_item(NAME(m_bcc_irq_source));
	save_item(NAME(m_bcc_irq_pending));
	save_item(NAME(m_ram_size));
}


void roland_mv30_state::machine_reset()
{
	// Startup banking state (inferred from hardware behavior):
	//   Execute 0000-3FFF = ROM,  Data 0000-3FFF = RAM page 0
	//   Data 4000-7FFF = RAM page 0x010
	//   Data 8000-BFFF = RAM page 0x020
	//   Data C000-FFFF = I/O (page 0x474, default power-on state)

	m_bank_reg[0] = ROM_PAGE;    // execute 0x0000-0x3FFF -> ROM
	m_bank_reg[1] = 0;           // execute 0x4000-0x7FFF -> unknown
	m_bank_reg[2] = 0;           // execute 0x8000-0xBFFF -> unknown
	m_bank_reg[3] = 0;           // execute 0xC000-0xFFFF -> unknown
	m_bank_reg[4] = 0x000;       // data 0x0000-0x3FFF -> RAM offset 0
	m_bank_reg[5] = 0x010;       // data 0x4000-0x7FFF -> RAM offset 0x4000
	m_bank_reg[6] = 0x020;       // data 0x8000-0xBFFF -> RAM offset 0x8000
	m_bank_reg[7] = IO_PAGE;     // data 0xC000-0xFFFF -> I/O (power-on default)

	m_keyscan_cmd = 0;
	std::fill(std::begin(m_keyscan_prev), std::end(m_keyscan_prev), 0);
	std::fill(std::begin(m_keyscan_fifo), std::end(m_keyscan_fifo), 0);
	m_keyscan_fifo_head = 0;
	m_keyscan_fifo_tail = 0;
	m_keyscan_enabled = false;
	m_port1_out = 0;
	m_encoder_last = 0;
	m_encoder_moved = false;
	m_encoder_dir = false;
	m_pcm_irq = 0;
	m_midi_rx = 0;
	m_midi_pos = 0;
	std::queue<u8>().swap(m_midi_queue);

	m_dma_count = 0;
	m_dma_adr_lo = 0;
	m_dma_adr_hi = 0;
	m_bcc_irq_source = BCC_NONE;
	m_bcc_irq_pending = 0;

	m_ram_size = (ioport("RAMSIZE")->read()) ? 1024 * 1024 : 512 * 1024;

	// Window 0 starts showing ROM for instruction fetch at reset
	m_view0.select(0);
	m_view1.select(0);
	m_view2.select(0);
	// Window 3 starts in I/O mode (firmware confirms with 0x474 write later)
	m_view3.select(1);
}


u16 roland_mv30_state::bank_r(offs_t offset, u16 mem_mask)
{
	return m_bank_reg[offset];
}


void roland_mv30_state::bank_w(offs_t offset, u16 data, u16 mem_mask)
{
	COMBINE_DATA(&m_bank_reg[offset]);

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

	// Window 3: check for I/O mapping
	if (window == 3 && page == IO_PAGE)
	{
		m_view3.select(1);   // I/O view
		return;
	}

	// Select the RAM view — the lambda reads m_bank_reg[] dynamically,
	// so just switching the view entry is sufficient.
	switch (window)
	{
	case 0: m_view0.select(1); break;   // view 1 = RAM (view 0 = ROM)
	case 1: m_view1.select(0); break;
	case 2: m_view2.select(0); break;
	case 3: m_view3.select(0); break;   // view 0 = RAM (view 1 = I/O)
	}
}


//
// I/O device handlers
//

// C000-C7FF: Reverb/Effect unit - Roland RCC (TC23SC104AF-007)
u8 roland_mv30_state::rcc_r(offs_t offset)
{
	return 0;
}

void roland_mv30_state::rcc_w(offs_t offset, u8 data)
{
}

// C800-C8FF: Key scan / LED gate array - Roland TC23SC060
//
// The gate array has two operating modes:
//
// 1. Poll mode (commands 0x00-0x07): Write column number to C800, then
//    read raw row data from C801.  Used by the firmware's KeyscanPollButtons
//    to detect PLAY/STOP, REC, and CTRL button transitions.
//
// 2. Interrupt mode (command 0x78): The gate array autonomously scans the
//    button matrix.  When a key press is detected, it queues a 6-bit keycode
//    (column*8 + bit) in an internal FIFO and asserts the BCC KEY interrupt.
//    The ISR writes 0x78 to C800, then reads C801 to get the event:
//      bit 7 = event pending, bits 5:0 = 6-bit keycode.
//    If bit 7 is clear, the FIFO is empty.
//
// Configuration registers (0x70-0x7B) are written by sending the register
// number to C800 then the value to C801.  Config 0x70 bit 7 enables
// autonomous scanning.
//
// LED registers (0x08-0x17) work similarly: register number to C800,
// LED data to C801.
//
void roland_mv30_state::keyscan_w(offs_t offset, u8 data)
{
	if (offset == 0)
	{
		// Command/address register
		m_keyscan_cmd = data;
	}
	else
	{
		// Data register write — behavior depends on current command
		if (m_keyscan_cmd == 0x70)
		{
			// Config register 0x70: bit 7 = enable autonomous scanning
			bool was_enabled = m_keyscan_enabled;
			m_keyscan_enabled = BIT(data, 7);
			if (m_keyscan_enabled && !was_enabled)
			{
				// Snapshot current key state so we only report transitions
				for (int col = 0; col < 8; col++)
					m_keyscan_prev[col] = m_keys[col]->read();
				m_keyscan_timer->adjust(attotime::from_msec(10), 0, attotime::from_msec(10));
			}
			else if (!m_keyscan_enabled && was_enabled)
			{
				m_keyscan_timer->adjust(attotime::never);
			}
		}
		// Other config/LED register writes are ignored for now
	}
}

u8 roland_mv30_state::keyscan_r(offs_t offset)
{
	if (offset == 1)
	{
		if (m_keyscan_cmd < 8)
		{
			// Poll mode: return raw row data for selected column
			return m_keys[m_keyscan_cmd]->read();
		}
		else if (m_keyscan_cmd == 0x78)
		{
			// Interrupt mode: read key event from FIFO
			if (m_keyscan_fifo_head != m_keyscan_fifo_tail)
			{
				u8 keycode = m_keyscan_fifo[m_keyscan_fifo_head];
				m_keyscan_fifo_head = (m_keyscan_fifo_head + 1) & 0x0f;
				keyscan_update_irq();
				return 0x80 | keycode;  // bit 7 = event pending
			}
			return 0x00;  // no event
		}
	}
	return 0x00;
}

// Periodic timer: scan the button matrix for changes and queue events
TIMER_CALLBACK_MEMBER(roland_mv30_state::keyscan_scan_tick)
{
	for (int col = 0; col < 8; col++)
	{
		u8 cur = m_keys[col]->read();
		u8 changed = cur ^ m_keyscan_prev[col];
		if (changed)
		{
			for (int bit = 0; bit < 8; bit++)
			{
				if (BIT(changed, bit) && BIT(cur, bit))
				{
					// Key press detected — queue 6-bit keycode
					u8 fifo_next = (m_keyscan_fifo_tail + 1) & 0x0f;
					if (fifo_next != m_keyscan_fifo_head)  // not full
					{
						m_keyscan_fifo[m_keyscan_fifo_tail] = u8(col * 8 + bit);
						m_keyscan_fifo_tail = fifo_next;
					}
				}
			}
			m_keyscan_prev[col] = cur;
		}
	}
	keyscan_update_irq();
}

// Update BCC KEY interrupt based on FIFO state
void roland_mv30_state::keyscan_update_irq()
{
	if (m_keyscan_fifo_head != m_keyscan_fifo_tail)
		bcc_assert_source(BCC_KEY);
	else
		bcc_clear_source(BCC_KEY);
}

//
// Gate array DMA registers (SFR 0x118-0x11D)
//
// The custom gate array acts as a DMA controller between the uPD72068 FDC
// and system RAM.  Three word-size registers control the transfer:
//
//   0x118: Transfer byte count – decremented per byte transferred.
//          DMA is active while this is non-zero.
//   0x11A: Low 16 bits of the physical RAM byte address (auto-incremented).
//   0x11C: Byte at 0x11C is the high address bits (bits 16+).
//          Byte at 0x11D is a DMA control register (the firmware writes
//          0x87 for FDC→RAM reads: bit 7 = enable, other bits TBD).
//          Physical address = ((0x11C & 0xFF) << 16) | 0x11A.
//

u16 roland_mv30_state::dma_reg_r(offs_t offset, u16 mem_mask)
{
	switch (offset)
	{
	case 0: return m_dma_count;
	case 1: return m_dma_adr_lo;
	case 2:
		// Reading 0x11D (high byte) acknowledges the DMA-complete interrupt.
		if (mem_mask & 0xff00)
			bcc_clear_source(BCC_DMA);
		return m_dma_adr_hi;
	}
	return 0;
}

void roland_mv30_state::dma_reg_w(offs_t offset, u16 data, u16 mem_mask)
{
	switch (offset)
	{
	case 0:
		COMBINE_DATA(&m_dma_count);
		break;
	case 1:
		COMBINE_DATA(&m_dma_adr_lo);
		break;
	case 2:
		COMBINE_DATA(&m_dma_adr_hi);
		break;
	}
}

//
// Gate array DMA controller
//
// When the FDC asserts DRQ, the gate array transfers one byte per DRQ
// between the FDC data register and physical RAM, incrementing the
// address and decrementing the count.  Transfer stops when count hits 0.
//
void roland_mv30_state::dma_drq_w(int state)
{
	if (!state)
		return;

	while (m_fdc->get_drq())
	{
		u8 byte = m_fdc->dma_r();

		if (m_dma_count > 0)
		{
			// Physical address: low byte of adr_hi provides bits 16+,
			// high byte of adr_hi is a control register (0x87 = enable).
			u32 phys = (u32(m_dma_adr_hi & 0xff) << 16) | m_dma_adr_lo;

			// FDC -> RAM (reading from floppy)
			if (phys < m_ram_size)
			{
				u32 word_idx = phys >> 1;
				if (phys & 1)
					m_ram[word_idx] = (m_ram[word_idx] & 0x00ff) | (u16(byte) << 8);
				else
					m_ram[word_idx] = (m_ram[word_idx] & 0xff00) | byte;
			}

			// Increment address, decrement count; preserve control byte
			u32 next = phys + 1;
			m_dma_adr_lo = u16(next);
			m_dma_adr_hi = (m_dma_adr_hi & 0xff00) | u8(next >> 16);
			m_dma_count--;

			// When count reaches 0, the BCC gate array fires INT3
			// (the "FSK" interrupt).  The firmware's FSK handler
			// asserts TC to the FDC via PORT2 bit 5.
			if (m_dma_count == 0)
				bcc_dma_irq();
		}
		// After count reaches 0, keep consuming DRQ bytes (discard)
		// to prevent FDC FIFO overrun while the FDC finishes.
	}
}


//
// BCC gate array interrupt controller
//
// Routes FDC, KEY, FSK, and DMA-complete interrupts to the CPU EXTINT
// pin, modifying the fetched vector to dispatch to the correct handler.
//
// The BCC latches pending IRQs.  When the current source is cleared
// (acknowledged), any remaining pending source takes over.
//

void roland_mv30_state::bcc_update_extint()
{
	if (m_bcc_irq_source != BCC_NONE)
		m_maincpu->set_input_line(i8x9x_device::EXTINT_LINE, ASSERT_LINE);
	else
		m_maincpu->set_input_line(i8x9x_device::EXTINT_LINE, CLEAR_LINE);
}

void roland_mv30_state::bcc_clear_source(u8 source)
{
	m_bcc_irq_pending &= ~(1 << source);
	if (m_bcc_irq_source == source)
	{
		// Deassert EXTINT first so the next assert creates a rising edge
		m_bcc_irq_source = BCC_NONE;
		m_maincpu->set_input_line(i8x9x_device::EXTINT_LINE, CLEAR_LINE);

		// Promote next pending source (priority: DMA > FDC > FSK > KEY)
		for (int i = BCC_DMA; i >= BCC_KEY; i--)
		{
			if (m_bcc_irq_pending & (1 << i))
			{
				m_bcc_irq_source = i;
				break;
			}
		}
	}
	bcc_update_extint();
}

void roland_mv30_state::bcc_assert_source(u8 source)
{
	m_bcc_irq_pending |= (1 << source);
	// Higher-numbered sources have higher priority
	if (source >= m_bcc_irq_source)
		m_bcc_irq_source = source;
	bcc_update_extint();
}

void roland_mv30_state::bcc_fdc_irq_w(int state)
{
	if (state)
		bcc_assert_source(BCC_FDC);
	else
		bcc_clear_source(BCC_FDC);
}

//
// BCC gate array DMA-complete interrupt
//
// When the DMA transfer count reaches 0, the BCC fires an interrupt
// routed via vector offset +0x10.  The firmware's handler asserts TC
// to the FDC via PORT2 bit 5 and signals the main loop via R70 bit 4.
//
void roland_mv30_state::bcc_dma_irq()
{
	bcc_assert_source(BCC_DMA);
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

// F000-F07F: TVF (Time Variant Filter) - MB654419U
u16 roland_mv30_state::tvf_r(offs_t offset)
{
	logerror("tvf_r: offset %02x\n", offset);
	return 0;
}

void roland_mv30_state::tvf_w(offs_t offset, u16 data, u16 mem_mask)
{
	logerror("tvf_w: offset %02x = %04x & %04x\n", offset, data, mem_mask);
}

//
// CPU port handlers
//

// ACH0 (P00): analog slider input via 8-way mux selected by P10-P12
u16 roland_mv30_state::ach0_r()
{
	u8 mux = m_port1_out & 0x07;       // P10-P12 = mux select
	bool inhibit = BIT(m_port1_out, 3); // P13 = mux inhibit
	if (inhibit)
		return 0x80;  // mux disabled, return midpoint
	return m_sliders[mux]->read();
}

// PORT0 (SFR 0x0E): encoder latch signals + LP-1 PCM interrupt
//   P0.5 = encoder direction (0 = CW, 1 = CCW)
//   P0.6 = encoder "moved" flag (1 = encoder turned since last HSO2 reset)
//   P0.7 = LP-1 PCM interrupt (directly wired from LP-1 IRQ output)
//
// The hardware uses external latch flip-flops to capture the quadrature
// encoder signals and present a moved + direction interface to the CPU.
// Reading PORT0 samples the current latch state.
u8 roland_mv30_state::port0_r()
{
	// Sample the encoder for any change since last HSO2 reset
	u8 cur = m_encoder->read();
	if (cur != m_encoder_last)
	{
		s8 delta = s8(cur - m_encoder_last);
		m_encoder_dir = (delta > 0);   // true = CW
		m_encoder_moved = true;
		m_encoder_last = cur;
	}

	u8 val = 0;
	if (m_encoder_moved)
		val |= (1 << 6);    // P0.6 = moved
	if (!m_encoder_dir)     // P0.5: 0 = CW, 1 = CCW
		val |= (1 << 5);
	if (m_pcm_irq)          // P0.7 = LP-1 PCM interrupt
		val |= (1 << 7);
	logerror("read p0 = %02X (irq=%d)\n", val, m_pcm_irq);
	return val;
}

// HSO callback: HSO2 (bit 2) rising edge resets the encoder latch
void roland_mv30_state::hso_w(offs_t offset, u8 data, u8 mem_mask)
{
	if (BIT(mem_mask, 2))
	{
		if (BIT(data, 2))
			m_encoder_moved = false;   // HSO2 high = reset latch
	}
}

// Port 1 read: directly read back the last written value
u8 roland_mv30_state::port1_r()
{
	return m_port1_out;
}

// Port 1 write: P10-P12 = slider mux select, P13 = mux inhibit
void roland_mv30_state::port1_w(u8 data)
{
	m_port1_out = data;
}

// Port 2 read: floppy status signals
//   P23 = /READY, P24 = /DSKCHG
u8 roland_mv30_state::port2_r()
{
	u8 val = 0xff;
	floppy_image_device *floppy = m_floppy ? m_floppy->get_device() : nullptr;
	if (floppy)
	{
		if (!floppy->ready_r())  // ready_r() returns false when ready (active low)
			val &= ~(1 << 3);   // P23 = /READY asserted (low)
		if (!floppy->dskchg_r())
			val &= ~(1 << 4);   // P24 = /DSKCHG asserted (low)
	}
	return val;
}

// Port 2 write: FDC terminal count and drive select
//   P25 = TC (to FDC), P26 = /DRVSEL (active low)
void roland_mv30_state::port2_w(u8 data)
{
	m_fdc->tc_w(BIT(data, 5));    // P25 → FDC TC

	// P26 is drive select, active low.  The motor is controlled by the
	// FDC's enable-motors aux command, not by this pin.
	floppy_image_device *floppy = m_floppy ? m_floppy->get_device() : nullptr;
	if (floppy)
		floppy->ds_w(BIT(data, 6) ? -1 : 0);  // P26 → /DRVSEL
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
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PTN EDIT") PORT_CODE(KEYCODE_BACKSLASH)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Up") PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Enter") PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CTRL") PORT_CODE(KEYCODE_LCONTROL)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("0") PORT_CODE(KEYCODE_0)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("1/9") PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("JUMP") PORT_CODE(KEYCODE_LSHIFT)

	PORT_START("SC1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TRK EDIT") PORT_CODE(KEYCODE_L)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Left") PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("3") PORT_CODE(KEYCODE_3)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("COMPU") PORT_CODE(KEYCODE_C)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("2") PORT_CODE(KEYCODE_2)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("2/10") PORT_CODE(KEYCODE_W)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F1") PORT_CODE(KEYCODE_F1)

	PORT_START("SC2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SYSTEM") PORT_CODE(KEYCODE_H)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Right") PORT_CODE(KEYCODE_RIGHT)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("6") PORT_CODE(KEYCODE_6)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MANUAL") PORT_CODE(KEYCODE_V)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("5") PORT_CODE(KEYCODE_5)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("3/11") PORT_CODE(KEYCODE_E)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F2") PORT_CODE(KEYCODE_F2)

	PORT_START("SC3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("DISK") PORT_CODE(KEYCODE_D)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Down") PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("9") PORT_CODE(KEYCODE_9)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SONG SELECT") PORT_CODE(KEYCODE_B)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("8") PORT_CODE(KEYCODE_8)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("4/12") PORT_CODE(KEYCODE_R)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F3") PORT_CODE(KEYCODE_F3)

	PORT_START("SC4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PTN MICROSCOPE") PORT_CODE(KEYCODE_QUOTE)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PTN REALTIME") PORT_CODE(KEYCODE_COLON)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("INS") PORT_CODE(KEYCODE_INSERT)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("REC") PORT_CODE(KEYCODE_O)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("STATUS") PORT_CODE(KEYCODE_N)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("1") PORT_CODE(KEYCODE_1)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("5/13") PORT_CODE(KEYCODE_T)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F4") PORT_CODE(KEYCODE_F4)

	PORT_START("SC5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TRK MICROSCOPE") PORT_CODE(KEYCODE_K)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TRK REALTIME") PORT_CODE(KEYCODE_J)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("DEL") PORT_CODE(KEYCODE_DEL)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Rewind") PORT_CODE(KEYCODE_P)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("LOCATE") PORT_CODE(KEYCODE_M)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("4") PORT_CODE(KEYCODE_4)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("6/14") PORT_CODE(KEYCODE_Y)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F5") PORT_CODE(KEYCODE_F5)

	PORT_START("SC6")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TIMBRE EDIT") PORT_CODE(KEYCODE_G)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("COMPU MIX") PORT_CODE(KEYCODE_F)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Play/Stop") PORT_CODE(KEYCODE_SPACE)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("MARK") PORT_CODE(KEYCODE_COMMA)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("7") PORT_CODE(KEYCODE_7)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("7/15") PORT_CODE(KEYCODE_U)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("EXIT") PORT_CODE(KEYCODE_BACKSPACE)

	PORT_START("SC7")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("CHAIN PLAY") PORT_CODE(KEYCODE_S)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("PLAY") PORT_CODE(KEYCODE_A)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Fast Forward") PORT_CODE(KEYCODE_OPENBRACE)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("TEMPO") PORT_CODE(KEYCODE_STOP)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("SHIFT") PORT_CODE(KEYCODE_RSHIFT)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("8/16") PORT_CODE(KEYCODE_I)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

	PORT_START("SL0")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 1/9") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL1")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 2/10") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL2")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 3/11") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL3")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 4/12") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL4")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 5/13") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL5")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 6/14") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL6")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 7/15") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("SL7")
	PORT_BIT(0xff, 0x00, IPT_PADDLE) PORT_NAME("Level 8/16") PORT_SENSITIVITY(10) PORT_KEYDELTA(10) PORT_MINMAX(0x00, 0xff)

	PORT_START("ENCODER")
	PORT_BIT(0xff, 0x00, IPT_DIAL) PORT_NAME("Data Dial") PORT_SENSITIVITY(25) PORT_KEYDELTA(1) PORT_CODE_DEC(KEYCODE_Z) PORT_CODE_INC(KEYCODE_X)

	PORT_START("RAMSIZE")
	PORT_CONFNAME(0x01, 0x00, "RAM Size")
	PORT_CONFSETTING(0x00, "512KB")
	PORT_CONFSETTING(0x01, "1MB (Expanded)")
INPUT_PORTS_END


static void mv30_floppies(device_slot_interface &device) ATTR_COLD;
static void mv30_floppies(device_slot_interface &device)
{
	device.option_add("35dd", FLOPPY_35_DD);
}

static void mv30_floppy_formats(format_registration &fr) ATTR_COLD;
static void mv30_floppy_formats(format_registration &fr)
{
	fr.add_mfm_containers();
	fr.add_pc_formats();
}

void roland_mv30_state::mv30(machine_config &config)
{
	// CPU: Intel 80C196KB-12
	C80C196KB(config, m_maincpu, 12_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &roland_mv30_state::mem_map);
	m_maincpu->set_addrmap(AS_OPCODES, &roland_mv30_state::opcodes_map);
	m_maincpu->serial_tx_cb().set("mdout", FUNC(midi_port_device::write_txd));
	m_maincpu->ach0_cb().set(FUNC(roland_mv30_state::ach0_r));
	m_maincpu->hso_cb().set(FUNC(roland_mv30_state::hso_w));
	m_maincpu->in_p0_cb().set(FUNC(roland_mv30_state::port0_r));
	m_maincpu->in_p1_cb().set(FUNC(roland_mv30_state::port1_r));
	m_maincpu->out_p1_cb().set(FUNC(roland_mv30_state::port1_w));
	m_maincpu->in_p2_cb().set(FUNC(roland_mv30_state::port2_r));
	m_maincpu->out_p2_cb().set(FUNC(roland_mv30_state::port2_w));

	// FDC: NEC uPD72068GF-389
	UPD72068(config, m_fdc, 32_MHz_XTAL);
	m_fdc->intrq_wr_callback().set(FUNC(roland_mv30_state::bcc_fdc_irq_w));
	m_fdc->drq_wr_callback().set(FUNC(roland_mv30_state::dma_drq_w));
	FLOPPY_CONNECTOR(config, m_floppy, mv30_floppies, "35dd", mv30_floppy_formats);

	// PCM: Roland MB87419/MB87420 LP-1 sound engine (U-220 compatible)
	SPEAKER(config, "lspeaker").front_left();
	SPEAKER(config, "rspeaker").front_right();
	MB87419_MB87420(config, m_pcm, 32.768_MHz_XTAL);
	m_pcm->int_callback().set([this](int state) { m_pcm_irq = state; });
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

void roland_mv30_state::init_mv30()
{
	// Descramble PCM wave ROMs (same scrambling as D-70)
	u8 *src = reinterpret_cast<u8 *>(memregion("pcmorg")->base());
	u8 *dst = reinterpret_cast<u8 *>(memregion("pcm")->base());
	for (int bank = 0; bank < 6; bank++)
	{
		static const offs_t bank_offsets[] = { 0x000000, 0x080000, 0x100000, 0x180000, 0x200000, 0x300000 };
		offs_t base = bank_offsets[bank];
		for (offs_t srcpos = 0; srcpos < 0x80000; srcpos++)
			dst[base + UNSCRAMBLE_ADDR_INT(srcpos)] = UNSCRAMBLE_DATA(src[base + srcpos]);
	}
}


ROM_START(mv30)
	ROM_REGION16_LE(0x4000, "maincpu", 0)
	ROM_LOAD("mv30rom103.bin", 0x0000, 0x4000, CRC(15CC9474) SHA1(dc09c839c774e8190c0dbfaa3f2fce3049be735e))

	ROM_REGION(0x400, "lcd:cgrom", 0)
	ROM_LOAD("t6963c_0101.bin", 0x000, 0x400, CRC(547d118b) SHA1(0DD3E3ACD3D47E6ECE644C98C390FC86587373E9))

    ROM_REGION(0x600000, "pcmorg", 0) // ROMs before descrambling
	ROM_LOAD("roland_d70_waverom-a.bin", 0x000000, 0x80000, CRC(8e53b2a3) SHA1(4872530870d5079776e80e477febe425dc0ec1df))
	ROM_LOAD("roland_d70_waverom-e.bin", 0x080000, 0x80000, CRC(d46cc7a4) SHA1(d378ac89a5963e37f7c157b3c8e71892c334fd7b))
	ROM_LOAD("roland_d70_waverom-b.bin", 0x100000, 0x80000, CRC(c8220761) SHA1(49e55fa672020f95fd9c858ceaae94d6db93df7d))
	ROM_LOAD("roland_d70_waverom-f.bin", 0x180000, 0x80000, CRC(d4b01f5e) SHA1(acd867d68e49e5f59f1006ed14a7ca197b6dc4af))
	ROM_LOAD("roland_d70_waverom-c.bin", 0x200000, 0x80000, CRC(733c4054) SHA1(9b6b59ab74e5bf838702abb087c408aaa85b7b1f))
	ROM_LOAD("roland_d70_waverom-d.bin", 0x300000, 0x80000, CRC(b6c662d2) SHA1(3fcbcfd0d8d0fa419c710304c12482e2f79a907f))
	ROM_REGION(0x600000, "pcm", ROMREGION_ERASEFF) // ROMs after descrambling

ROM_END

} // anonymous namespace


SYST(1990, mv30, 0, 0, mv30, mv30, roland_mv30_state, init_mv30, "Roland", "MV-30 Studio M", MACHINE_NOT_WORKING)
