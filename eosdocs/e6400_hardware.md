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

| Bit | LED | Notes |
|---|---|---|
| 0 | Preset Manage | |
| 1 | Sample Manage | |
| 2 | Preset Edit | |
| 3 | Sample Edit | |
| 4 | Master | |
| 5 | Disk | |
| 6 | Page Previous | |
| 7 | Page Next | |
| 8 | MIDI | |
| 9 | SCSI | |
| 10 | Play | E4K only |
| 11 | Record | E4K only |

Bits 12–15 drive a DAC for LCD contrast.

### Key matrix (K-chip IT433 scan matrix, 6 scan columns × 8 scan rows)

#### Front panel key matrix for E4K (Schematic SK504+SK505)

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

#### Front panel key matrix for E-IV (Schematic SK414)

| Bit | SC0 | SC1 | SC2 | SC3 | SC4 | SC5 |
|---|---|---|---|---|---|---|
| SI0 | | Sample Manage | | Page Prev | Down | 6 |
| SI1 | | Preset Edit | F1 | F5 | Dec | 7 |
| SI2 | | Sample Edit | Assignable #3 | Page Next | Inc | 8 |
| SI3 | | Master | F2 | F6 | 1 | 9 |
| SI4 | | Disk | Audition | Enter | 2 | +/- |
| SI5 | | Exit | F3 | Up | 3 | 0 |
| SI6 | | Assignable #1 | | Left | 4 | Dot |
| SI7 | Preset Manage | Assignable #2 | F4 | Right | 5 | |


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

The G-chip register file is accessed through the CSG1CHIP window at 0x420000. The firmware stores this base address at DRAM location `gchip_reg_base` ($F3A42C), G-chip 2 at `pG2CHIP_BUS` ($F07E22). The window is organized as a 64-voice array with 0x40-byte stride per voice.

Key registers (offsets from G-chip base):

| Offset | R/W | Function |
|---|---|---|
| +$1C | R | **Sample data read** through a one-word pipeline: each read returns the latch, then loads the word at the read address and increments the address. The first read after setting the address is stale, so EOS reads twice for one word (`wmem_readword`) and discards one before a block copy (0x223C2). |
| +$1E | W | **Sample data write** — writes 16-bit word to address set by +$34, which increments: EOS sets the address once and then writes a whole block (0x223C2, used by the SDS receive). |
| +$30/+$32 | W | **Read address** — 32-bit word address written via `move.l`. High word to +$30, low word to +$32 (M68K 16-bit bus splits 32-bit writes). Bit 29 is set by EOS's sound-memory pointer convention (bit 30 of the byte pointer) and isn't part of the address. |
| +$34/+$36 | W | **Write address** — 32-bit word address written via `move.l`. High word to +$34, low word to +$36. |
| +$3E | R/W | **Global control / DMA mode** — write: 0x39=forward DMA (normal), 0x09=reverse DMA, 0x33/0x3C=other modes. Read: bit 11 and bit 12 are status flags. |
| +$BC | R | **DAC counter** — 32-bit timer/counter for measuring DAC output frequency. |
| +$43E | W | **SIMM config A** — sample rate / timing configuration |
| +$83E | W | **SIMM config B** — bank select / size configuration (bit 8 = bank select, bits 6-7 = mode) |
| +$C3E | W | **SIMM type** — SIMM size/type code (from template table) |

**AES/EBU receiver** registers at offset 0x14000 from G-chip base (0x434000 for G-chip 1):

| Offset | Register | R/W | Function |
|---|---|---|---|
| +$14000 | AESRCV_SR1 | R | Status register 1 |
| +$14002 | AESRCV_SR2 | R | Status register 2 |
| +$14004 | AESRCV_CR1 | W | Control register 1 |
| +$14006 | AESRCV_CR2 | W | Control register 2 |

Addresses in the G-chip space are **word-addressed** (shifted right by 1 from byte addresses): the firmware does `asr.l #1, address` before writing to registers +$30/+$34. These addresses are written as 32-bit values via `move.l`, which on the 16-bit bus produces two consecutive word writes: high 16 bits to offset+0, low 16 bits to offset+2.

### Dual G-Chip / Channel Detection (`get_channel_count` → `detect_dual_gchip`)

The firmware detects whether a second G-chip is present by writing 4 test patterns to high offsets within G-chip 1's register window and reading them back:

| Offset | Test Pattern |
|---|---|
| +$1F014 | 0x12345C00 |
| +$1F314 | 0x54321000 |
| +$1F148 | 0x005A5A5A |
| +$1F5CC | 0x00A5A5A5 |

These offsets are near the top of the 128 KB CSG1CHIP window (0x420000–0x43FFFF). Between each write and the corresponding read, a delay loop (`sub_23F9E`) executes ~500 iterations.

If all 4 patterns match on readback, the routine returns true → 128 channels (0x80). Otherwise → 64 channels (0x40). The result is stored at `channel_count` ($F00A88) and displayed as "128 Channel Card Installed" or "64 Channel Card Installed" during boot.

Before the test, the routine also programs SIMM config registers on both G-chips:
- Both chips: +$C3E = 0x0400, +$43E = 0x0000, config B = 0x012F

**What the test detects:** the four addresses are voice registers in the top 4 KB of CSG1CHIP: voice 0 +$14, voice 12 +$14, voice 5 +$08, voice 23 +$0C, with the window starting at 0x43F000. In 128-voice mode EOS runs all 128 voices as one 0x40-byte-stride array starting there: voices 0–63 on the CSG1CHIP chip and voices 64–127 on the CSG2CHIP chip at 0x440000 (`gchip_select_voice_bus` 0x240AE; config at +$C3E and +$1C3E). A **64-voice unit has its only G-chip on CSG2CHIP** and CSG1CHIP empty, so the readback fails. Then:

| | 128 voices | 64 voices |
|---|---|---|
| `gchip_reg_base` | 0x43F000 (2 chips) | 0x440000 (1 chip) |
| H-chip base (`hchip_select_bus`) | 0x460000 | 0x464000 (`HCHIP_BUS` + $2000 words) |

Voice → H-chip address is `base + WORD_ARRAY_0005082C[v]` either way. So the 64-voice unit's chip uses the H-chips at 0x464000/0x466000, the same ones the CSG2 chip's voices 64–127 use in 128-voice mode. The H-chip decode is the same in both configurations. Checked in MAME: with CSG1CHIP reading 0, EOS reports 64 channels and writes exactly these addresses.

### MAME Emulation Status

The G-chip is emulated as `emu_gchip_device` (`device_t` + `device_memory_interface`) in `src/mame/emusys/emu_gchip.h/.cpp`. Two instances (`gchip1`, `gchip2`) are mapped at CSG1CHIP and CSG2CHIP respectively. Each has:
- A flat 128 KB register file (0x10000 words) backing the full chip-select window
- A 128 MB sample memory address space (27-bit byte address, 16-bit data, big-endian)
- Side-effect handling for sample read/write registers and SIMM config registers

SIMM detection passes ("128mb of Sound Memory Installed"). The "Polyphony" configuration setting picks the voice count. The default, 64 voices, matches the reference unit: G-chip 1 at CSG1CHIP is behind a `memory_view` that stays disabled, so the socket reads as empty and boot shows "64 Channel Card Installed". "128 voices (expansion)" maps it. A test note renders identically in both modes. Before 2026-10-20 the driver always ran as 128 voices.

