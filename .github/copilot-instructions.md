# E-mu E6400 Sampler — MAME Driver Development

This branch adds a MAME driver for the E-mu E6400 sampler (EOS generation). All work is concentrated in the `emusys/` machine driver directory and the `eosdocs/` reference directory.

## Key Files

- `src/mame/emusys/e6400.cpp` — Main MAME machine driver
- `src/mame/emusys/emu_gchip.h` — G-chip device header (sound engine / sample memory)
- `src/mame/emusys/emu_gchip.cpp` — G-chip device implementation
- `eosdocs/e6400_hardware.md` — Comprehensive hardware reference (register maps, boot sequence, schematics index)
- `eosdocs/eos30b.lst` — IDA Pro disassembly listing of the EOS 3.00b firmware (~800K lines). **Gitignored** — use `includeIgnoredFiles: true` when searching.

## Build & Run

```sh
make SUBTARGET=tiny -j12 DEBUG=1     # build (uses mametiny target list)
./mametinyd e6400                     # run in debug mode
./mametinyd e6400 -nofilter -window -resolution 960x256
```

The driver is registered in the `tiny` subtarget. ROM file: `roms/e6400/eos30b.raw`.

## Hardware Architecture

**CPU:** MC68EC020 @ 22.5792 MHz (45.1584 MHz XTAL ÷2 via D-latch toggle). 24-bit address bus.

**Clocks:**
- CPU: 45.1584 MHz ÷ 2 = 22.5792 MHz
- MFP: 16 MHz ÷ 4 = 4 MHz (HC393 Q1 divider)
- FDC: 24 MHz crystal (separate from CPU/MFP clocks)

**Peripherals (active when A23=0, A22=1, A21=0):**

| Range | Signal | Device |
|---|---|---|
| 0x400000 | CSWCR1 | Control register 1 (16-bit write) |
| 0x404000 | CSWCR2 | Control register 2 (16-bit write) |
| 0x408000 | CSMSR | Status register (bits[11:8]=variant, E6400=0x0500) |
| 0x40C000 | CSLED | LEDs (bits 0–11) + LCD contrast (bits 12–15) |
| 0x420000 | CSG1CHIP | G-chip 1 — sound engine (polyphony board) |
| 0x440000 | CSG2CHIP | G-chip 2 — sound engine (polyphony board) |
| 0x460000 | CSHCHIP | H-chip — digital filter IC413 |
| 0x480000 | CSHDC | SCSI (NCR5380-compatible) |
| 0x4C0000 | CSSCC | SCC (Z85C30 DUART) |
| 0x4E0000 | CSRFIFO | IDT7202 sample FIFO |
| 0x500000 | CSKCHIP | IT433 K-chip key scanner (emulated) |
| 0x560000 | CSFDC | N82077AA FDC (emulated) |
| 0x580000 | CSLCD | LM24014H LCD / T6963C (emulated) |
| 0x5A0000 | CSMFP | MC68901 MFP (emulated) — D[15:8], word-spaced |
| 0x5C0000 | CSWGAIN | Sample gain latch |
| 0x5E0000 | CSRJACK | Jack detection latch |
| 0xF00000 | — | CPU DRAM (1 MB) |

All byte-wide peripherals use **D[15:8]** (upper byte of 16-bit bus) with `.umask16(0xff00)`.

## MFP Interrupt System

MFP VR=0x48, vectors at CPU 0x0120–0x015F. MFP IRQ output → CPU IPL6 (IACK at 0xfffffffd).

**GPIO pin assignments (active-edge register AER=0x0B):**

| Pin | Signal | Edge | Description |
|---|---|---|---|
| GP0 | FDCINT | Rising | FDC interrupt → wired to `m_mfp->i0_w()` |
| GP1 | ROMWRINT | Rising | Flash ROM write (PAL IP822) |
| GP2 | FIFOHF | Falling | Sample FIFO half-full |
| GP3 | HDCINT | Rising | SCSI interrupt |
| GP4 | KCHPINT | Falling | K-chip key scanner → wired to `m_mfp->i4_w()` |
| GP5 | EXPINT2 | Falling | Expansion interrupt #2 |
| GP6 | EXPINT1 | Falling | Expansion interrupt #1 |
| GP7 | SCCINT | Falling | SCC Z85C30 interrupt |

