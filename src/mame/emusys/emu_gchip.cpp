// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu G-chip (IC402) — polyphony board sound engine

    Register interface (offsets from chip base, e.g. CSG1CHIP = 0x420000):

    Per-voice registers (64 voices × 0x40-byte stride):
      +$00..$3F per voice — voice parameters (playback address, pitch, etc.)
      Voice N base = N × 0x40

    Per-voice register map (byte offsets from voice base):
      +$00  R/W  Voice control/status — bit 12 read by firmware to select
                 register write order (oscillator phase/busy flag).
      +$04  R/W  Unknown (cleared on init and voice stop)
      +$08  R/W  Oscillator accumulator — bits[25:0] ones'-complemented sample
                 address/frequency. Upper 6 bits preserved (read-modify-write).
      +$0C  W    End address / amplitude — 32-bit, ones'-complemented.
      +$10  R/W  Oscillator mode/control (cleared on init)
      +$14  W    Trigger/enable — bit 26 = start oscillator.
      +$20–$2E   8 words (cleared on init; filter coefficients or volume?)

    Chip-wide registers (mirrored at every voice window, accessed via & 0x3f):
      +$1C  R   Sample data read — returns word from address set by +$30
      +$1E  W   Sample data write — stores word to address set by +$34
      +$30  W   Read address — word-addressed (firmware does asr.l #1, addr)
      +$34  W   Write address — word-addressed
      Note: The firmware's G-chip base pointer is NOT always at byte offset 0.
            For gchip1, the base is at ~0x1F000 within the device window.

    Global SIMM configuration registers:
      +$43E W   SIMM config A (timing)
      +$83E W   SIMM config B (bank/size; bit 8 = bank select)
      +$C3E W   SIMM type code

    During SIMM detection, the firmware:
    1. Programs config registers via sub_224E4
    2. Writes test pattern: voice N writes (N × 0x7531) to word address ((N-1) << 20 >> 1)
       via +$34 (write addr) and +$1E (write data), for N = 1..96
    3. Reads back via +$30 (read addr) and +$1C (read data, read twice for pipeline)
    4. Matching patterns determine SIMM size; largest valid config wins

******************************************************************************/

#include "emu.h"
#include "emu_gchip.h"

#define LOG_SAMPLE (1U << 1)
#define LOG_CONFIG (1U << 2)
#define LOG_VOICE  (1U << 3)
//#define VERBOSE (LOG_SAMPLE | LOG_CONFIG | LOG_VOICE)
#define VERBOSE (LOG_VOICE)
#include "logmacro.h"
#define LOGSAMPLE(...) LOGMASKED(LOG_SAMPLE, __VA_ARGS__)
#define LOGCONFIG(...) LOGMASKED(LOG_CONFIG, __VA_ARGS__)
#define LOGVOICE(...)  LOGMASKED(LOG_VOICE, __VA_ARGS__)

DEFINE_DEVICE_TYPE(EMU_GCHIP, emu_gchip_device, "emu_gchip", "E-mu G-chip Sound Engine")

emu_gchip_device::emu_gchip_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, EMU_GCHIP, tag, owner, clock)
	, device_memory_interface(mconfig, *this)
	, m_sample_space_config("sample", ENDIANNESS_BIG, 16, 27, 0) // 16-bit data, 27-bit byte address = 128 MB (64M words)
{
}

device_memory_interface::space_config_vector emu_gchip_device::memory_space_config() const
{
	return space_config_vector{ std::make_pair(0, &m_sample_space_config) };
}

void emu_gchip_device::device_start()
{
	space(0).specific(m_sample_space);

	save_item(NAME(m_regs));
	save_item(NAME(m_read_addr));
	save_item(NAME(m_write_addr));
	save_item(NAME(m_read_pipeline));
	save_item(NAME(m_simm_config_a));
	save_item(NAME(m_simm_config_b));
	save_item(NAME(m_simm_type));
}

void emu_gchip_device::device_reset()
{
	m_read_addr = 0;
	m_write_addr = 0;
	m_read_pipeline = 0;
	m_simm_config_a = 0;
	m_simm_config_b = 0;
	m_simm_type = 0;

	std::fill(std::begin(m_regs), std::end(m_regs), u16(0));
}

//
// Host CPU register interface
//
// The CSG1CHIP window is 0x20000 bytes (128 KB = 0x10000 words).
// Offsets are byte addresses from the chip select base.
//
// Special register offsets have side-effects (sample memory access,
// SIMM config). Everything else is plain read/write into m_regs[].
//
// The firmware accesses registers throughout the full 128 KB window,
// including high offsets for G-chip 2 detection (sub_23FB6).
//

u16 emu_gchip_device::read(offs_t offset)
{
	const u32 byte_off = offset * 2;

	// Sample data read register at +$1C within any voice window.
	// Pipeline delay: first read primes the latch, second returns data.
	// The firmware may access this through any voice window (e.g. gchip1
	// uses a base at ~0x1F000), so we mask to the voice-relative offset.
	if ((byte_off & 0x3f) == 0x1c)
	{
		u16 result = m_read_pipeline;
		m_read_pipeline = m_sample_space.read_word(m_read_addr * 2);
		LOGSAMPLE("read +$1C: pipeline=%04x, next from word_addr=%08x -> %04x\n", result, m_read_addr, m_read_pipeline);
		return result;
	}

	return m_regs[offset & (NUM_REGS - 1)];
}

