# E-mu e6400 Ultra Sampler — Hardware Reference for MAME Emulation

Source: *EOS Technical Documents* (E-MU PN FI11039 Rev. A, © 1999 E-MU/ENSONIQ).  
All information extracted from the OCR text layer of that document.

---

## Schematics

The e6400 shares the **E4XT / E-Synth Rack** platform (main board AP524).

| Schematic ID | Description |
|---|---|
| **SK524** | Main PCB (primary reference for e6400) |
| **SK507** | G-0.5 Effects PCB |
| **SK535** | 16 MB Sound ROM SIMM |

The EIV schematics (SK500, SK414 etc.) cover an older platform but the same CPU family and are useful for cross-reference of the boot/diagnostic architecture.

The technical documents contain these schematics:


| Schematic ID | Revision | Description |
| -- | -- | -- |
| **SK411** | rev 2 | E-IV analog board |
| **SK414** | rev A | EIV front panel |
| **SK415** | rev C | E-IV processor ROM SIMM card |
| **SK417** | rev A | E4/E64 16MB sound flash SIMM |
| **SK500** | rev A | E-IV, e-64 digital board |
| **SK501** | rev C | E4 keyboard analog board |
| **SK502** | rev A | E4 keyboard digital board |
| **SK503** | rev C | E4K / E4X / E6400 polyphony board |
| **SK504** | rev A | E4 keyboard left panel |
| **SK505** | rev A | E4 keyboard right panel |
| **SK507** | rev A | G0.5 effects card |
| **SK510** | rev A | lower board |
| **SK511** | rev A | Expansion card interface |
| **SK518** | rev A | E4/E4K compatible G0.5 effects card |
| **SK524** | rev C | E4X/E6400 main board |
| **SK528** | rev A | E4K/EKX 16MB sound flash SIMM |
| **SK535** | rev A | E4/e64 16M sound ROM SIMM |
| **SK639** | rev B | E4 ADAT |
| **SK10255** | rev A.1 | E4 ADAT redesign |
| **SK10485** | rev A.1 | E4 Ultra |

## Nomenclature

### EOS — The Operating System

EOS (E-mu Operating System) is the software/firmware, not a hardware model. It's what all the samplers in this generation run. When people say "EOS sampler" they mean any hardware unit in this family. EOS went through multiple versions (1.x through 4.x), and importantly, most hardware units in the family could be updated — so the same box could run EOS 2.x or 3.x etc.

### Models

| Model name | Description |
| -- | -- |
| E-IV / E4 | The original flagship rack-mount Emulator IV (1994). The baseline of the whole family. |
| E4K | The keyboard version of the E4. The "K" = keys. Same engine, built-in keyboard added. Aimed at live performance / studio use without a separate controller. |
| E4X | The eXpanded rack version. More voices, more RAM capacity, enhanced I/O compared to the original E4. Still rack-mount. |
| EKX | The keyboard + eXpanded combination. Essentially the E4X engine in a keyboard chassis. K + X = EKX. |
| E64 | The E64 was a more stripped-down, lower-cost entry point into the EOS family — fewer voices, less expandability, fewer I/O options. The E6400 sat above it with more polyphony and expansion capability. They were distinct products, not just different configurations of the same box. |
| E6400 | A somewhat different beast. A more streamlined/accessible rack unit, positioned below the E4X in cost. The "64" refers to 64-voice polyphony. It lacked some of the high-end I/O and options of the E4X but ran the same EOS and sounded the same. |
| E6400 Ultra | A later revised version of the E6400 with significant upgrades — faster processor, more RAM capacity, improved EOS version. |
| E4 Ultra | The top-tier late-generation revision of the rack flagship. Basically the ultimate expression of the platform — faster, more voices, more RAM, updated EOS. The "Ultra" suffix marks the later revised hardware generation across the line. |

---

## CPU

| Item | Detail |
|---|---|
| CPU | **MC68C020 @ 22.5792 MHz** (IM405, U78) — 45.1584 MHz audio XTAL ÷2 via D-latch toggle |
| Bus width | 16-bit data bus (BD[15:0]) — it is the 68EC020 variant with no full 32-bit external bus |
| Address space | 24-bit (A[23:0]) |

> **Note:** The parts list says "68C020 25 MHz" but this is the chip speed rating, not the actual clock. The CPU clock is derived from the 45.1584 MHz audio crystal (ZX316/U37) divided by 2 via a D-latch toggle flip-flop (/Q→D), giving **22.5792 MHz** (= 44100 × 512). The 24 MHz crystal (ZX315/V1) serves the FDC, not the CPU.

---

## Crystal Oscillators

| Ref | Part | Frequency | Usage |
|---|---|---|---|
| V1 | ZX315 | **24 MHz** | FDC clock source |
| U56 | ZX314 | **16 MHz** | MC68901 MFP timer clock source (÷4 via HC393 Q1 = 4 MHz) |
| U37 | ZX316 | **45.1584 MHz** | Audio sample clock (= 44100 × 1024); also CPU clock source (÷2 via D-latch = 22.5792 MHz) |
| U48 | ZX317 | **49.152 MHz** | Audio/DAC clock (= 48000 × 1024) |

---

## Main Board ICs (AP524)

### Processors and Custom Silicon

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U78 | MC68C020 25 MHz | IM405 | Main CPU |
| U76 | K-Chip PLCC-68 | IT433 | E-mu proprietary keyboard scanning / MIDI engine |
| U40 | EMU8000 | IC405 | E-mu proprietary sound synthesis chip |
| U28, U29 | H1.5-chip Digital Filter | IC413 | E-mu proprietary digital filter (2 units) |
| U2 (polyphony board) | G2.0-chip Sound Engine | IC402 | E-mu proprietary sound engine (1 or 2 units on polyphony board) |