## IT433 K-Chip (Key Scanner) — Emulated

Custom keyscanner at CSKCHIP (0x500000–0x50001F). 4 address pins A1–A4, 8-bit data on D8–D15. Scans 6×8 button matrix, rotary encoder, and volume pot ADC.

**Register map (confirmed from firmware disassembly):**

| Reg | Addr | R/W | Function |
|---|---|---|---|
| 0 | 0x500000 | R | Key code — bits[6:0]=key number, bit 7=release. Reading pops FIFO. |
| 1 | 0x500002 | R | Key velocity — raw 8-bit. Firmware: `(vel − 0x68)`, clamp 1–127. |
| 2 | 0x500004 | R | Status — bit 7=key ready, bit 6=pot ready, bit 5=encoder ready |
| 3 | 0x500006 | R | Pot MSB — upper 8 bits of 11-bit ADC. Reading clears pot-ready. |
| 4 | 0x500008 | R | Pot LSB — bits[2:0] of 11-bit ADC value |
| 5 | 0x50000A | R | Encoder delta — signed byte. Reading clears encoder-ready. |
| 7 | 0x50000E | W | Control — bit 7=enable, bits[3:2]=MIDI LEDs, bit 0=scan config |

**Interrupt flow:** KCHPINT asserted (low) when key or encoder data available → MFP GP4 falling edge → IRQ6 → ISR_User7 at 0x350F0. ISR loops calling sub_21AD0 until KCHPINT deasserts.

**Emulation:** A 200 Hz `emu_timer` scans input ports, queues events into a 32-entry FIFO, and drives `m_mfp->i4_w()`.

## Sound Memory — G-Chip Architecture (Emulated)

Sound sample RAM is **not CPU-addressable**. It sits behind the G-chip on the polyphony board (AP503), accessed indirectly through G-chip memory-mapped registers.

The G-chip is emulated as `emu_gchip_device` (`device_t` + `device_memory_interface`) in `src/mame/emusys/emu_gchip.h/.cpp`. Two instances are wired into the driver: `gchip1` at CSG1CHIP (0x420000) and `gchip2` at CSG2CHIP (0x440000). Each chip has a 128 KB register window (0x10000 words) backed by a flat register file, plus a 128 MB sample memory address space (27-bit byte address, 16-bit data, big-endian).

**G-chip register interface (offsets from CSG1CHIP base 0x420000):**

| Offset | R/W | Function |
|---|---|---|
| +$1C | R | Sample data read (from address in +$30). Read twice for pipeline flush. |
| +$1E | W | Sample data write (to address in +$34) |
| +$30/+$32 | W | Sample read address high/low words (word-addressed, 32-bit via move.l). Low word write triggers pipeline prime. |
| +$34/+$36 | W | Sample write address high/low words (word-addressed, 32-bit via move.l) |
| +$43E | W | SIMM config A (timing) |
| +$83E | W | SIMM config B (bank/size; bit 8=bank select) |
| +$C3E | W | SIMM type code |

Note: The firmware uses `move.l` (32-bit) to write addresses to +$30 and +$34. On the 16-bit bus, this generates two consecutive word writes: high word to offset+0, low word to offset+2. The emulation handles +$30/+$32 and +$34/+$36 as separate high/low word registers that accumulate a 32-bit word address.

**SIMM detection** (`sub_24210`): Firmware tries 4 SIMM configs (1MB/4MB/16MB/64MB), writes test patterns `voice_N × 0x7531` to G-chip address `(N−1) × 0x80000`, reads back via +$1C. Result stored via `setRAMSize` → `$F074AE`, displayed as `"%dmb of Sound Memory Installed."`.

**Channel detection** (`sub_24066` → `sub_23FB6`): Tests for second G-chip by writing 4 test patterns (0x12345C00, 0x54321000, 0x5A5A5A, 0xA5A5A5) to high offsets within G-chip 1's register window (+$1F014, +$1F314, +$1F148, +$1F5CC) and reading them back. Success → 128 channels (0x80), failure → 64 channels (0x40). Stored at `word_F00A88`.

