# E-mu E6400 Sampler — MAME Driver Development

This branch adds a MAME driver for the E-mu E6400 sampler (EOS generation). All work is concentrated in the `emusys/` machine driver directory and the `eosdocs/` reference directory.

## Key Files

- `src/mame/emusys/e6400.cpp` — Main MAME machine driver
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

## Sound Memory — G-Chip Architecture

Sound sample RAM is **not CPU-addressable**. It sits behind the G-chip on the polyphony board (AP503), accessed indirectly through G-chip memory-mapped registers.

**G-chip register interface (offsets from CSG1CHIP base 0x420000):**

| Offset | R/W | Function |
|---|---|---|
| +$1C | R | Sample data read (from address in +$30). Read twice for pipeline flush. |
| +$1E | W | Sample data write (to address in +$34) |
| +$30 | W | Sample read address (word-addressed: byte_addr >> 1) |
| +$34 | W | Sample write address (word-addressed) |
| +$43E | W | SIMM config A (timing) |
| +$83E | W | SIMM config B (bank/size; bit 8=bank select) |
| +$C3E | W | SIMM type code |

**SIMM detection** (`sub_24210`): Firmware tries 4 SIMM configs (1MB/4MB/16MB/64MB), writes test patterns `voice_N × 0x7531` to G-chip address `(N−1) × 0x80000`, reads back via +$1C. Result stored via `setRAMSize` → `$F074AE`, displayed as `"%dmb of Sound Memory Installed."`.

**Max config:** 2× 64MB 72-pin SIMMs = 128MB. The boot currently shows "0mb" because the G-chip is not emulated.

**Recommended implementation:** Separate MAME device class (`device_t` + `device_sound_interface` + `device_memory_interface`), following the ES5506 pattern in `src/devices/sound/es5506.h`. Two address spaces for two SIMM banks.

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
| 0x023DFC | sub_23DFC | Boot status display ("Sound Memory Installed" etc.) |
| 0x0A2AD4 | sub_A2AD4 | Boot display: "Software: %s" from flash header at 0x10004 |

## Conventions

- Schematics source: E-MU PN FI11039 Rev A (EOS Technical Documents), primarily SK524 (main board) and SK503 (polyphony board).
- All addresses in firmware analysis are CPU addresses unless noted as "flash offset" or "ROM offset".
- Byte devices on the peripheral bus always use D[15:8] (upper byte). The `.umask16(0xff00)` pattern is used throughout the memory map.
- The IDA listing uses segment `eOS_ROM` for firmware code (base CPU 0x010800) and `CfgRam` for scratch RAM at 0x000200.