### Memory

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U58, U59 | 1M × 16 DRAM ≤70 ns | IM432 | CPU DRAM (2 × 16-bit = 32-bit total, 4 MB) |
| U41 | 256K × 4 80 ns DRAM | IM418 | Small on-board scratch DRAM |
| CN14 | 72-pin SIMM socket | JC361 | Sound/sample RAM expansion (1 socket) |
| U53 | 93C46 64×16 EEPROM | IM400 | Non-volatile settings storage |
| — | 72-pin SIMM sockets ×4 | JC348/JC361 | Sound RAM expansion (on polyphony board) |

**CPU RAM range:** 0xF00000–0xFFFFFF (2 × 512K×16 = 1M×32, mapped as two banks)  
- Low bank:  0xF00000–0xF7FFFF  
- High bank: 0xF80000–0xFFFFFF  

**Sound RAM:** Separate address space, accessed via G-chip. Up to 128 MB via SIMM combinations (see memory charts).

Allowed sound RAM SIMM sizes: **4 MB, 16 MB, 64 MB** (72-pin, 8 or 9 bit, ≤70 ns). No 8 MB or 32 MB permitted.

### Programmable Logic (PLDs / PALs)

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U80 | MEM PAL | IP822 (rev d required) | Memory address decoding (LMEM / MEM_PAL) |
| U45 | ChipSel PAL | IP872 (rev a required) | Chip select generation (CS_PAL) |v
| U33 | Sample PAL | IP751 | Sample address control |
| U73 | G0.5 FX PAL | IP826 | Effects sub-board PAL |

> From the update checklist: U80 **must** have IP822**d** installed; U45 must have IP872**a**.

### Chip Select Decode (CS_PAL IP872 + 3× 74ACT138)

The chip select PAL (U45, IP872) takes address lines A15–A23 as inputs. One of its outputs, **ENBIO1**, enables three 74ACT138 1-of-8 decoders that generate all peripheral chip selects in the 0x400000–0x5FFFFF range.

**ENBIO1 active when:** A23=0, A22=1, A21=0 → address range **0x400000–0x5FFFFF**.

#### Decoder #1 — selected by A20=1 (0x500000–0x5FFFFF)

Address inputs: A17–A19. Each output covers a 128 KB window.

| A19:A17 | Signal | Address Range | Device |
|---|---|---|---|
| 000 | CSKCHIP | 0x500000–0x51FFFF | K-Chip key scanner (IT433). A0–A3 address bus, 8-bit data on D[15:8] |
| 001 | CSDSP | 0x520000–0x53FFFF | DSP daughter card (effects processor) |
| 010 | CSEXP | 0x540000–0x55FFFF | Expansion daughter card |
| 011 | CSFDC | 0x560000–0x57FFFF | Intel 82078 floppy disk controller |
| 100 | CSLCD | 0x580000–0x59FFFF | Sharp LM24014H LCD (T6963C) |
| 101 | CSMFP | 0x5A0000–0x5BFFFF | MC68901 MFP (timers, GPIO) |
| 110 | CSWGAIN | 0x5C0000–0x5DFFFF | Sample gain latch (write). 8 bits on D[15:8]: bits 0–5 = gain, bit 7 = BIGEECS pad (not populated on E6400; designed for optional 4Mbit×1 parallel flash EEPROM, see note below) |
| 111 | CSRJACK | 0x5E0000–0x5FFFFF | Jack detection latch (read). 8 bits on D[15:8]: bits 2–7 = jack insertion status |

#### Decoder #2 — selected by /A20 (0x400000–0x4FFFFF)

Address inputs: A17–A19. Each output covers a 128 KB window.

| A19:A17 | Signal | Address Range | Device |
|---|---|---|---|
| 000 | CNTRLSEL | 0x400000–0x41FFFF | Partial selector → feeds Decoder #3 |
| 001 | CSG1CHIP | 0x420000–0x43FFFF | G-chip 1 — sound engine (polyphony board) |
| 010 | CSG2CHIP | 0x440000–0x45FFFF | G-chip 2 — sound engine (polyphony board) |
| 011 | CSHCHIP | 0x460000–0x47FFFF | H-chip — digital filter IC413 (+ polyphony connector) |
| 100 | CSHDC | 0x480000–0x49FFFF | AM85C80 SCSI controller (NCR5380-compatible portion) |
| 101 | CSHDD | 0x4A0000–0x4BFFFF | SCSI DMA data port (via memory PAL IP822) |
| 110 | CSSCC | 0x4C0000–0x4DFFFF | AM85C80 SCC (Z85C30-compatible DUART portion) |
| 111 | CSRFIFO | 0x4E0000–0x4FFFFF | IDT7202 1K×9 sampling FIFO buffer (read) |

#### Decoder #3 — selected by CNTRLSEL (0x400000–0x41FFFF)

Address inputs: A14–A16. Each output covers a 16 KB window.

| A16:A14 | Signal | Address Range | Device |
|---|---|---|---|
| 000 | CSWCR1 | 0x400000–0x403FFF | Control register 1 — 16-bit write latch (see bit definitions below) |
| 001 | CSWCR2 | 0x404000–0x407FFF | Control register 2 — 16-bit write latch (see bit definitions below) |
| 010 | CSMSR | 0x408000–0x40BFFF | Misc status register — 16-bit read (HW variant, FIFO status, EEPROM data) |
| 011 | CSLED | 0x40C000–0x40FFFF | LED latch — 16-bit write. Bits 0–11 = front panel LEDs, bits 12–15 → DAC for LCD contrast |
| 100 | — | 0x410000–0x413FFF | NC |
| 101 | CSAESRX | 0x414000–0x417FFF | CS8411 AES/EBU digital audio receiver |
| 110 | — | 0x418000–0x41BFFF | NC |
| 111 | — | 0x41C000–0x41FFFF | NC |