void emu_gchip_device::write(offs_t offset, u16 data)
{
	const u32 byte_off = offset * 2;

	// SIMM configuration registers (can appear at +$43E, +$83E, +$C3E and
	// their +$1000 aliases for bank 2: +$143E, +$183E, +$1C3E)
	switch (byte_off & 0xfff)
	{
	case 0x43e:
		LOGCONFIG("SIMM config A = %04x (byte_off=%05x)\n", data, byte_off);
		m_simm_config_a = data;
		break;
	case 0x83e:
		LOGCONFIG("SIMM config B = %04x (byte_off=%05x)\n", data, byte_off);
		m_simm_config_b = data;
		break;
	case 0xc3e:
		LOGCONFIG("SIMM type = %04x (byte_off=%05x)\n", data, byte_off);
		m_simm_type = data;
		break;
	}

	// Sample memory access registers — appear at the same offsets within every
	// voice window.  The firmware's G-chip base pointer can sit anywhere in the
	// 128 KB register window (e.g. gchip1 base is at byte_off ~0x1F000), so we
	// mask to the 0x40-byte voice-relative offset to match.
	switch (byte_off & 0x3f)
	{
	case 0x1e: // Sample data write — writes to m_write_addr
		LOGSAMPLE("write +$1E: data=%04x -> word_addr=%08x\n", data, m_write_addr);
		m_sample_space.write_word(m_write_addr * 2, data);
		break;

	case 0x30: // Read address high word (word-addressed, 32-bit via move.l)
		m_read_addr = (u32(data) << 16) | (m_read_addr & 0xffff);
		LOGSAMPLE("read addr hi = %04x -> read_addr=%08x\n", data, m_read_addr);
		break;

	case 0x32: // Read address low word — triggers pipeline prime
		m_read_addr = (m_read_addr & 0xffff0000) | data;
		m_read_pipeline = m_sample_space.read_word(m_read_addr * 2);
		LOGSAMPLE("read addr lo = %04x -> read_addr=%08x, primed=%04x\n", data, m_read_addr, m_read_pipeline);
		break;

	case 0x34: // Write address high word (word-addressed, 32-bit via move.l)
		m_write_addr = (u32(data) << 16) | (m_write_addr & 0xffff);
		LOGSAMPLE("write addr hi = %04x -> write_addr=%08x\n", data, m_write_addr);
		break;

	case 0x36: // Write address low word
		m_write_addr = (m_write_addr & 0xffff0000) | data;
		LOGSAMPLE("write addr lo = %04x -> write_addr=%08x\n", data, m_write_addr);
		break;
	}

	// Per-voice register logging (64 voices × 0x40 bytes per voice window).
	// The firmware's G-chip base can be anywhere in the 128 KB window, so we
	// use (byte_off & 0x3f) to get the voice-relative register offset, and
	// derive voice number from the full byte_off modulo the voice stride.
	// The M68K writes 32-bit values as two 16-bit words (high word first via move.l).
	// We log on the low-word write when the complete 32-bit value is available.
	// The high word was stored to m_regs[] during the preceding write call.
	{
		const int vreg = byte_off & 0x3f;

		switch (vreg)
		{
		case 0x02: // Control/status low word (completes +$00/+$02 pair)
		{
			const u32 raw = (u32(m_regs[(offset - 1) & (NUM_REGS - 1)]) << 16) | data;
			LOGVOICE("V%02d control=%08x\n", (byte_off >> 6) & 0x3f, raw);
			break;
		}
		case 0x06: // Unknown low word (completes +$04/+$06 pair)
		{
			const u32 raw = (u32(m_regs[(offset - 1) & (NUM_REGS - 1)]) << 16) | data;
			LOGVOICE("V%02d reg04=%08x\n", (byte_off >> 6) & 0x3f, raw);
			break;
		}
		case 0x0a: // Oscillator accumulator low word (completes +$08/+$0A pair)
		{
			const u32 raw = (u32(m_regs[(offset - 1) & (NUM_REGS - 1)]) << 16) | data;
			const u32 addr = ~raw & 0x03ffffff; // ones'-complement, 26-bit
			LOGVOICE("V%02d osc acc raw=%08x addr=%07x\n", (byte_off >> 6) & 0x3f, raw, addr);
			break;
		}
		case 0x0e: // End/amplitude low word (completes +$0C/+$0E pair)
		{
			const u32 raw = (u32(m_regs[(offset - 1) & (NUM_REGS - 1)]) << 16) | data;
			LOGVOICE("V%02d end/amp raw=%08x val=%08x\n", (byte_off >> 6) & 0x3f, raw, ~raw);
			break;
		}
		case 0x16: // Trigger/mode low word (completes +$14/+$16 pair)
		{
			const u32 raw = (u32(m_regs[(offset - 1) & (NUM_REGS - 1)]) << 16) | data;
			LOGVOICE("V%02d trigger=%08x%s\n", (byte_off >> 6) & 0x3f, raw, (raw & (1U << 26)) ? " START" : "");
			break;
		}
		case 0x12: // Oscillator mode low word (completes +$10/+$12 pair)
		{
			const u32 raw = (u32(m_regs[(offset - 1) & (NUM_REGS - 1)]) << 16) | data;
			LOGVOICE("V%02d mode=%08x\n", (byte_off >> 6) & 0x3f, raw);
			break;
		}
		default:
			// Log other word writes in the voice filter/volume region (+$20..+$2E)
			// Skip +$30..+$3E which overlap sample address and SIMM config registers
			if ((vreg & 1) == 0 && vreg >= 0x20 && vreg <= 0x2e)
				LOGVOICE("V%02d +$%02x=%04x\n", (byte_off >> 6) & 0x3f, vreg, data);
			break;
		}
	}

	// Store to backing register file
	m_regs[offset & (NUM_REGS - 1)] = data;
}