Voice playback is emulated from the measurements below: rate/4096 per output sample, the seven interpolation kernels, the loop convention with the double-buffered loop end, and the wrap-fetch glitch. Each voice is a separate stream output into its H-chip channel, which applies the filter and the left/right volume (0.375 dB steps, glided as measured) and mixes to the stereo output (see [H-chip](#h-chip--voice-filter-and-volume)). The filter is emulated generically from the words EOS writes (see [Word format](#h-chip--voice-filter-and-volume)), for all 21 types. Rendering the hardware ladders in MAME, the shape (gain removed) matches to:
- 0.2–0.7 dB rms for the 2-pole types, high-pass, band-pass, swept EQs, EQ morphs, Peak/Shelf, flanger and Bat-Phaser
- 0.3–1.0 dB for the 4- and 6-pole low-pass, except at cutoff 0 (2–4 dB, mostly below the noise floor)
- up to 3.7 dB for the vocal types and Phaser 2 at cutoff 208

The level matches within about 1 dB, except for these flat offsets the decoding doesn't explain yet (hardware − MAME):
- −3 dB for the swept EQs and EQ morphs
- −10 dB for the vocal types
- −7/−14 dB at Q 64/127 for the 2nd-order band-pass and both phasers (twice that for the 4th order)

Both G-chips share the sample RAM (EOS writes a sample once, through G-chip 1). The output rate follows CSWCR2 SCLKSEL (45.1584 MHz for 44.1 kHz, 49.152 MHz for 48 kHz, ÷ 1024).

Checked against the hardware recordings (2026-10-20) by loading the same test samples into MAME over MIDI (SDS dump, then remote edit: New Voice in preset 0, sample, original key 69, MASTER_OUTPUT_CLOCK = 48 kHz):
- The SDS upload lands in sample memory bit-exact, one word in, as on the hardware; the voice registers EOS writes match the hardware analysis (loop start 999, loop end 3998 relative to the sample).
- The rates match the hardware sessions (3766 at note 69, 7532 at note 81), and pass lengths equal 3000/step to 0.1 ppm.
- The seam test gives −2.000 at step 0.92 (hardware −1.98) and the binary wrap-fetch glitch at steps 1.16 and 1.30, with the same phase window as the hardware.
- A single word's area against phase (the timing impulse's spread) matches the hardware for selects 1–3: 0.9/0.8 % at step 1.30, 16.0/15.7 % at 1.84, 51/48 % at 2.19, 43/43 % at 2.60. This needed select 1 to be select 0 stretched 1.08×; the fast-impulse extraction of select 1 gave 5.3 % at step 1.30.
- Open: at steps 1.84–3.09 the seam excess against phase differs from the hardware by 0.4–0.6 rms (same range and shape class), which points at the exact glitch timing (which tap is the fetch pointer).

MIDI in needed two driver fixes: the SCC registers are B ctrl, B data, A ctrl, A data at 0x4C0000/2/4/6 (EOS writes its WRn sequences to +4), and SCCINT (/INT, open drain) must be inverted into MFP GP7 and idle high.

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

### Per-Voice G-chip Registers

Each G-chip manages 64 voices. Per-voice registers are mapped at a stride of **0x40 bytes** (32 word registers) per voice within the chip-select window. Voice N's registers start at CSG1CHIP + N × 0x40.

**Initialization** (`gchip_init_voices`): On boot, the firmware iterates over all 64 voices and clears registers +$00, +$04, +$10 (longwords) and calls `gchip_clear_voice_filter_regs` to clear +$20 through +$2E (8 words). The G-chip base is stored at `gchip_reg_base` ($F3A42C) and the current voice × 0x40 offset at `current_voice_channel` ($F3A850).

**Known per-voice register map** (byte offsets from voice base):

| Offset | Size | R/W | Init | Function |
|--------|------|-----|------|----------|
| +$00 | 32-bit | R/W | 0 | **VOICE_CTRL** — Voice control/status. Bit 12 = oscillator phase flag (determines write order). 0x1800 = enabled+running, 0x1000 = disabled. |
| +$04 | 32-bit | R/W | 0 | **VOICE_RATE** — bits 31–16 = rate (step = rate/4096), written by `gchip_compute_sample_rate`; 0 at voice start. EOS reads it back and sums it over all voices as a load estimate (0x8677C). When it frees a voice it writes bits 9–0 (0x200; meaning unknown, `FUN_000226a0`). 0x80000 = silence. |
| +$08 | 32-bit | R/W | — | **Loop/end address, buffer A**. Bits[25:0] hold a word address, written ones'-complemented. Bits[31:26] are preserved by read-modify-write; bits 26–28 are the **interpolation kernel select**, written by `gchip_compute_sample_rate` (see Voice Interpolation). |
| +$0C | 32-bit | W | — | **Loop/end address, buffer B**, ones'-complemented. At voice start it gets ~(loop end), or ~(end) for a one-shot. `gchip_write_voice_loop_regs` (0x22EC0) writes a new loop end to +$08 or +$0C depending on +$00 bit 12, so the two are double-buffered (see Voice Loop Playback). |
| +$10 | 32-bit | R/W | 0 | **VOICE_MODE** — bits 31–16 get the rate too while the voice is in use (`gchip_compute_sample_rate`); EOS clears them when it frees the voice after the release (0x000000A8, `FUN_000226a0`). Bits 15–0: mode; low byte 0x98 = one-shot (`gchip_voice_set_oneshot`). 0x80000 = silence. |
| +$14 | 32-bit | W | — | **Loop start** word address, not inverted: the first word played after a wrap (EOS writes the sample's loop start − 1 for bit-3 samples). 8 means park: a one-shot voice continues into the silent loop at words 8–0x1C. Bit 26 (0x4000000) is set at voice start and toggled together with the +$08/+$0C buffer choice, so it appears to select the active loop-end buffer. |
| +$18 | 32-bit | W | — | **Start** word address (sample start + start offset), written at voice start. |
| +$1C | 16-bit | R | — | Sample data read (chip-wide — overlaps voice 0's register space). |
| +$1E | 16-bit | W | — | Sample data write (chip-wide). |
| +$20–$2E | 16-bit ×8 | W | 0 | **VOICE_FILT0–7** — Filter/volume registers. Cleared by `gchip_clear_voice_filter_regs` during init and voice start. |
| +$30/+$32 | 32-bit | W | — | Sample read address (chip-wide). |
| +$34/+$36 | 32-bit | W | — | Sample write address (chip-wide). |

**Notes:**
- The ones'-complement pattern (`not.l` before write to +$08 and +$0C) suggests the G-chip uses inverted or down-counting logic internally.
- Registers +$1C/+$1E/+$30-$36 are chip-wide sample memory access registers that overlap voice 0's register space. Voice 0 may be reserved or these registers may be dual-function.
- For stereo playback, two consecutive voices (N and N+1) are paired. The second voice is at offset +$40 from the first: writes to +$48/+$4C/+$54 correspond to the second voice's +$08/+$0C/+$14.

### Voice Oscillator Write — `gchip_write_voice_loop_regs`

Writes the voice's loop registers (loop start +$14, and the inactive loop-end buffer +$08/+$0C) from the staging struct; formerly named `gchip_write_voice`. It is called from `sub_8318E` (pitch/volume update) during each voice tick.

**Input:** Pointer to a voice oscillator state structure (offset $32 within voice register block):
- +$00: G-chip voice A hardware pointer (address: CSG1CHIP + voice_N × 0x40)
- +$04: G-chip voice B hardware pointer (for stereo pair)
- +$10: Loop start word address (8 = park, i.e. one-shot); from `gchip_prepare_oscillator`
- +$14: Loop end word address (written complemented to +$08 or +$0C)
- +$18: Voice mode: 0=inactive, 1/2=active stereo modes, 3=disabled
- +$19: Secondary flag
- +$1A: Dirty flag (cleared after hardware write)

**Write algorithm:** Disables interrupts (`ori #$300,sr`), then:

1. **Single-voice mode** ($10 == 8): Writes `not($14)` to +$08 (masked 26-bit, preserving upper 6 bits), writes `not(gchip_sample_addr_lo)` to +$0C, writes `gchip_sample_addr_hi | bit26` to +$14.

2. **Multi-voice mode** ($10 != 8): Writes $10 to +$14 directly, writes `not($14)` to +$08 (26-bit masked).

3. **Stereo pair:** If $18 indicates dual-voice, repeats appropriate writes at voice base + $40 using parameters from the B pointer.

4. Re-enables interrupts (`andi #$F8FF,sr`).

The **write order** depends on bit 12 of the voice status register at +$00. When bit 12 is clear, the sequence is: +$08, +$0C, +$14. When bit 12 is set, the sequence reverses to: +$0C, +$08, +$14. This likely avoids glitches during the oscillator's active phase.

**Global parameters:**
- `gchip_sample_addr_hi` (at $F00A58): Initialized to 8 by `gchip_set_sample_params`. Used as the +$14 base value.
- `gchip_sample_addr_lo` (at $F00A5C): Initialized to 0x1C by `gchip_set_sample_params`. Used as the +$0C base value.
- `gchip_set_sample_params` computes: `gchip_sample_addr_hi = (offset >> 1) + 8`, `gchip_sample_addr_lo = gchip_sample_addr_hi + 0x14`.

### Voice Pitch — Measured on Hardware

Measured 2026-10-20 on a real e6400 (EOS 3.00) at both MASTER_OUTPUT_CLOCK settings, 48 kHz and 44.1 kHz. Both match the firmware's `gchip_compute_sample_rate` (0x0231A8) to within 0.16 cents on every rung.

**What the G-chip does:** a voice steps through its sample at **rate / 4096 source samples per output sample**, where `rate` is the 16-bit value EOS writes to the upper half of +$04 and +$10. At 48 kHz output, a 44.1 kHz sample needs rate 0.91875 × 4096 = 3763.2 to play at its own pitch. The largest rate, just under 0x10000, is a 16× step.

**What EOS computes** (`gchip_compute_sample_rate`):

- The pitch word has 768 units per octave (64 per semitone). `u = pitch − 0x17F` at 44.1 kHz output, or `pitch − 0x1DD` at 48 kHz. The difference, 94 units, is 1200·log2(48000/44100) = 146.7 cents.
- `rate = PITCH_RATE_LUT[u mod 768]` (0x022818, 768 words from 0x1000 to 0x1FF8, i.e. 4096·2^(i/768)). It is shifted **right** by the number of octaves `u` lies below 0, and **left** by the octaves above, **capped at 3**.
- A 44.1 kHz sample at its original key has pitch word 0x180. At 48 kHz that gives u = −93, so rate = LUT[675] >> 1 = 0x1D6C >> 1 = **3766**.
- The same function also writes `table[rate >> 10] << 13` into bits 26–31 of +$08. This is the **interpolation kernel select** (see [Voice Interpolation](#voice-interpolation--measured-on-hardware)). The table is in DRAM at $F009F8, copied at boot from ROM 0x0F99F8 with the rest of `.data` (ROM 0x0F9000 → $F00000). Its entries for `rate >> 10` = 0…4 are 0, then 0x2000 for 5–6, 0x4000 for 7–8, 0x6000 for 9–10, 0x8000 for 11–12, 0xA000 for 13–14 and 0xC000 from 15 up. That puts the values 0–6 in bits 26–28.

**Consequences, all reproduced by EOS itself.** A G-chip model only needs rate/4096; it must not correct any of these:

At **48 kHz output** (session `2026-10-20_e6400_note-2`):

| Transposition (semitones) | rate | Measured (cents vs ideal) | Model (cents) | Cause |
| --- | --- | --- | --- | --- |
| −69 | 69 | −22.9 | −23.1 | `>> 6` truncation |
| −60 | 117 | −8.7 | −8.9 | `>> 5` truncation |
| −48 | 235 | −1.4 | −1.5 | `>> 4` truncation |
| −36 | 470 | −1.5 | −1.5 | `>> 3` truncation |
| −24 | 941 | +0.4 | +0.4 | `>> 2` truncation |
| −12 … +48 | 1883 … 60256 | +1.29 | +1.29 | LUT rounding: 3766 vs 3763.2 |
| +49 | 63840 | +1.32 | +1.31 | |
| +50 and up | 33816 … | −1200 | −1200 | left-shift cap of 3: the pitch drops an octave |

At **44.1 kHz output** (session `2026-10-20_e6400_note-3`), the original key falls at u = 1, so rate = LUT[1] = 0x1003 = 4099 against the ideal 4096. 0x1003 has only its two lowest bits set, so the right shifts truncate to exact powers of two, and the low notes come out *more* accurate than at 48 kHz. 16× of a 44.1 kHz sample needs rate 65536, so the wrap moves down two semitones:

| Transposition (semitones) | rate | Measured (cents vs ideal) | Model (cents) | Cause |
| --- | --- | --- | --- | --- |
| −69 | 76 | −2.3 | −2.5 | `>> 6` truncation |
| −60 … −24 | 128 … 1024 | 0.00 … +0.12 | 0.00 | truncation lands on exact powers of two |
| −12 | 2049 | +0.85 | +0.85 | `>> 1` truncation of 4099 |
| 0 … +36 | 4099 … 32792 | +1.27 | +1.27 | LUT rounding: 4099 vs 4096 |
| +47 | 61912 | +1.52 | +1.52 | |
| +48 and up | 32792 … | −1199 | −1199 | left-shift cap of 3: the pitch drops an octave |

At both clocks the mid-range rungs agree with the model to 0.01 cents, so the e6400's audio clock and the recording interface's agree to within about 6 ppm.

**How it was measured:**

- The tool was samplebot (`sds_send`, `probe`), recording the main outputs at 48 kHz/24 bit; the data is in session `2026-10-20_e6400_note-2`.
- The test sample was a −6 dBFS 441 Hz sine (100 words per period, 4400 words, 44.1 kHz) uploaded by MIDI Sample Dump. Its forward loop ran over words 1000–3999 (30 whole periods).
- It was played from a one-voice scratch preset: original key 69, filter type 1 with cutoff 255 and Q 0, all tuning 0, velocity 127.
- Pitch was taken from zero-crossings over 2.5 s of each note.

### Voice Interpolation — Measured on Hardware

Measured 2026-10-20 at 48 kHz output (sessions `2026-10-20_e6400_note-4` and `…note-6`) and checked at 44.1 kHz (`…note-5`). The full method, validation, error sources and reproduction steps are in [e6400_interpolation_measurement.md](e6400_interpolation_measurement.md). Scripts and data are in [measurements/](measurements/).

**The interpolator is a 6-tap low-pass FIR**, not linear interpolation. It uses **one of seven kernels, selected by the step**, through bits 26–28 of +$08.

- **Shape:** each output sample is a weighted sum of the source words within ±3 words of the read position. The kernels are symmetric. The select-0 kernel is windowed-sinc-like, with zero-crossings at ±0.86, ±1.73 and ±3.05 words; higher selects widen the main lobe.
- **Not interpolating:** because the zero-crossings are not at ±1, ±2, the output does not reproduce the source words even at integer positions. With select 0 the taps there are 1.000, −0.11, +0.07, 0, which sum to 0.92.
- **Phase resolution:** a slow impulse trace (59 output samples per source word) shows no phase stair-steps. The fractional position is resolved to at least about 64 phases, or the coefficients are interpolated.
- **Output clock:** the select-0 kernel is the same at 44.1 kHz output (max difference 0.006).

**Kernel select.** EOS sets the select from `rate >> 10` (see [Voice Pitch](#voice-pitch--measured-on-hardware)), on a fixed grid of 0.5 in step. Each kernel was measured with fast-impulse recordings (method in the measurement record, section 14):

| Select | rate | Step | Width vs select 0 | First zero-crossing (words) | DC gain vs select 0 | Measured at steps |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | < 5120 | < 1.25 | 1.00 | 0.86 | 1.000 | 0.017, 0.034, 0.92 |
| 1 | 5120–7167 | 1.25–1.75 | 1.08 | 0.93 | 1.026 | 1.30, 1.55 |
| 2 | 7168–9215 | 1.75–2.25 | 1.26 | 1.20 | 0.925 | 1.84, 2.19 |
| 3 | 9216–11263 | 2.25–2.75 | 1.51 | 1.48 | 0.962 | 2.32, 2.45, 2.60 |
| 4 | 11264–13311 | 2.75–3.25 | 1.79 | 2.28 | 1.037 | 2.75, 2.92, 3.09 |
| 5 | 13312–15359 | 3.25–3.75 | 2.07 | 3.07 | 1.037 | 3.28, 3.47, 3.68 |
| 6 | ≥ 15360 | ≥ 3.75 | 2.40 | 3.55 | 1.035 | 4.13, 5.51, 7.36 |

- "Width" is the factor S for which H(ν·S) best fits the select's response, where H is the select-0 response. The fit is only a 1.5–5.5 dB-rms approximation, so use the measured kernels: `measurements/g_chip_kernel.csv` for select 0 and `measurements/g_chip_kernel_fast_sel1.csv` … `sel6.csv` for the others.
- Every kernel stays within about ±3.2 words, so all seven are 6-tap. Each is normalised to about the same DC gain.
- Within a select the kernel doesn't change with the step: notes sharing a select agree to 0.002–0.027 (selects 1–6, two or three steps each).
- Steps below 1.25 use select 0 and alias when above 1. Each higher kernel cuts off around 0.6–0.7 × the source-rate Nyquist of its step range, so pitched-up voices still alias somewhat. Select 6 is the widest kernel, so steps above about 4.9 alias more and more.

**Frequency response of select 0** (× source sample rate). The passband and transition band come from the impulse trace; the stopband comes from the images of a 1/8-rate sine played at 9 transpositions (notes 33–87), which reach a −110 dBc noise floor:

| 0.25 | 0.375 | 0.5 | 0.625 | 0.75 | 0.875 | 1.125 | 1.875 | 3.125 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| −0.7 dB | −0.1 | −2.6 | −10.8 | −27.6 | −63 … −68 | −76 … −90 | −78 … −89 | −70 … −77 |

For comparison, linear interpolation would put the first image pair at −34 / −38 dBc and no interpolation at −17 / −19 dBc.

**Select-0 kernel h(t)** (t in source words, normalised to h(0) = 1, mean of h(±t), zero beyond |t| ≈ 3.2; full resolution in `measurements/g_chip_kernel.csv`):

| t | +0 | +1/8 | +2/8 | +3/8 | +4/8 | +5/8 | +6/8 | +7/8 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | +1.0000 | +0.9630 | +0.8593 | +0.7010 | +0.5115 | +0.3140 | +0.1323 | −0.0140 |
| 1 | −0.1125 | −0.1646 | −0.1717 | −0.1449 | −0.0992 | −0.0440 | +0.0068 | +0.0458 |
| 2 | +0.0690 | +0.0766 | +0.0719 | +0.0595 | +0.0432 | +0.0279 | +0.0154 | +0.0065 |
| 3 | +0.0015 | −0.0010 | −0.0014 | −0.0011 | −0.0005 | +0.0002 | +0.0002 | +0.0002 |

**How it was measured** (in brief):

- A sample of isolated +16384 impulses, 500 words apart, was played at note 0 with original key 69 (rate 69, step 69/4096). That traces the kernel at 59.4 output samples per source word.
- Six traces were averaged. The analog output's AC-coupling offset was removed as a baseline that follows the kernel's cumulative area.
- The result was cross-checked with a second trace at note 12 and with the images of a −6 dBFS sine of exactly 8 words per period.

**Measurement pitfalls:**

- EOS pulls SDS loop points that lie within about 6 words of the sample's ends inwards (0–4399 became 6–4394). That breaks a whole-period loop: it clicks, adds loop-rate sidebands, and here added +4.3 cents. Keep loops inside the sample.
- Using the front panel changes the current preset and overrides MIDI Program Changes (the e6400 transmits `B0 00 00`, `B0 20 00`, `Cn pp` when it does). Leave the panel alone during a ladder.

### Voice Loop Playback — Measured on Hardware

Measured 2026-10-20 at 48 kHz output (session `2026-10-20_e6400_note-5`), with a test sample whose loop body is silent apart from one timing impulse, and with ±16384 markers in the six words on either side of the loop. Details are in [e6400_interpolation_measurement.md](e6400_interpolation_measurement.md), section 15.

- **The interpolator wraps cleanly.** At the loop point the six taps read across the seam as if the loop were one continuous, repeating stream of words. The words after the loop end are never read, and no tap reads raw memory before the loop start after the wrap. A deconvolution of the seam (note 12) fits a single periodic word stream to the noise level.
- **The fractional position carries over** across the wrap. Pass lengths are non-integer and average exactly loop length / step output samples (to within 1.8 ppm, the clock offset between the e6400 and the recorder). The phase isn't reset at the loop start.
- **SDS loop points play 2 words early.** A loop sent by MIDI Sample Dump as 1000–3999 (forward, length 3000) plays words 998–3997 of the data as sent. Steps of 0.034, 0.92 and 0.97 agree. The firmware accounts for both words (see below); the chip itself adds no offset.
- **Wrap-fetch glitch at steps above 1.** When an output sample's step crosses the loop end, the chip subtracts the loop length from *every* word it fetches in that step. With a 2-word advance that skips the loop-end word, that word is read from one loop length earlier (sent 997 instead of 3997). That happens in a fraction 1 − 1/step of the wraps, and only for one pass at a time; the loop length is unaffected. It was identified with two marker samples whose values differ: each glitched pass is off by exactly the difference of those two words (+1.00 and −1.24 timing-impulse units), and nothing else fits both. Above step 2, two words can be affected. Details: measurement record, section 15.
- EOS writes no guard copies of the loop start after the loop end.

**Where the 2 words come from (EOS 3.00 firmware).** A sample of N words received by SDS with loop LS–LE:

1. **`rcv_dumpheader` (0x13C64) builds the sample header** from a 0x5C-byte template at ROM 0x01EAB8 ("Untitled Sample", 44100 Hz, flag byte +0x3A = 0x0E: bit 3 and both channel bits set):
   - start (+0x14) = 0 and end (+0x1C) = 2N − 2 (byte offsets);
   - loopStart (+0x24) = 2·LS and loopEnd (+0x2C) = 2·LE;
   - loop mode in +0x3A bits 30–31 = SDS loop type + 1, channel bits 25–26 = left only;
   - +0x3A byte bit 4 set; byte bit 3 stays set from the template.
   - The rate (+0x34) is 1e9 / period rounded down: SDS's 22676 ns gives 44099 Hz, which the front panel shows.
2. **The data lands one word in.** It's written from byte +2 of the channel's memory (+0x3C), so sent word k is stored word k + 1, and stored word 0 is the zero the SDS readback shows. The loop pointers don't allow for this, so they point at **sent words LS − 1 and LE − 1**. That's one word of the shift. The last sent word falls past `end`.
3. **After the last packet,** `sds_rcv_zero_sample_ends` (0x13FD6) zeroes stored words 0–1 and the last two stored words. `sample_clamp_loop_channel` (0x1F05A, called per channel by `sample_clamp_loops`) clamps loopStart to ≥ start + 6 words (and < end − 2) and loopEnd to ≤ end − 5 words (and > start + 2), and zeroes guard words around the sample. That's the earlier "0–4399 → 6–4394".
4. **At voice start, `gchip_prepare_oscillator` (0x1E94E, called from `voice_update_pitch`) converts the pointers into register values**, and `gchip_voice_start_osc` (0x230AE) writes them (word addresses, (pointer + $F0095A + channel offset) >> 1):
   - start → +$18;
   - loopStart → +$14, minus 1 if +0x3A byte bit 3 is set (it is for SDS samples; that's the other word);
   - loopEnd − 1 → +$0C (complemented).
   - A non-looping sample gets 8 in +$14 and `end` in +$0C. 8 is the "park" sentinel: the voice then loops a silent region at sound-memory words 8–0x1C ($F00A58/$F00A5C).
5. **For the SDS loop 1000/3999** that gives +$14 = stored word 999 and loop end = stored word 3998. The measured loop is stored 999–3998.

So **the chip plays from +$14 through the loop-end register inclusive** and then wraps back to +$14: (loop end) − (+$14) + 1 words.

- **For MAME:** EOS produces both words of the shift itself. The G-chip model loops from the +$14 word through the loop-end register's word inclusive, with no offset of its own.
- **Caveat:** this assumes the chip's word address W reads the word the CPU wrote at byte 2W. A constant offset there would also move the start address, and these measurements can't see it.
- **Bit 3 (+0x3A byte, 0x08)** means "loop start register = loop start − 1". New samples get it from the template (SDS receive, `get_new_sample_header_template`), and recording (`sampling_build_sample_header` 0x32374), the importers (0x414D0, 0x6EC02, 0xF314E, 0xF5D24), the Sample Edit Loop tool (`dsp_loop_exec` 0x14B26, and its page `ui_dsp_range_page__ctor` 0xBE4B2) all set it; `read_sample_header` takes it from disk. Nothing in EOS 3.00 clears it, so only a sample loaded from disk without it loops one word later. Measured 2026-10-20 (session `2026-10-20_e6400_note-8`): running the Loop tool on an SDS sample changed nothing, because the bit was already set; moving its loop start by one word on the panel moved the played loop by exactly one word.
- **Loop tool display:** the Sample Edit Loop page shows start = stored loop start − 4 words (996 for SDS 1000) and end exclusive (start + length, 3996); executing it writes the loop pointers back from those values (`dsp_loop_exec`). Only start and length are editable; the wheel moves start in large steps, the keypad sets it exactly.

### G-chip Helper Functions (Sample Memory Access)

Functions in the 0x22xxx ROM area handle G-chip sample memory transfers:

| Function | Address | Operation |
|----------|---------|-----------|
| `wmem_readword` | 0x02211E | Set sample read address (+$30), read one sample word (+$1C) |
| `wmem_readword` | 0x022160 | Set read address + pipeline prime (reads +$1C twice) |
| `wmem_writeword` | 0x022188 | Set sample write address (+$34), write sample data (+$1E) |
| `wmem_readbyte` | 0x022396 | Read byte (reads word, extracts high/low based on addr bit 0) |
| `wmem_writebyte` | 0x022354 | Write byte (read-modify-write) |
| `wmem_copy` | 0x0222AE | Block copy within sample memory |
| `gchip_init_voices` | 0x0224E4 | Full G-chip init — SIMM config, clear all 64 voice registers |
| `gchip_clear_voice_filter_regs` | 0x022624 | Clear voice registers +$20–+$2E (8 words) |
| `gchip_silence_all_voices` | 0x0225EC | Disable all voices (+$00=0x1000, +$10/+$04=0x80000) |
| `gchip_set_forward_dma` | 0x0226EC | Silence voices + set +$3E=0x39 (forward DMA) |
| `gchip_set_reverse_dma` | 0x022704 | Silence voices + set +$3E=0x09 (reverse DMA) |
| `gchip_set_sample_params` | 0x022EA0 | Configure sample addressing parameters |
| `gchip_write_voice_loop_regs` | 0x022EC0 | Voice oscillator hardware write (main audio path) |
| `gchip_voice_start_osc` | 0x0230AE | Voice oscillator start (full register setup) |
| `gchip_compute_sample_rate` | 0x0231A8 | Pitch → rate register conversion via LUT |

All sample memory access helpers convert byte addresses to word addresses (`asr.l #1`) and write via `move.l` to +$30 or +$34 (which splits into two 16-bit writes on the M68K bus).

### H-chip — Voice Filter and Volume

Each G-chip voice goes through an H-chip channel: filter, left/right volume, and the mix to the outputs. A G-chip feeds two H-chips, one for its odd voices and one for its even ones, 32 channels each. The four H-chips share CSHCHIP at **0x460000**, 0x2000 bytes per chip (traced in MAME 2026-10-20, EOS 3.00):

| Address bits | Meaning |
|---|---|
| A14 | G-chip (0 = G-chip 1) |
| A13 | 0 = odd voices, 1 = even voices |
| A12, A10–A7 | channel: A12 = bit 1 of the voice, A10–A7 = (voice >> 2) − 1, mod 16 |
| A6–A0 | register in the channel's 0x80-byte window |

EOS keeps the base in `F3DD34` (= 0x460000) and adds `WORD_ARRAY_0005082C[voice]` (`hchip_get_channel_addr`, 0x05092C); voices 0–63 are G-chip 1's, 64–127 G-chip 2's. A G-chip 1 voice v therefore lands on chip `~v & 1`, e.g. voice 4 at 0x462000, voice 5 at 0x460000, voice 0 at 0x462780. The per-voice pointer at descriptor +$08 is this H-chip address, not the G-chip's.

**Per-channel registers** (offsets in the channel's window):

| Offset | R/W | Function |
|---|---|---|
| +$00–$24 | W | Filter program: long words at +$00/$04, +$10/$14, +$20/$24 written at note start by the filter type's setup (0xFFCFFFCF = unused; `hchip_clear_filter_regs` 0x0509C8 writes it everywhere). The high byte of +$24 is a gain, 0xF2 = unity, about 0.75 dB per step at cutoff ≈ 200 and more below (see **Filter**). +$08/$0C, +$18/$1C, +$28/$2C are never written (probably the filter state). See **Filter** below |
| +$34 / +$36 | R | Current left / right level, 8.8 fixed point in volume steps. `FUN_00050A44` returns the larger high byte; it feeds a voice level readout (`FUN_0008712A`) and an "all channels silent" check (`FUN_000871AC`) that gates a jack-sense read |
| +$34 / +$36 | W | Set the current level (EOS writes 0 at init) |
| +$3B | R | Reads back +$7B (`hchip_write_filter_coeff` merges into it) |
| +$46 | W | Latch for the next level write: 1 before volume updates, 4 before a mute (`hchip_mute_channel`), 0 before a filter cutoff write |
| +$71 / +$73 | W | Left / right volume target: 0.375 dB per step, 255 = 0 dB, 0 = off |
| +$7B | W | Filter mode byte (`BYTE_ARRAY_00050D7E[type] \| Q bits`; 0x77 for the default voice); its bit 7 goes to the previous channel's +$7B |
| +$7C | W | Filter cutoff 0–255 = the voice's FMORPH (remote-edit id 83) plus modulation, written every tick in the **previous** channel's window (−0x80, wrapping 0x000 → 0x780) after +$46 = 0 there |

Per chip, +$40/+$42 of channels 0 and 16 take output setup words (`FUN_00050010`: 0x0808/0x003B and 0x2B2B/0x0038 in normal running, 0x3B3B/0x3333 during channel init) and +$44/+$46 of channel 0 are written 4 at boot.

**Volume.** EOS computes the left/right bytes in software (`voice_update_volume_init` 0x08393C, `voice_update_volume` 0x083678) as master + pan curve (`WORD_ARRAY_00082CD2[pan] >> 4`, −256 = off, −20 at centre) and steps them for the amplitude envelope about every 10 ms. Default voice, note 69: velocity 127 → 0xE9 left / 0xE8 right, 100 → 0xDC, 64 → 0xCB, 32 → 0xBC, 16 → 0xB5, 8 → 0xB1; the release ramps 0xE9 → 0 in about 170 ms, 13 steps a tick.

Measured on the hardware (samplebot session `2026-10-20_e6400_velocity`: preset 127, sample 902 sine, note 69 at those velocities, 1 s each):
- Steady levels fall 16.12 dB over the 43 steps from velocity 100 to 8: **0.375 dB per step** (E-mu's usual 96 dB / 256). Velocity 127 sits 0.6 dB below that line, explained by the curve's top (next item).
- **The curve compresses above about 219** (session `2026-10-20_e6400_rung`, `level_curve_pan0_pan63`: looped noise, filter open, voice volume +10 … −40 dB at centre pan and +10 … −30 dB at pan +63, targets 155–255 from MAME's EOS trace, master volume at maximum). Above the knee the amplitude is proportional to (level − 196), which joins the 0.375 dB/step line with the same slope at 219.2: 0.28, 0.22 and 0.17 dB per step at 227, 235 and 247, and 255 is 5.3 dB below the straight line. MAME with this curve matches the ladder to 0.31 dB rms (0.64 max) and the Moog preset levels to 0.57 dB rms (two groups ±2 dB apart before: presets sustaining at 245 against decaying ones below 219).
- The level glides down towards a lower target at about 7/256 step per output sample (≈ 490 dB/s at 48 kHz) with the latch at 1: the release of session `2026-10-20_e6400_note-2` fits a slew of 0.028 step/sample to 0.32 dB rms, against 0.58 dB for a one-pole in dB. Rising to the note-on target takes about 1 ms.

**Mix and output.** H-chip 1.5 has cascade pins CSCDI/CSCDO and a DAC output DACD (plus STB0N–STB7N for the eight output pairs): the four chips are chained, each adding its channels to the previous chip's mix, and the last one feeds the DACs (SK524 sheet 14, PC510). Measured with one to four unison voices on a 750 Hz sine (`clip_unison_knobmax`, master volume at maximum): the voices sum coherently, and the mix **saturates symmetrically with flat tops at 3.55 × one full-scale voice at level 255** (11 dB; the clip follows the total, not each chip's share). The filter stays linear before that: a resonant 4-pole low-pass with its output 11 dB above a full-scale input showed no distortion over 45 dB of input level (`sat_q114_stair`). P13 "Mini Moog 1" reaches this ceiling on the hardware at full volume as its resonance crosses the fundamental: the overdrive heard on that preset. Split on a copy (session `2026-10-20_e6400_fsweep`, preset 101 = P13, MAME rendering the same edits):
- Its swept voice (voice 1: 4-pole, Q 114, filter-envelope cord +55) flat-tops on the hardware at −3.54 dBFS, which is MAME's clamp after the −3.6 dB rig offset (1156 against 1060 samples within 0.1 dB of the peak).
- Voice 0 alone matches MAME within ±0.6 dB.
- Voice 1, with chorus 13 or 0, swings −2.7 to +2.8 dB against MAME at the same moments in both runs and ends 1–2.7 dB low. That is the resonance crossing the harmonics, and it amplifies the per-section peak residual (see "4-pole resonance" below).
- On looped noise (`sweep101`) the same swept voice is +1.0 dB against MAME, as in its static controls (+0.5 to +1.8 dB). The resonance tracks the falling cutoff without lag: the peak frequency matches MAME within the 2% resolution during the sweep.

So the earlier P13 level gap (−2.2 dB) is not a separate mechanism. The main and sub outputs are DC-coupled (SK524 sheet 20: AD1860 → RC → Sallen-Key low-pass → inverting buffer, no series capacitor); the analog low-pass peaks +2.5 dB at 18 kHz, compensating the DAC's zero-order hold, so DAC and analog stage are flat within ±0.7 dB to 20 kHz.

**Master volume.** The front-panel knob is read by the K-chip pot ADC and applied by EOS to the +$71/+$73 targets (centre pan caps at 235 = 255 − 20). The recordings of the 6th 2026-10-20 session use the knob at maximum, the same as MAME's VOLUME adjuster at 100; with the knob lower, voice volume above 0 dB is not capped, so +10 dB measures 7.7 dB instead of 0.7.

**Filter.** The remote-edit filter parameters are FTYPE (id 82) = index into the descriptor table at 0x07B776 (0 = 2-pole low-pass, 1 = 4-pole low-pass (the default voice), 2 = 6-pole low-pass, 3/4 = 2nd/4th-order high-pass, 5/6 = 2nd/4th-order band-pass, 7 = contrary band-pass, 8–10 = swept EQ, 11–14 = phasers and flanger, 15/16 = vocal, 17–20 = morph EQs), FMORPH (id 83) = cutoff, written as is to +$7C, and FKEY_XFORM (id 84) = Q, which picks one of 16 section words (Q >> 3; ¾ of Q for the 6-pole). Words EOS writes (MAME trace of all 21 types at Q 0/64/127):

| Type | Words |
|---|---|
| Low-pass 2/4/6-pole | the Q word in 1/2/3 sections at +$00/+$10/+$20 (`DWORD_ARRAY_0007BEE8`: 0x8408FFCF at Q 0 … 0x5205D4F3), +$24 high byte 0xF2 − sections × (Q >> 3) |
| High-pass 2nd/4th | +$04 = 0x80048004, Q word at +$10 (and +$20 with +$14 = 0x80048004 for the 4th order): 0x8004FFCF, 0x7004EFCF, 0x6104E0CF at Q 0/64/127 |
| Band-pass 2nd | +$00 = +$04 = 0xFF68FFCF, +$14 = 0xFF04FF90, Q word at +$20: 0x8808F8C8, 0x6808D8C8, 0x4908B9C8 |
| Band-pass 4th | +$04 = +$14 = 0xFF04FF90, the Q word at +$00 and +$20 |
| Types 7–20 | their own word sets (byte-structured; not decoded) |

In every type the Q word's high byte falls with Q.

Measured on the hardware (samplebot session `2026-10-20_e6400_filter`): sample 909 = looped white noise (`noise.wav` from `make_test_samples.py`), note 69, the filter set by remote edit before each 0.7 s note. The transfer function is the hardware spectrum over MAME's render of the same notes with the filter bypassed (the G-chip output is identical), Welch-averaged (`measurements/hchip_filter_tf.py`). Ladders: low-pass 2/4/6-pole cutoff 0, 16, …, 255 at Q 0; 2-pole cutoff every 4 steps; 2-pole Q 0, 8, …, 127 at cutoffs 64, 128, 192, 240; 4-pole Q at 128; high-pass and band-pass 2nd order cutoff at Q 0.
- Each section is a **state-variable filter** in Chamberlin form, L += F·B, H = x − L − D·B, B += F·H. Its low-pass, high-pass and band-pass outputs fit the 2-pole types to 0.2–0.8 dB rms at every cutoff (`measurements/hchip_svf_fit.py`). The 4- and 6-pole low-passes are 2 and 3 identical sections: at Q 0 the hardware 4-pole equals two 2-poles within ±0.1 dB, and the 6-pole three.
- **F** roughly doubles every 40.6 cutoff steps: 0.0119 at cutoff 0 (pole at 91 Hz), 0.119 at 128 (950 Hz), 1.04 at 255 (essentially flat to 20 kHz).
- **D at Q 0 depends on the cutoff:** 0.76–0.92 below cutoff 60, rising to 1.64 near 170, and back to 0.92 at 255. So the pole Q runs from 1.14 at the bottom to 0.52 at cutoff 192 and 0.72 at 255.
- **Q** lowers D by about 2^(−1/16) per step of the Q word's high byte (×0.09–0.13 at Q 127). It also raises the pole frequency, by ×1.15 at cutoff 64 and up to ×1.9 at 240. The low-pass passband drops with Q: −12 dB for the 2-pole at Q 127, −23 dB for the 4-pole.
- **High-pass and band-pass** use the same F within 2%. The high-pass output is 0.5 dB louder. The band-pass F is 0.8–0.95 times the low-pass F, with D higher at low cutoffs.

**Architecture (published sources, checked against the measurements).** The H-chip sections follow Dave Rossum's E-mu designs:

- **US 5,170,369** (Rossum, E-mu, filed 1990, priority September 1989; [Google Patents](https://patents.google.com/patent/US5170369A/en)). Later documents cite it as "5,170,639". It describes a canonical second-order section, y = x − b1·y₋₁ − b2·y₋₂, remapped to unsigned coefficients B1′ = b1 + 2 and B2′ = 1 − b2, with optional FIR zeros (A1′, A2′) and a mode that forces DC gain to unity.
  - The datapath runs 8 cycles per sample on 4 multipliers, with the delayed outputs truncated at the multiplier input. Up to 128th order fits on one chip.
  - Coefficients are log-coded: B2′ directly, B1′ as a difference from B2′, the top 4 bits driving a barrel shifter.
  - Each coefficient is interpolated linearly in the coded domain, C(x) = Ca + x(Cb − Ca). The interpolator takes an 8-bit destination, expanded to 16 bits by duplication (5a → 5a5a), and one of 8 approach times, 32·2ⁿ samples (0.6–80 ms in its Table II). It computes its own 8-bit increment: sign, 3 shift bits, 4 data bits.
- **Rossum, "The 'ARMAdillo' Coefficient Encoding Scheme for Digital Audio Filters"**, IEEE ASSP Workshop 1991 ([copy](http://www.rossbencina.com/static/junk/00634131.pdf)). It defines b2 ≈ 1 − e^(−k2) and b1 ≈ −2 + e^(−k2) + 4e^(−k1), with k1 and k2 running from 0 to 11.
  - So B2′ = e^(−k2) gives a resonance height of about 8.68·k2 + 6.02 dB, and B1′ − B2′ = 4e^(−k1) gives a frequency of about −k1/1.38 + 9.31 octaves.
  - The hardware uses an "octave shift" approximation of the exponential (within about 1 dB), with 8-bit codes.
- **Kyma list post** (Ben Phenix, 2005, relaying Joe Grisso; [archive](https://archive.symbolicsound.com/cgi-bin/bin/view/How/CreateEMUZ.html)), secondhand. It says Z-plane is seven two-pole sections in series and that the ARMAdillo coefficients are per two-pole section. It also says 64 two-pole filters fit a 20,000-gate ASIC in 1989, and that an "H2.0" with 384 filters on 250K gates was planned. **US 10,514,883** (Rossum, 2019) names Morpheus and UltraProteus as prior art but gives no H-chip detail.

Checked against the 2-pole low-pass fits by converting each fitted section to B1′ − B2′ = |1 − p|² (= F² in Chamberlin terms) and B2′ = 1 − R²:
- **Frequency coefficient:** log₂(B1′ − B2′) = −12.41 + b·(0.0493 + 0.000247·(byte₄ − 0xCF)). This fits all 115 cutoff and Q rungs to 0.035 octave rms (worst 0.15), where b is the cutoff and byte₄ is the Q word's low byte. Byte₃ has no effect. So the code is the 16-bit product cutoff × byte₄ (0xCF/4096 = 0.0505 octave per step), read as an octave-shift exponent with 4096 codes per octave. The 12.9-octave span over cutoff 0–255 sits inside ARMAdillo's 16-octave k1 range.
- **Radius coefficient:** across Q, log₂ B2′ changes by 1/16 octave per step of the Q word's high byte. That is a 4.4 octave-shift code, about 0.376 dB of resonance per code, the same step as the volume. Against cutoff, log₂(−ln R) is close to linear (0.07 octave rms) but B2′ itself is not. The part of the radius code that depends on cutoff is still unidentified.
- **Interpolator:** the duplicated-byte words (`00F200F2`, `80048004`, `57075707`) match the destination expansion. The +$46 latch (0, 1 or 4 before cutoff, volume and mute writes) is probably the approach-time select. A 512-sample approach (about 10.7 ms) reproduces the smooth release (EOS writes 13 steps every ~10 ms, 0.025 step per sample, against the fitted 0.028). It does not explain the ~1 ms attack.
- **Why Chamberlin fitted:** a Chamberlin SVF and the patent's direct form have the same second-order responses. The fitted F and D are therefore just another parametrisation of the ARMAdillo coefficients, which is why D looked irregular against cutoff.

**Word format (decoded 2026-10-20).** Traced in MAME across all 21 types (Q 0–127 and cutoff 0–255) and the morph types' extra parameters (ids 87–92). Checked against the hardware with ladder `survey_types` in session `2026-10-20_e6400_filter`: types 3–20 at cutoffs 48, 128 and 208, each at Q 0, 64 and 127.
- **Interpolation:** EOS writes the six words once per note. Cutoff reaches the chip only through +$7C, as the interpolation variable x = cutoff/255. Each 32-bit word is one coefficient set at two morph endpoints: the high half at x = 0, the low half at x = 1. Each half is two 8-bit ARMAdillo codes, [radius][frequency]. The chip interpolates each code linearly, code(x) = code₀ + x·(code₁ − code₀). In the Dual EQ Morph type, EQ1 Low (id 87) moves only the high halves of +$04/+$10 and EQ1 High (id 88) only the low halves.
- **Decoding**, 16 codes per octave for both:
  - radius: 1 − R = 2^((code_r − 252)/16)
  - frequency: V = |1 − p|² = 1 + b1 + b2 = 2^((code_f − 206.5)/16)
  - then b2 = R² and b1 = (1 − R²) + V − 2

  Radius code 0xFF gives R = 0, so b2 = 0 and the polynomial reduces to a first-order 1 + (V − 1)z⁻¹. With frequency 0xCF the root sits near 0, so `FFCFFFCF` passes signal through unchanged.
- **Layout:** +$00, +$10 and +$20 are pole polynomials (denominators); +$04 and +$14 are zero polynomials (numerators). All are cascaded. +$24 is not a zero pair; it acts as a gain word (`00F200F2`, never interpolated with cutoff). In the vocal types +$24 is unused.
  - Low-pass: poles only.
  - High-pass: zero pair at +$04 (`80048004`, just inside z = 1).
  - BP2: a first-order pole and zero that cancel (+$00/+$04), a first-order zero near DC (+$14) and the Q-word pole pair (+$20).
  - Swept EQ: zeros at +$14 and poles at +$20 with the same frequency codes, radii swapped for boost or cut.
  - Phasers: zeros on the unit circle (radius 0x30) with poles at radius 0xC0 to 0x81.
- **Gain:** the pole section at +$00 is always DC-normalised (numerator V, the patent's DC mode). Those at +$10/+$20 are DC-normalised only when bit 7 of the filter coefficient byte is 0. EOS writes that bit to the previous channel's +$7B; it is 0 for the low-pass and vocal types.
- **Accuracy:** with only the two code constants, the cascade predicts the measured shape (gain fitted per rung, dB rms) for:
  - 0.4–0.7: HP2/HP4, BP2, all three swept EQs, Dual EQ Morph, Peak/Shelf Morph
  - 0.5–1.0: Flanger
  - 0.6–1.4: Phaser 1 below cutoff 208, Bat-Phaser
  - 0.25–0.6: every 2-pole low-pass rung
  - worse: Phaser 2 at cutoff 208 (4 dB); the vocal types, 4-pole and contrary band-pass at high Q, 2EQ+LP at low cutoff and 2EQ Morph+Expression at Q 0 (1–3 dB)

  The phasers' large single-bin maxima are notch depths.
- **Open:** BP2, BP4 and both phasers lose about 7 and 14 dB at Q 64 and 127. This is a flat gain (twice as much for BP4) that keeps the band-pass peak near 0 dB. Bat-Phaser, HP and contrary BP keep their level for the same or larger radius changes. It isn't EOS (the volume stays E9) and isn't in any other word, so what selects it is unknown. The role of +$24's bytes is also open.
  - **Q ladders** (session `2026-10-20_e6400_qloss`: BP2, BP4 and LP2 at cutoffs 48/128/208, Q 0–127 in steps of 8; each rung compared with the Q 0 rung at the same cutoff, hardware against MAME, median over the bins above the floor). BP2 loses 0.235 dB per radius code of the +$20 Q word: 14.5–14.8 dB at Q 127 at all three cutoffs. Both halves of the word drop 63 codes and the frequency codes stay fixed, which is why it doesn't depend on cutoff. BP4, whose +$00 and +$20 both hold the Q word, loses twice as much: 28–31 dB, 1–2 dB more at low cutoff, with growing shape scatter. +$24 stays `00F200F2` throughout. LP2 matches MAME's +$24 step model within ±1 dB.
  - **One fit for all three:** −0.24 dB per radius code of the Q-dependent pole sections, interpolated at x and taken relative to the Q 0 word at the same cutoff, fits BP2, BP4 and LP2 together at 0.56 dB rms (max 2.2) with no +$24 term. For LP2 the +$24 step model fits equally well. The two models can't both be right, since together they would double the LP loss. Supporting the radius law: LP Q 112 and Q 120 have the same word but +$24 bytes E4/E3, and they measure the same.
  - **Not an absolute law:** an absolute per-code loss is ruled out because the Q 0 level is flat across cutoff, although its radius code spans about 70 codes. The decoded pole Q (θ / 2(1 − R)) fits worse (LP2 2.2 dB rms).
  - **All types** (survey_types re-analysed the same way against a current MAME render, Q 64/127 vs Q 0 at cutoffs 48/128/208). Only BP2, BP4, Phaser 1 (11) and Phaser 2 (12) lose level: −7.6 dB at Q 64 and −14.5 to −14.9 at Q 127 for BP2 and both phasers, and twice that for BP4, at all cutoffs. Every other type stays within ±0.7 dB of MAME (contrary BP at cutoff 208 Q 127 is noisy). That includes HP2/HP4 (+$10/+$20 radius −31), contrary BP (+$00 −127), the swept EQs (+$20 −63 with the +$14 zero +63), type 13 (+$10/+$20 −63 each), the flanger and the morphs. So no per-radius-code law covers all types. The phasers move two sections by 63 codes but lose one BP2's worth. Ruled out as the selector:
    - the section's decoded peak gain or peak-to-DC ratio (type 13's reach 72–77 dB without loss)
    - the +$7B mode bytes
    - any other register: between Q 0 and Q 127 EOS writes only the six words, +$44, +$70 (volume, same levels), +$78 and +$7C
  - Fitted rule for those four types only: −0.235 dB per radius code of the +$20 pole section, plus the +$00 section in BP4. This matches the loss rungs within 0.6 dB, except BP4 at Q 127 (1.2 and 1.9 dB at cutoffs 128 and 208, the rungs with the largest shape scatter) and Phaser 2 at cutoff 208, Q 64 (0.9 dB). Script: scratch survey_qloss.py, secpeak.py.
  - **No new selector in +$7B:** `hchip_write_filter_coeff` (0x50D82) writes only the coefficient's bit 7 to the previous channel's +$7B. The channel's own +$7B keeps bit 7 from +$3B (a neighbour's flag) and takes `0x77` in bits 0–6 for both LP and BP. `BYTE_ARRAY_00050D7E` is indexed by mode & 3, not by filter type.
- **Low-pass Q gain depends on cutoff** (session `2026-10-20_e6400_rung`, `lp4_noise_ladder`: 4-pole at Q 114 on cutoffs 32–192, Q 0/32/64 controls, compared with MAME rendering the same notes, plus the passband of the earlier 2-pole Q ladders). At high Q the hardware passband falls further below MAME's +$24 gain the lower the cutoff: −1.0 dB at cutoff 192 to −5.9 dB at 32 for the 4-pole at Q 114. Fitted over all ladders: each +$24 step is −0.75 + 0.224 (x − 0.78) dB, x = cutoff / 255 (0.46 dB rms; implemented in `emu_hchip`). The level follows the Q word rather than the byte, though: Q 120 and 127 share Q 112's word and measure the same despite one more step. Equivalently (0.44 dB rms) the per-section level is 0.265 dB per radius code plus 0.038 dB per frequency code relative to the Q 0 word at the same cutoff, close to the band-pass loss above (0.22 dB per radius code), so both may be one mechanism. Residuals: the 4-pole resonance peak at Q 114 stands about 1.6 dB higher above its passband than MAME's, and Q 64 at cutoffs 48–144 is over-corrected by about 1 dB.

- **4-pole resonance** (the transfer functions of session `2026-10-20_e6400_filter` against the cascade decoded from EOS's words, shape only; MAME renders of `lp2_q`/`lp4_q` agree). The 4-pole uses the 2-pole's Q word in both sections; EOS steps +$24 twice as fast, which changes only level. The hardware 4-pole is the 2-pole squared within about 1 dB. The resonance error is a per-section one that the 4-pole doubles.
  - 2-pole peak height above the passband, hardware − model: +0.3 to +1.2 dB at cutoffs 128–240, mostly about +0.7 (−0.2 at cutoff 240, Q ≥ 112). At cutoff 64 it is +0.3 to +1.0 up to Q 56 and −0.8 at Q ≥ 112.
  - 4-pole at cutoff 128: +0.6 to +2.2 dB (+1.0 at Q 112–127).
  - Peak frequencies agree within about 2%, except −6 to −8% at cutoff 240 and Q ≥ 104.
  - Re-fitting the four code constants over all the low-pass ladders gives radius 252.5 / 15.98 and frequency 206.3 / 16.09 codes. That barely changes the result: it trades mid against high Q, because the sign of the error changes with cutoff. Coding 1 − b2 directly (ARMAdillo's B2′) instead of 1 − R fits worse at high cutoff.
  - Left as is until the mechanism is known. Shape error is 0.2–0.6 dB rms per rung for the 2-pole (up to 1.9 at cutoff 240, Q ≥ 112) and 0.3–1.1 for the 4-pole. Scripts: scratch decode_fit.py, res_cmp.py.

**Not yet known:** the coefficient format, i.e. how the words produce these F, D and output choices (needed for types 7–20); Q for the high- and band-pass (only Q 0 is measured); the fall rates for latch values other than 1; the output setup words and the submix outputs.

**Not the H-chip:** the code the developer guide's H-chip section calls `hchip_init` (0x0636C2) and its tables drive the effects card at CSDSP. See [Effects card](#effects-card-g05--e-64-fx--emu8000-register-map) below. The Ghidra names are now `fx_*`.

**CTRLREG1 (0x400000):** bit 7 clear = G/H-chip reset, bit 10 = clock control used by the same init (unverified on the hardware). Doubtful: EOS's effects bypass (`fx_set_bypass` 0x63A88) also clears bit 7, and the effects probe pulses bit 10 as a card reset (`fx_card_reset_assert`/`_release`). Bit 7 is more likely the effects-return enable.

### Effects card (G0.5 / E-64 FX) — EMU8000 register map

Not fitted to the test unit; known from EOS 3.00 (Ghidra, `fx_*` names) and a MAME trace with a fake register file. The parts lists put an EMU8000 (IC405) and the "G0.5 FX PAL" on the G0.5 effects card (SK507/SK518). The Identity Reply table also has an "E64FX" model (03h,05h).

**Bus:** CSDSP 0x520000 (0x530000 on hardware variant 4). Each EMU8000 register has its own bus address, and the channel (0–31) goes to +$0E first. Bus offset = `reg << 4 | port`, with DATA0 at +4, DATA1 at +8, DATA2 at +A and DATA3 at +C. So +$04 CPF, +$14 PTRX, +$24 CVCF, +$34 VTFT, +$64 PSST, +$74 CSL, +$08 CCCA, +$18 HWCF/SMA* (DATA1 r1), +$28/+$2A/+$38/+$3A INIT1/INIT2/INIT3/INIT4, +$1C IFATN, +$58 DCYSUSV, and so on.

**Probe** (`fx_probe_and_init`): writes HWCF1/2/3 (+$18, ch 29/30/31) = $88/$10/$06 and needs HWCF2 to read back $50. Then it runs the EMU8000 audio init that the Linux `snd_emu8000` driver also uses: DCYSUSV $80, IFATN $FF00 and the rest 0 on all 32 channels, and SMALR/SMARR/SMALW/SMARW = 0.

**Program (INIT arrays): one fixed program, loaded at boot.** `fx_load_program` follows Linux's `init_arrays` order: write `fx_init1_table` (0x63F64, byte-identical to Linux init1), wait 150 ms, write it again with bit 15 set on the odd words, then write the target table with bit 15 set on the odd words, then the target as is. 128 words go to INIT1..4 × 32 channels.

- `fx_program_idle` (0x648EC, mode 1, during the probe): $3ECA/$0474 pairs.
- `fx_program_main` (0x64140, mode 2): the only runtime program. Compared with the full Linux init3 table:
  - INIT1/INIT2 half (words 0–63): the odd words equal Linux's except for bit 15 (often set in Linux, clear here) and word 1 (8480 vs 8470). All even words (probably delay-line addresses) differ.
  - INIT3/INIT4 half: rearranged. Linux's chorus words sit at other channels here (feedback E610 at INIT3 ch 9 becomes 6610 at ch 15; delay offset 031A at ch 12 becomes ch 18).
- `fx_program_diag` (0x649EC): self-test, used only by the unreferenced `fx_run_diag_program`.

No other program is ever loaded. The trace stepped all 45 FX A and 33 FX B types plus every master parameter, and none of it reloads the INIT arrays.

**Effect types are parameter sets.** These are traced with remote edit params 228–244. A type change crossfades the changed words over 64 steps (`fx_param_ramp_tick`, about 0.27 s for FX A and 0.53 s for FX B).

- **FX A** (`fx_a_apply_params`, 21-byte blocks in `reverb_effects[45]`): about 15–25 words in INIT1 ch 3, 5, 7, 9, 11, 15, 17, 19, 23, 25, 27, 31, INIT2 ch 1, 3, 7, 9, 11, 15, 17, 19, 23, 29, 31, INIT3 ch 1/3 and INIT4 ch 27/29. These are the same INIT1/INIT2/INIT3 positions as Linux's `reverb_cmds`. Linux's INIT4 $1F is at INIT4 ch 29 here. Type 2 (Room 2) equals Linux "room 2" word for word. Type 1 (Room 1) matches "room 1" except INIT2 ch 7/15/23 (7244 vs 7204). Types 0 and 1 are identical.
  - Decay → INIT1 ch 5 and INIT4 ch 29 low byte (= 2 × value).
  - HF damping → INIT2 ch 29/31 and INIT3 ch 1/3.
  - FxB→FxA → INIT4 ch 27 low byte.
- **FX B** (`fx_b_apply_params`, `effects[33]`): words at INIT3 ch 15 (feedback, Linux INIT3 9), INIT3 ch 18 (delay offset, Linux INIT3 $0C), INIT4 ch 3 (LFO depth), HWCF4 = +$18 ch 9 (delay) and HWCF5 = ch 10 (LFO rate = 32 × value). Types 2/3/4/5 = Linux chorus 2/3/4/"feedback" (feedback bit 15 clear: E608 → 6608). Type 7 (Slapback) = Linux "short delay" (HWCF4 $0006E000, $0B06). Types 0 and 1 are identical.
  - Feedback → INIT3 ch 15 low byte.
  - LFO rate → HWCF5.
  - Delay time writes only for the delay types (`fx_b` mode 1–4).
- **Sends**: the G-chip mix enters through the EMU8000 channels 0/2 (main), 8/10 (sub1)… as PTRX (FX A send), PSST and CSL high byte (FX B send). These are ramped by the main/sub send params.
- **Bypass** (244) writes nothing to the card; see CTRLREG1 bit 7.

**In MAME:** the driver has an `fx` slot ([e6400_fx.cpp](../src/mame/emusys/e6400_fx.cpp)), empty by default like the reference unit. `-fx g05fx` fits a register-file stub that passes the probe. EOS then enables its effects pages and writes the card, but no audio is processed. A real model needs an EMU8000 effects-engine model: reverb and chorus as defined by the INIT arrays and parameter words. That engine is undocumented, and MAME has no EMU8000 device. Without hardware, references could be AWE32 recordings of the shared modes (Room 1–3, Hall 1–2, Plate, Delay, Panning Delay; Chorus 1–4, feedback, flanger, short delay). Trace tooling: [measurements/fx/](measurements/fx/) (`fxgen.py` stimulus, `fxtrace.lua` fake card and logger, `fxdiff.py` per-edit diff, `fxdiff.txt` result).

---

*MIDI note processing, voice state machine, and audition key documentation moved to [EOS_developer_guide.md](EOS_developer_guide.md#midi-note-processing).*

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
| 0x010000–0x0FEFFF | Flash ROM (full image at CPU base 0x010000; size varies by firmware version) |

#### Flash Header (CPU 0x010000–0x0103FF)

The first 1024 bytes (2 sectors) of the flash chip form a header used by the firmware update process and version display. The ROM image files (`.raw`) include this header at offset 0, so the file maps directly to CPU address 0x010000. The format (confirmed from floppy disk images and firmware cross-reference):

| Flash Offset | CPU Address | Size | Content |
|---|---|---|---|
| 0x000 | 0x010000 | 4 | Magic: `0x12345678` |
| 0x004 | 0x010004 | 24 | Version string, space-padded (e.g. `"EOS v3.00b"`) |
| 0x01C | 0x01001C | 4 | ROM sector count (code only, excluding the 2-sector header) |
| 0x020 | 0x010020 | 4 | Checksum (32-bit, algorithm TBD) |
| 0x034 | 0x010034 | 4 | `0x00E00000` — consistent across all versions (unknown purpose, possibly flash base or size) |
| 0x038 | 0x010038 | 4 | `0x000002C9` — consistent across all versions (unknown purpose) |
| 0x040–0x3FF | 0x010040–0x0103FF | 960 | Zero padding |

Total image size = 0x400 (header) + sector_count × 512 (code).

**Per-version header values:**

| Version | Sectors | Code Size | Total Size | Checksum |
|---|---|---|---|---|
| EOS v2.80f | 0x075A (1882) | 0xEB400 | 0xEB800 (964,608) | 0xE7CE5FD5 |
| EOS v3.00b | 0x0778 (1912) | 0xEF000 | 0xEF400 (979,968) | 0x2DF72B2B |
| EOS v4.10a | 0x0898 (2200) | 0x113000 | 0x113400 (1,127,424) | 0x7F56834D |
| EOS v4.62  | 0x097E (2430) | 0x12FC00 | 0x130000 (1,245,184) | 0x19C17BEF |

Firmware references:
- `sub_A2AD4` (boot display): `pea ($10004).l` → `sprintf("Software: %s", version_string)`
- `sub_2E1E8` ("Saving System to Floppy"): `move.l #$10000,d2` (flash base); `move.l ($1001C).l,d1` (sector count); `addq.l #2,d1` (adds 2 sectors for header)
- `sub_25D6E` (boot): sets UPROMPGM = CR1 bit 15 (flash program enable)
- `sub_25CCA` (boot): flash type identification — AMD (manufacturer ID 1) or Intel (0x89)
- `sub_260C0`: flash ID command sequence — copied to RAM before execution since the flash chip cannot be read during the ID command

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
| GP7 | SCCINT | AM85C80 SCC (Z85C30) | Serial controller interrupt (/INT, active low, idles high; falling edge per AER) |

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

*EOS version history and firmware update disk format moved to [EOS_developer_guide.md](EOS_developer_guide.md#eos-firmware--rom-versions).*

---

*UI widget class hierarchy, vtable slot maps, and virtual dispatch documentation moved to [EOS_developer_guide.md](EOS_developer_guide.md#ui-widget-class-hierarchy-firmware-vtable-system).*

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
- [x] ~~Verify which CPU IPL level the MC68901 IRQ output connects to (driver uses IPL6 as placeholder)~~ → **IPL 3**: the MFP's IRQN drives both IPL1N and IPL0N (SK524 sheet 3); IPL2N comes from the CSPAL (IP821, NMIN input). EOS depends on it: `xmitQueuePush` (0x3F03E) masks to IPL 3 around writing the first MIDI byte and setting `midi_xmit_active`; at IPL 6 the SCC's TX-empty interrupt ran in between, cleared the flag first, and the transmit queue never drained, hanging anything that waits for it (`xmitQueueEmpty` 0x3EFD2, e.g. Copy Voice via 0x44BC6).
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