#### Control Register 1 — CSWCR1 (0x400000, 16-bit write)

| Bit | Signal | Description |
|---|---|---|
| 0 | AESCTL0 | AES/EBU control bit 0 |
| 1 | AESCTL1 | AES/EBU control bit 1 |
| 2 | AESCTL2 | AES/EBU control bit 2 |
| 3 | HCHIP18 | H-chip control signal |
| 4 | EEPROMCK | EEPROM clock (shared line; active for small EEPROM only on E6400 since big EEPROM is not populated) |
| 5 | EEPROMDI | EEPROM data in (shared line; active for small EEPROM only on E6400 since big EEPROM is not populated) |
| 6 | EEPROMCS | Small EEPROM (93C46) chip select |
| 7 | DSPBOOT | DSP boot signal |
| 8 | LCDA0 | LCD register/data select (directly drives T6963C RS pin) |
| 9 | SCSITPN | SCSI terminator power enable (active low) |
| 10 | DSPRST | DSP reset |
| 11 | HDCRST | SCSI host adapter reset |
| 12 | EXPRST | Expansion board reset |
| 13 | FDCRST | FDC reset |
| 14 | LCDRST | LCD reset |
| 15 | UPROMPGM | Flash ROM program enable |

#### Control Register 2 — CSWCR2 (0x404000, 16-bit write)

| Bit | Signal | Description |
|---|---|---|
| 0 | LITEOFF | LCD backlight off |
| 1 | VDIMMER0 | LCD backlight dimmer bit 0 (resistor-ladder DAC with LITEOFF) |
| 2 | VDIMMER1 | LCD backlight dimmer bit 1 |
| 3 | VDIMMER2 | LCD backlight dimmer bit 2 |
| 4 | SFLASHPGM | Sample flash program enable |
| 5 | AESBOOST | AES/EBU boost |
| 6 | AESPRO | AES/EBU professional mode |
| 7 | SAMPARM | Sample ARM signal |
| 8 | SAMPSEL0 | Sample source select bit 0 |
| 9 | SAMPSEL1 | Sample source select bit 1 |
| 10 | SCLKSEL0 | Sample clock select bit 0 |
| 11 | SCLKSEL1 | Sample clock select bit 1 |
| 12 | SAMPRATE | Sample rate select |
| 13 | AESRST | AES/EBU receiver reset |
| 14 | SFLASHWP | Sample flash write protect |
| 15 | GHRST | G-chip / H-chip reset |

#### Misc Status Register — CSMSR (0x408000, 16-bit read)

| Bit | Signal | Description |
|---|---|---|
| 15 | EEPROMD | EEPROM serial data out |
| 14 | FIFOF | Sampling FIFO full |
| 13 | FIFOHF | Sampling FIFO half-full |
| 12 | SROMBSY | Sample ROM busy |
| 11:8 | — | Hardware variant code (4 bits from schematic; firmware reads [11:9] via `bfextu d0{20:3}`) |
| 7:0 | — | Reserved / unused |

**Hardware variant decode** (bits[11:8] from schematic; firmware `bfextu{20:3}` reads bits[11:9] → switch → `byte_F00A84`):

| bits[11:8] | bits[11:9] | Variant ID | Model |
|---|---|---|---|
| 0101 | 2 | 4 | **E-mu E6400** (confirmed from schematic) |
| — | 0 | 2 | (unknown) |
| — | 1 | 0 | (unknown) |
| — | 3 | 1 | (unknown) |
| — | 4 | — | (error / invalid) |
| — | 5 | 3 | (unknown) |

### Serial / MIDI / System Controller

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U44 | MC68901 MFP PLCC-52 | IM408 | Multi-function peripheral: timers, GPIO, interrupts |

> **Important:** MC68901 is the **primary timer and interrupt controller**. The boot diagnostic tests write/read this device as the first peripheral health check. MIDI is handled by the SCC (Z85C30 portion of AM85C80 at CSSCC), not by the MFP.  
> Update checklist warns: **replace all ST SGS-Thomson 96 date-code MC68901 parts** — they are known-bad.

### Storage / FDC

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U81 | 82078 Floppy Disk Controller | IT382 (II382) | FDC for 3.5" HD drive |
| U36 | IDT7202SO 1K×9 FIFO ≥40 ns | IM413 | Data FIFO buffer |
| U25 | AM85C80-16JC SCSI/SCC | II383 | SCSI + serial comms controller |
| U2, U26 | 9× Active SCSI Terminator | IL357 | SCSI bus termination (2 units) |

### Digital Audio I/O

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U42 | CS8411-CP Digital Audio Receiver | II394 | S/PDIF / AES receiver |
| U43 | CS8402A-CP Digital Audio Transmitter | II393 | S/PDIF / AES transmitter |
| U39 | AV9170 PLL/Clock Sync | II401 | Clock synchronisation PLL |
| U3,5,7,9,11,13,15,17 | AD1860 18-bit Serial DAC | II379 | 8 DACs × stereo = 8 output pairs |
| U23 | 16-bit ΣΔ ADC | II381 | Analogue input (volume pot / ADC) |

### AES/ASCII Option Board (optional, CN via 14-pin header J11)

Contains:
- AES digital audio transformer (ZT320, 1:1) × 2 — XLR balanced AES/EBU I/O
- DIN-5 MIDI connector
- 74ACT125 quad buffer

---

## Front Panel

