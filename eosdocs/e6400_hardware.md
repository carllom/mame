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

---

## CPU

| Item | Detail |
|---|---|
| CPU | **MC68C020 @ 25 MHz** (IM405, U78) |
| Bus width | 16-bit data bus (BD[15:0]) — it is the 68EC020 variant with no full 32-bit external bus |
| Address space | 24-bit (A[23:0]) |

> **Note:** The parts list says "68C020 25 MHz" but the driver was wired as 24 MHz based on the crystal present. The boot diagnostic comment confirms 24 MHz crystal (ZX315, V1). The 25 MHz is the rated speed of the chip, not necessarily the actual clock used.

---

## Crystal Oscillators

| Ref | Part | Frequency | Usage |
|---|---|---|---|
| V1 | ZX315 | **24 MHz** | CPU clock |
| U56 | ZX314 | **16 MHz** | Unknown (FDC candidate) |
| U37 | ZX316 | **45.1584 MHz** | Audio sample clock (= 44100 × 1024) |
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
| U45 | ChipSel PAL | IP872 (rev a required) | Chip select generation (CS_PAL) |
| U33 | Sample PAL | IP751 | Sample address control |
| U73 | G0.5 FX PAL | IP826 | Effects sub-board PAL |

> From the update checklist: U80 **must** have IP822**d** installed; U45 must have IP872**a**.

### Chip Selects (from CS_PAL / ACT138 decoding in SK524)

The chip select PAL (U45, IP872) and two 74ACT138 demultiplexers decode the upper address lines. The following active-low chip select signals are generated:

| Signal | Peripheral |
|---|---|
| CSRJACKN | Rear-panel jack detection ADC |
| CSWGAINN | Wet/gain control |
| CSMFPN | MC68901 MFP (MIDI / timers / UART) |
| CSLCDN | LCD (T6963C via LM24014H) |
| CSFDCN | FDC (82078 floppy disk controller) |
| CSEXPN | Expansion port |
| CSDSPN | DSP (H-chip) |
| CSKCHIPN | K-Chip |
| CSRFIFON | IDT7202 FIFO |
| CSSCCN | 85C80 SCSI/SCC controller |
| CSHDDN | Hard disk (SCSI target) |
| CSHDCN | Hard disk control |
| CSHCHIPN | H-chip select |
| CSG1CHIPN | G-chip 1 |
| CSG2CHIPN | G-chip 2 |
| CSPOTADCN | Panel pot ADC |

### Serial / MIDI / System Controller

| Ref | Part | E-mu P/N | Description |
|---|---|---|---|
| U44 | MC68901 MFP PLCC-52 | IM408 | Multi-function peripheral: MIDI UART, timers, GPIO |

> **Important:** MC68901 is the **primary MIDI, timer, and UART** device. The boot diagnostic tests write/read this device as the first peripheral health check.  
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

## Boot Sequence (E4 Classic — applies to e6400)

The bootPROM runs a fixed sequence on power-up, with front-panel LEDs extinguishing as each test passes:

| LED | Test | Description |
|---|---|---|
| Master | CPU | CPU is running |
| Preset Manage | MC68901 MFP | Tests write/read to 68901 — MIDI, timer, UART |
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

## Memory Map (known / partial)

| Address Range | Content |
|---|---|
| 0x000000–0x0001FF | ROM vectors (mapped from Flash) |
| 0x000200–0x0003FF | Scratch RAM |
| 0x010800–0x0FF3FF | EOS flash (OS code) |
| 0xF00000–0xF7FFFF | CPU DRAM low bank (U58/U59) |
| 0xF80000–0xFFFFFF | CPU DRAM high bank (U65/U66/U67/U68) |

Peripheral addresses are decoded by CS_PAL (IP872) and the two 74ACT138s using upper address lines A[23:17] approximately. Exact register offsets require schematic SK524 for confirmation.

**Known offsets from driver work:**

| Address | Device | Notes |
|---|---|---|
| 0x400000 | CS register | Write to control LCD A0 (bit 8) and other chip selects |
| 0x560000–0x560007 | 82078 FDC | n82077aa-compatible |
| 0x580000–0x580003 | LM24014H LCD | Data/command selected via CSEL bit 8 |
| 0x5A0000 | ISR (interrupt service) | TBD |

---

## On-Board Diagnostics (accessed via front panel)

Password: **1-3-5-8** (notes of a major chord)

| Test | Description |
|---|---|
| Panel Test | LCD pixels, all buttons, encoder (0–36), volume pot (0–255) |
| RAM Test | CPU RAM (cRAM) then Sound RAM (gRAM), 4-pass pattern write/read |
| Jack Detection | Submix output jack insertion detection |
| Serial Test | MIDI loopback (Out→In), writes 0xAA then 0x55 |
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

- [ ] Confirm exact CPU clock: 24 MHz (crystal V1/ZX315) vs 25 MHz (chip rating)  
- [ ] Map all peripheral register addresses from SK524 schematic  
- [ ] Determine 82078 FDC clock source (16 MHz from U56/ZX314 is likely)  
- [ ] Identify AM85C80 SCSI/SCC register map and interrupt connection  
- [ ] Identify K-chip register interface and MIDI routing  
- [ ] Identify EMU8000 vs G2.0-chip distinction (parts list shows both IC402 and IC405 — may be different board revisions)  
- [ ] Confirm interrupt levels for all peripherals (MC68901 → CPU IPL lines)  
- [ ] Decode CS_PAL (IP872) address ranges for each peripheral window  
- [ ] Determine sound RAM address space (G-chip side)  
- [ ] Compute SHA1 for eos30b.raw ROM  
- [ ] Obtain SK524 schematic page images for full address decode  