**Current emulation status:** Both SIMM detection and channel detection pass. Boot shows "128mb of Sound Memory Installed" and "128 Channel Card Installed". Sound generation is not yet implemented.

## ROM Layout

`eos30b.raw` = 0xEF400 bytes. First 0x400 bytes = flash header (magic 0x12345678, version string at +4, sector count at +0x1C). Mapped at CPU 0x10000. Vector table at ROM offset 0x400 is aliased to CPU 0x000000 by CS_PAL.

## Firmware Reference Points (IDA listing)

| Address | Label | Purpose |
|---|---|---|
| 0x034FD0 | reset() | Cold-boot entry; copies API table, calls bootSystem |
| 0x0425A0 | bootSystem() | Main init sequence |
| 0x0239C2 | sub_239C2 | Hardware initializer (variant detect, MFP, SCC, memory) |
| 0x024210 | sub_24210 | Sound memory (SIMM) detection via G-chip |
| 0x02503A | init_mem | CPU DRAM size detection (probes 0xC1BE3E–0xF1BE3E) |
| 0x021AD0 | sub_21AD0 | K-chip key read handler (called from ISR_User7) |
| 0x021F48 | sub_21F48 | K-chip init (writes 0x85 to control reg) |
| 0x021FEE | sub_21FEE | Volume pot read (11-bit from K-chip regs 3+4) |
| 0x0200F2 | sub_200F2 | MFP full register initialization |
| 0x0350F0 | ISR_User7 | MFP GP4 (KCHPINT) interrupt handler |
| 0x023DFC | sub_23DFC | Boot status display ("Sound Memory Installed", "Channel Card", etc.) |
| 0x023FB6 | sub_23FB6 | Dual G-chip detection (register write/readback test) |
| 0x024066 | sub_24066 | Channel count query (calls sub_23FB6, returns 64 or 128) |
| 0x0A2AD4 | sub_A2AD4 | Boot display: "Software: %s" from flash header at 0x10004 |
| 0x022EC0 | sub_22EC0 | G-chip voice oscillator hardware write (pitch/volume to per-voice regs) |
| 0x0224E4 | sub_224E4 | G-chip full init (SIMM config + clear all 64 voice registers) |
| 0x022EA0 | sub_22EA0 | Configure sample addressing parameters (dword_F00A58/5C) |
| 0x07E4FE | sub_7E4FE | MIDI event queue: note-on enqueue (→ circular buffer at $F3FE70) |
| 0x07E446 | sub_7E446 | MIDI event queue: note-off enqueue |
| 0x07E5CC | sub_7E5CC | MIDI event queue: timer-driven dequeue + dispatch |
| 0x07E68C | sub_7E68C | Note-on handler (velocity curve → voice allocation) |
| 0x0828CA | sub_828CA | Master voice init (descriptor setup, state machine install, start playback) |
| 0x08434A | sub_8434A | Voice state machine tick (modulation, envelopes, → hardware writes) |
| 0x086C60 | sub_86C60 | Voice playback start (adds voice to timer-driven execution list) |
| 0x08318E | sub_8318E | Per-tick pitch/pan/volume update (calls sub_22EC0 for hardware write) |
| 0x02BB98 | sub_2BB98 | Audition key note-on (reads _auditionkey, queues MIDI event) |
| 0x02BBB8 | sub_2BBB8 | Audition key handler (note-on + note-off, via event 0x16) |

## Conventions

- Schematics source: E-MU PN FI11039 Rev A (EOS Technical Documents), primarily SK524 (main board) and SK503 (polyphony board).
- All addresses in firmware analysis are CPU addresses unless noted as "flash offset" or "ROM offset".
- Byte devices on the peripheral bus always use D[15:8] (upper byte). The `.umask16(0xff00)` pattern is used throughout the memory map.
- The IDA listing uses segment `eOS_ROM` for firmware code (base CPU 0x010800) and `CfgRam` for scratch RAM at 0x000200.