| Item | Detail |
|---|---|
| LCD | Sharp **LM24014H** (240×64 dot matrix, T6963C controller inside) |
| LCD assembly | AP441 (240×64 LCD assembly with header) |
| Encoder | Rotary encoder with ball bearing (HG001) |
| Buttons | 20× ellipse buttons (0–9, INC, DEC, +/-, .) + 4× arrow buttons |
| Headphone | 1/4" stereo right-angle jack |

The LCD is driven via the T6963C interface. `CSLCDN` is the active-low chip select. `LCDA0` distinguishes data/command. `LCDRST`/`LCDRSTN` is the reset signal.

### LED assignments (CSLED register 0x40C000, bits 0–11)

| Bit | LED |
|---|---|
| 0 | Preset Manage |
| 1 | Sample Manage |
| 2 | Preset Edit |
| 3 | Sample Edit |
| 4 | Master |
| 5 | Disk |
| 6 | Page Previous |
| 7 | Page Next |
| 8 | MIDI |
| 9 | SCSI |
| 10 | Play |
| 11 | Record |

Bits 12–15 drive a DAC for LCD contrast.

### Key matrix (K-chip IT433 scan matrix, 6 scan columns × 8 scan rows)

| Bit | SC0 | SC1 | SC2 | SC3 | SC4 | SC5 |
|---|---|---|---|---|---|---|
| SI0 | Return to zero | Sample Manage | | Page Prev | Down | 5 |
| SI1 | Rewind | Preset Edit | F1 | F5 | Dec | 6 |
| SI2 | Fast forward | Sample Edit | Assignable #3 | Page Next | Inc | 7 |
| SI3 | Stop | Master | F2 | F6 | 0 | 8 |
| SI4 | Play | Disk | | Enter | 1 | Lock |
| SI5 | Record | Exit | F3 | Up | 2 | 9 |
| SI6 | Sequencer | Assignable #1 | Controls/FX | Left | 3 | Set |
| SI7 | Preset Manage | Assignable #2 | F4 | Right | 4 | |

### IT433 K-chip Register Map (CSKCHIP 0x500000–0x50001F)

The IT433 is a custom E-mu key scanner/encoder IC. It has 4 address pins (A1–A4) giving 16 word-aligned registers, and an 8-bit data bus on D8–D15. It scans the 6×8 button matrix, a rotary encoder, and a volume potentiometer ADC. When data is available, it asserts KCHPINT (active low → MFP GP4), triggering ISR_User7.

Registers (deduced from firmware disassembly):

| Reg | CPU Address | R/W | Function |
|---|---|---|---|
| 0 | 0x500000 | R | **Key code** — bits[6:0] = key number; bit 7 = release flag (1=release, 0=press). Key numbers ≤0x50 = keyboard notes; >0x50 = panel buttons. Reading pops one event from the internal FIFO. |
| 1 | 0x500002 | R | **Key velocity** — raw 8-bit value latched with key code. Firmware maps: `(vel − 0x68)`, clamped to 1–127. |
| 2 | 0x500004 | R | **Status** — bit 7: key data ready; bit 6: pot data ready; bit 5: encoder data ready. |
| 3 | 0x500006 | R | **Pot MSB** — upper 8 bits of 11-bit pot ADC value. Reading clears pot-ready flag (bit 6). |
| 4 | 0x500008 | R | **Pot LSB** — lower 3 bits (bits[2:0]) of 11-bit pot value. Read first, before reg 3. |
| 5 | 0x50000A | R | **Encoder delta** — signed 8-bit relative encoder movement. Reading clears encoder-ready flag (bit 5). |
| 6 | 0x50000C | — | Not referenced by firmware. |
| 7 | 0x50000E | W | **Control** — bit 7: enable scanning; bits[3:2]: MIDI activity LEDs; bit 0: scan config. Shadow copy at `byte_F00991`. |

Data read protocol (sub_21AD0, called from ISR_User7 in a loop):
1. Read status reg 2 → if bit 7 set: read key code reg 0 (pops FIFO), read velocity reg 1
2. Read status reg 2 again → if bit 5 set: read encoder delta reg 5
3. ISR loops while KCHPINT remains asserted (GPDR bit 4 low)
4. On exit: ISR clears ISRB bit 6 (GP4 in-service)

Volume pot read (sub_21FEE, polled from main loop):
1. Read status reg 2 → if bit 6 set: read reg 4 (LSB) then reg 3 (MSB, clears flag)
2. Combine: `(reg3 << 3) | (reg4 & 7)` → 11-bit value (0–2047)

Init (sub_21F48, called from bootSystem):
- Writes 0x85 to control reg 7 (enable + scan config)
- sub_21F08 toggles bit 0 based on runtime parameter

MIDI LED control:
- sub_21D24: clears bits[3:2] → `byte_F00991 &= 0xC3` → LEDs off
- sub_21D42: sets bits[3:2] → `byte_F00991 |= 0x0C` → LEDs on

---

## Sound Memory (Sample RAM)

### Architecture

Sound memory (sample RAM) is **not** in the CPU's address space. The CPU has a 24-bit address bus (MC68EC020) and can only directly address 16MB; the maximum 128MB of sample RAM would never fit.

Instead, sample RAM sits behind the **G-chip sound engine** on the polyphony board (AP503). The G-chip has its own address bus to the SIMM slots. The CPU accesses sample memory **indirectly** through G-chip memory-mapped registers at **CSG1CHIP (0x420000)** and **CSG2CHIP (0x440000)**.

### SIMM Configuration

- 2× 72-pin SIMM slots on the polyphony board
- Maximum: 2× 64MB SIMMs = 128MB total
- Standard 72-pin fast-page DRAM SIMMs (same as vintage PCs)

### G-chip Register Interface for Sample Memory

The G-chip register file is accessed through the CSG1CHIP window at 0x420000. The firmware stores this base address at DRAM location `$F07E22` (read by `unk_F3A42C`). The window is organized as a 64-voice array with 0x40-byte stride per voice.

Key registers (offsets from G-chip base, voice 0):

| Offset | R/W | Function |
|---|---|---|
| +$1C | R | **Sample data read** — reads 16-bit word from address set by +$30. Read twice to flush G-chip pipeline. |
| +$1E | W | **Sample data write** — writes 16-bit word to address set by +$34. |
| +$30 | W | **Read address** — sets G-chip internal sample memory address for read operations. |
| +$34 | W | **Write address** — sets G-chip internal sample memory address for write operations. |
| +$43E | W | **SIMM config A** — sample rate / timing configuration |
| +$83E | W | **SIMM config B** — bank select / size configuration (bit 8 = bank select, bits 6-7 = mode) |
| +$C3E | W | **SIMM type** — SIMM size/type code (from template table) |

Addresses in the G-chip space are **word-addressed** (shifted right by 1 from byte addresses): the firmware does `asr.l #1, address` before writing to registers +$30/+$34.

### Sound Memory Detection (`sub_24210`, called from `bootSystem`)

The firmware probes for installed SIMMs by trial-and-error through 4 template configurations:

| Template | Config word | Size multiplier | SIMM size |
|---|---|---|---|
| 0 | 0x27 | 1 | 1 MB |
| 1 | 0x2F | 4 | 4 MB |
| 2 | 0x37 | 0x10 | 16 MB |
| 3 | 0x3F | 0x40 | 64 MB |

For each template configuration:
1. Programs the G-chip SIMM config registers (+$C3E, +$43E, +$83E) via `sub_224E4`
2. Writes unique test patterns to voice sample addresses: voice N writes `N × 0x7531` to address `(N-1) × 0x80000`
3. Reads back each address via register +$1C and compares — first/last matching voices determine SIMM start/end addresses
4. Computes memory range: `start = (first_valid-1) << 20`, `end = last_valid << 20 - 1`

After probing all 4 configurations, picks the one with the largest valid range, finalizes the G-chip config, and stores the result via `setRAMSize` → `$F074AE`.

### Boot Display

The `sub_23DFC` boot status routine calls `getRAMSize` (returns `$F074AE`) and `getPrstMemSize`, computes available memory, shifts right by 20 to convert to MB, and displays: `"%dmb of Sound Memory Installed."`. A return value of 0 means the G-chip probe found no valid SIMMs.

### CPU DRAM vs Sound Memory

| Memory | Address Range | Size | Access |
|---|---|---|---|
| CPU DRAM | 0xF00000–0xFFBFFE | 1–4 MB (probed by `init_mem` at 0xC1BE3E–0xF1BE3E) | Direct CPU access |
| Sound RAM | Behind G-chip | 1–128 MB (probed via G-chip registers) | Indirect via G-chip register reads/writes |

The `init_mem` routine at `$2503A` probes CPU DRAM by writing test patterns (0x11111111, 0x22222222, 0x33333333, 0x44444444) to addresses 0xC1BE3E, 0xD1BE3E, 0xE1BE3E, 0xF1BE3E (1MB apart in the CPU address space) and storing the detected size in `$F1BE3A`.

---

## Power Supply

- Switching PSU, auto-selects 110/220V
- One trim pot adjusting +5V and +12V simultaneously
- Rails: **+5V, +12V, −12V, +5V (analog), −5V (analog)**

| Rail | Wire colour | Tolerance on header |
|---|---|---|
| +12V | Violet | — |
| −12V | Orange | −11.7 V to −12.3 V |
| +5V | Yellow | +4.75 V to +5.25 V |

DAC supply: ±5V via discrete regulators VR1 (7905, −5V) and VR2 (7805, +5V).

---

## Boot Sequence

### Cold-boot execution flow

`eos30b.raw` **is** the cold-boot ROM — no prior bootprom stage exists.

1. CPU fetches ISP (0xFFBFFE) and reset PC (0x034FD0) from the vector table alias at 0x000000. These bytes come from flash offset 0x400 (CPU 0x010400), aliased to 0x000000 by CS_PAL hardware.
2. **`reset()`** at 0x034FD0:
   - Sets supervisor mode, masks interrupts, enables instruction cache
   - Copies 0x6000 bytes from ROM (0x0F9400 = `api_src`) to DRAM (0xF00400) — the API jump table
   - Calls `bootSystem(0)`
3. **`bootSystem()`** sets `_RTCMode=1` (pre-interrupt, use spin-loop timing), then:
   - `sub_24646`  — stub (empty)
   - `resetRtc`   — clears `timer_value` software counter
   - `sub_51262`  — initializes linked-list data structures in DRAM
   - `sub_24FEA`  — zeros 0x14000 bytes of heap at DRAM 0xF07E2E
   - `sub_239C2`  — **main hardware init** (see below)
   - `sub_428F2`  — probes SCSI bus; result → `byte_F0173A` (SCSI present flag)
   - … many more OS and driver init calls …
   - `_RTCMode=2`, interrupts unmasked — timer now running
   - `sub_21A5E`  — sets SCSI active flag
   - … second-phase init …
   - `_RTCMode=3` — disk subsystem ready
   - `pc_memory_init` — DOS/filesystem init; if fails → "Error initializing DOS."
   - If SCSI present: sets terminator, prints "Mounting SCSI devices…", waits up to 12 s
   - `dispdlg_MountDrv` — mounts attached drives
   - loads last preset, starts main UI loop
   - `_RTCMode=0` — init complete

### `sub_239C2` — hardware initializer

1. `loc_23948`  — reads hardware variant from CSMSR (0x408000) bits[11:8] (firmware uses bfextu{20:3} on bits[11:9]); stores result in `byte_F00A84`
2. Programs control register 1 (CSWCR1, 0x400000) and control register 2 (CSWCR2, 0x404000) with initial chip-select/reset masks
3. Passes 0x4C0000 (CSSCC) to `sub_20814` — SCC channel A reset
4. Passes 0x54FF00 (CSEXP) to `sub_21976` — probes for expansion SCC (WR13 readback test)
5. If expansion SCC probe fails → error: passes 1 and 0x54FF00 to `sub_3DE5E` + `sub_21608`
6. `sub_200F2` — **MC68901 MFP full initialization** (see register table above)
7. `sub_3FA16(0x21)` — stores 0x21 in `byte_F01450`
8. `sub_23B2C`  — reads hardware variant again from CSMSR; selects SCSI terminator scheme
9. `sub_872DC`  — reads CSRJACK (0x5E0000) bits[1] and [0]; stores board config flags
10. `init_mem`  — memory size detection (probes sound RAM at 0xC1BE3E–0xF1BE3E)
11. `sub_502AC` — effects DSP RAM probe: write/read test at 0x540400–0x540407 (CSEXP window)
12. `loc_2535C/25302` — sets up memory zone headers

### Boot diagnostics (from EOS Technical Documents — E4 platform)

| LED | Test | Description |
|---|---|---|
| Master | CPU | CPU is running |
| Preset Manage | MC68901 MFP | Tests write/read to 68901 — timer, GPIO (MIDI is via SCC) |
| Preset Edit | LCD | LCD write/read OK |
| — | Boot message | Displays boot version on LCD |
| — | Computer running | Floppy probed; Flash loaded into CPU RAM |

If the MFP test fails, LED stays on and boot continues attempting subsequent tests.

### Bus Loop (failure mode)
If certain boot errors occur, code jumps to an infinite bus loop exercising BD[31:16] and A[23:9] so they can be monitored with an oscilloscope. This is observable on U30 and U31, pins 11–18.

**Peripherals using only BD[31:24] (relevant for stuck-select debugging):**
- SCSI chip U13 (NOTE: on EIV board; cross-check U25 on e6400)
- K-chip U61 (NOTE: check IT433 K-chip location on e6400)
- FDC U58 / U81
- LCD
- FIFO U47 / U36

---

## Memory Map

### CPU RAM and ROM

| Address Range | Content |
|---|---|
| 0x000000–0x0001FF | ROM vector table (hardware mirror via CS_PAL — same bytes as flash[0x400], i.e. CPU 0x010400) |
| 0x000200–0x0003FF | Scratch RAM |
| 0x010000–0x0FEFFF | Flash ROM (eos30b.raw, full image at CPU base 0x010000) |

#### Flash Header (CPU 0x010000–0x0103FF)

The first 1024 bytes of the flash chip form a header used by the firmware update process and version display. The format (confirmed from `eos30b.img` floppy and firmware cross-reference):

| Flash Offset | CPU Address | Size | Content |
|---|---|---|---|
| 0x000 | 0x010000 | 4 | Magic: `0x12345678` |
| 0x004 | 0x010004 | 24 | Version string, space-padded: `"EOS v3.00b"` |
| 0x01C | 0x01001C | 4 | ROM sector count (excluding header): `0x00000778` (= 0xEF000 bytes) |
| 0x020 | 0x010020 | 4 | Checksum: `0x2DF72B2B` |
| 0x034 | 0x010034 | 4 | `0x000000E0` (unknown) |
| 0x038 | 0x010038 | 4 | `0x000002C9` (unknown) |
| 0x040–0x3FF | 0x010040–0x0103FF | 960 | Zero padding |

Firmware references:
- `sub_A2AD4` (boot display): `pea ($10004).l` → `sprintf("Software: %s", version_string)`
- `sub_2E1E8` ("Saving System to Floppy"): `move.l #$10000,d2` (flash base); `move.l ($1001C).l,d1` (sector count); `addq.l #2,d1` (adds 2 sectors for header)

| 0xF00000–0xF7FFFF | CPU DRAM low bank (2× HM514260 256K×16) |
| 0xF00400–0xF063FF | API jump table mirror (0x6000 bytes copied from ROM 0x0F9400 on boot) |
| 0xF80000–0xFFBFFE | CPU DRAM high bank |
| 0xFFBFFE | ISP — initial stack pointer (from vector table) |

### Peripheral Map

Decoded by CS_PAL (IP872) + three 74ACT138 decoders. All addresses word-aligned; byte devices on even addresses via D[15:8].

| Address Range | CS Signal | Device | Status | Notes |
|---|---|---|---|---|
| 0x400000–0x403FFF | CSWCR1 | Control register 1 | **Confirmed** | 16-bit write latch; see bit definitions in Chip Select section |
| 0x404000–0x407FFF | CSWCR2 | Control register 2 | **Confirmed** | 16-bit write latch; see bit definitions in Chip Select section |
| 0x408000–0x40BFFF | CSMSR | Misc status register | **Confirmed** | 16-bit read; bits[15:12]=EEPROMD/FIFOF/FIFOHF/SROMBSY, bits[11:8]=HW variant |
| 0x40C000–0x40FFFF | CSLED | LED / LCD contrast latch | **Confirmed** | 16-bit write; bits 0–11 = front panel LEDs, bits 12–15 = LCD contrast DAC |
| 0x414000–0x417FFF | CSAESRX | CS8411 AES/EBU receiver | **Confirmed** | Digital audio interface receiver chip select |
| 0x420000–0x43FFFF | CSG1CHIP | G-chip 1 (sound engine) | **Confirmed** | Polyphony board connector |
| 0x440000–0x45FFFF | CSG2CHIP | G-chip 2 (sound engine) | **Confirmed** | Polyphony board connector |
| 0x460000–0x47FFFF | CSHCHIP | H-chip (digital filter) | **Confirmed** | IC413 + polyphony board connector |
| 0x480000–0x49FFFF | CSHDC | AM85C80 SCSI (NCR5380) | **Confirmed** | Word-spaced byte regs; 0x48000A = Reg5 Bus&Status (bit6=DRQ) |
| 0x4A0000–0x4BFFFF | CSHDD | SCSI DMA data port | **Confirmed** | Via memory PAL IP822; DMA burst via `move.b` loop |
| 0x4C0000–0x4DFFFF | CSSCC | AM85C80 SCC (Z85C30) | **Confirmed** | DUART portion; both SCC channels A & B |
| 0x4E0000–0x4FFFFF | CSRFIFO | IDT7202 FIFO (read) | **Confirmed** | 1K×9 sampling FIFO buffer |
| 0x500000–0x51FFFF | CSKCHIP | K-Chip key scanner | **Confirmed** | IT433; A0–A3 address, 8-bit data on D[15:8] |
| 0x520000–0x53FFFF | CSDSP | DSP daughter card | **Confirmed** | Effects processor board |
| 0x540000–0x55FFFF | CSEXP | Expansion daughter card | **Confirmed** | Effects DSP RAM at +0x400; expansion SCC probe at +0xFF00 |
| 0x560000–0x57FFFF | CSFDC | 82078 FDC | **Confirmed** | n82077aa-compatible register map |
| 0x580000–0x59FFFF | CSLCD | LM24014H LCD (T6963C) | **Confirmed** | Data/command selected via CR1 bit 8 (LCDA0) |
| 0x5A0000–0x5BFFFF | CSMFP | MC68901 MFP | **Confirmed** | Word-spaced byte regs on D[15:8]; full init in sub_200F2 |
| 0x5C0000–0x5DFFFF | CSWGAIN | Sample gain latch | **Confirmed** | 8-bit write on D[15:8]; bits 0–5 = gain, bit 7 = BIGEECS pad (not populated on E6400) |
| 0x5E0000–0x5FFFFF | CSRJACK | Jack detection latch | **Confirmed** | 8-bit read on D[15:8]; bits 2–7 = jack status |

### MC68901 MFP Register Init (sub_200F2 in eos30b.raw)

Executed once during boot from `sub_239C2 → loc_23AF0 → sub_200F2`:

| Register | Offset | Value | Meaning |
|---|---|---|---|
| VR | 0x5A0016 | 0x48 | Vector base = 0x48; MFP interrupt vectors at CPU 0x0120–0x015F |
| DDR | 0x5A0004 | 0x00 | All GPIP pins = inputs |
| AER | 0x5A0002 | 0x0B | Active-edge: bits 0,1,3 rising; others falling |
| TACR | 0x5A0018 | 0x00 | Timer A disabled |
| TBCR | 0x5A001A | 0x00 | Timer B disabled |
| TCDCR | 0x5A001C | 0x00 | Timers C and D disabled |
| IMRA | 0x5A0012 | 0xFF | All channel-A interrupt sources unmasked |
| IMRB | 0x5A0014 | 0xFF | All channel-B interrupt sources unmasked |
| IERA | 0x5A0006 | 0xE1 | Enable: GPIP7, Timer A, Receiver-full, bit 0 source |
| IERB | 0x5A0008 | 0xFF | Enable all channel-B sources |

Timer data registers (TADR/TBDR/TCDR/TDDR at 0x5A001E–0x5A0024) are programmed later by `sub_20050` using a lookup table to produce baud rates 1000–50000 Hz. The MFP timer drives the software system-tick counter (`timer_value` in DRAM), used by `wait_msec()` and the 12-second SCSI mount timeout.

#### MFP GPIO Pin Assignments (GP0–GP7)

All pins configured as inputs (DDR=0x00). Active-edge register AER=0x0B selects rising edge for GP0, GP1, GP3; falling edge for the rest.

| Pin | Signal | Source | Description |
|---|---|---|---|
| GP0 | FDCINT | N82077AA FDC | Floppy disc controller interrupt |
| GP1 | ROMWRINT | PAL IP822 (MEM_PAL) | Flash ROM write interrupt |
| GP2 | FIFOHF | IDT7202 FIFO | Sample FIFO buffer half-full |
| GP3 | HDCINT | AM85C80 (NCR5380) | SCSI controller interrupt |
| GP4 | KCHPINT | PAL IT433 (K-chip) | Key scanner interrupt |
| GP5 | EXPINT2 | Expansion bus | Expansion interrupt #2 |
| GP6 | EXPINT1 | Expansion bus | Expansion interrupt #1 |
| GP7 | SCCINT | AM85C80 SCC (Z85C30) | Serial controller interrupt |

Interrupt vector assignments (VR=0x48, vectors at 0x120+):
- The ISR at CPU vector-table offset **0x120** (`ISR_User9_Timer`) is MFP vector 0 → system tick.
- The ISR at **0x11C** (`ISR_User8_Exp2`) handles voice card interrupts.
- **0x138** (`ISR_User15_Exp1`) handles effects board.

Interrupts in-service are cleared by writing to ISRA (0x5A000E) or ISRB (0x5A0010) with the bit to clear set to 0 (MC68901 clear-by-write semantics).

---

## On-Board Diagnostics (accessed via front panel)

Password: **1-3-5-8** (notes of a major chord)

| Test | Description |
|---|---|
| Panel Test | LCD pixels, all buttons, encoder (0–36), volume pot (0–255) |
| RAM Test | CPU RAM (cRAM) then Sound RAM (gRAM), 4-pass pattern write/read |
| Jack Detection | Submix output jack insertion detection |
| Serial Test | MIDI loopback (Out→In) via SCC channel A, writes 0xAA then 0x55 |
| AutoTest | Burn-in: continuous CPU RAM, G-chip RAM, SCSI disk, floppy |
| Hard Disk Tests | Read-only, write (destructive), media defects, checksum |
| Floppy Test | Drive alignment and format compatibility |
| Effects RAM Test | Effects processor RAM |
| Sound SIMM Utils | Dup→Flash, Compare, D10sum, D9sum |
| Init EEPROM | Resets to factory defaults |

> **Warning:** AutoTest and HD Write tests **destroy all hard disk data**.

---

## EOS Firmware / ROM Versions

| Version | Notable e6400-relevant changes |
|---|---|
| 1.4 | Bus Error crash fix |
| 2.0 | SyQuest/CD-ROM booting; SMDI |
| 2.1 | SMDI; SCSI compatibility; Akai/Emax II import |
| 2.12 | Motorola CPU chip compatibility fix |
| 3.00 | 17 filter types; G-chip/H-chip bug fixes |
| 4.01 | Word clock (Ultra only); Export WAV/AIFF; FIR filter |
| 4.10 | DVD/Orb drive support; SMDI bug fixes |

Boot ROM revision ".7" firmware or newer required for EOS 2.0–3.0.

---

## Hardware Quirks / Known Issues

- **MC68901 (U44):** All ST SGS-Thomson **96 date-code** parts are known-bad and must be replaced.
- **LCD contrast:** Several field deviations exist (DEV#3139, #3210, #3092) relating to LCD backlight voltage and contrast current — the LCD can have flickering or no display issues.
  - DEV#3139: Flickering LCD fix
  - DEV#3210: LCD requires more contrast current
  - DEV#3092: LCD backlight voltage too high (requires 2 cuts & jumps)
- **Voice board (AP503):** Locations R14–R25 must have **10Ω resistors**, not 47Ω.
- **G-chip cold solder joints:** Can cause "bad sounds" or digital garbage. Pressing on pins with a pencil eraser while playing can identify unsoldered pins.
- **Polyphony board:** 10Ω resistors R14–R25 (replace 47Ω if found).

---

## TODO / Open Questions

- [x] ~~Confirm exact CPU clock: 24 MHz (crystal V1/ZX315) vs 25 MHz (chip rating)~~ → **22.5792 MHz** (45.1584 MHz ÷2 via D-latch toggle; V1/24 MHz is FDC clock)
- [x] ~~Verify MC68901 MFP clock source and divider (currently using 24 MHz placeholder)~~ → **4 MHz** (16 MHz XTAL U56/ZX314 ÷4 via HC393 Q1)
- [ ] Verify which CPU IPL level the MC68901 IRQ output connects to (driver uses IPL6 as placeholder)
- [x] ~~Confirm MFP data bus connection~~ → D[15:8] (upper byte). All byte devices on this bus use D[15:8] (CSKCHIP, CSMFP, CSWGAIN, CSRJACK, FDC, LCD).
- [x] ~~Identify device at 0x500000–0x50000E~~ → **K-Chip key scanner (IT433)**, CSKCHIP. A0–A3 address bus, 8-bit data on D[15:8].
- [x] ~~Confirm the AM85C80 SCC address windows~~ → **CSSCC at 0x4C0000** (main SCC, both channels). 0x54FF00 is in the **CSEXP** (expansion) window — firmware probes for an optional expansion SCC there.
- [x] ~~Identify device at 0x5C0000~~ → **CSWGAIN** — sample gain latch (write). bits 0–5 = gain, bit 7 = BIGEECS pad (not populated on E6400; the E6400 stores firmware on a flash ROM SIMM module instead).
- [x] ~~Identify 0x5E0000 config register~~ → **CSRJACK** — jack detection latch (read). bits 2–7 = jack insertion status, bits 0–1 = board config.
- [x] ~~Determine 82078 FDC clock source~~ → **24 MHz** (V1/ZX315). Confirmed: the 16 MHz XTAL goes to MFP, 45.1584 MHz to CPU.
- [x] ~~Map all CS signals to exact address ranges from schematic SK524~~ → Complete decode in Chip Select section above.
- [x] ~~Obtain SHA1 for eos30b.raw ROM (currently placeholder in driver)~~
- [x] ~~Map G-chip, H-chip, K-chip addresses~~ → CSG1CHIP=0x420000, CSG2CHIP=0x440000, CSHCHIP=0x460000, CSKCHIP=0x500000.
- [x] ~~Understand effects DSP RAM layout at 0x540000+~~ → 0x540000 is **CSEXP** (expansion card window). DSP RAM probe at +0x400 and expansion SCC at +0xFF00 are within this space.
- [x] ~~Identify K-chip register interface and MIDI routing (IT433 has 4-bit address A0–A3, 8-bit data D[15:8])~~ → Complete register map in IT433 section above. 8 registers deduced from firmware: key code/vel FIFO, status, pot ADC (11-bit), encoder delta, control/LED.
- [ ] Identify EMU8000 vs G2.0-chip distinction (parts list shows both IC402 and IC405 — may be different board revisions)
- [ ] Confirm interrupt levels for all peripherals (MC68901 → CPU IPL lines)
- [x] ~~Decode CS_PAL (IP872) address ranges for each peripheral window~~ → Complete decode above.
- [x] ~~Determine sound RAM address space (G-chip side)~~ → Sound RAM is behind the G-chip, accessed indirectly via registers at CSG1CHIP (0x420000). CPU cannot directly address it. See Sound Memory section above.
- [x] ~~Obtain SK524 schematic page images for full address decode~~ → Full CS decode obtained from schematic analysis.
