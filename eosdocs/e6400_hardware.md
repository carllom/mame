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

## Boot Sequence

### Cold-boot execution flow

`eos30b.raw` **is** the cold-boot ROM — no prior bootprom stage exists.

1. CPU fetches ISP (0xFFBFFE) and reset PC (0x034FD0) from the vector table alias at 0x000000. These bytes are the first 8 bytes of the flash ROM, aliased there by CS_PAL hardware.
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

1. `loc_23948`  — reads hardware variant from 0x408000 bits[11:9]; stores result in `byte_F00A84`
2. Programs CS_PAL control registers (0x400000, 0x404000) with initial chip-select masks
3. Passes 0x4C0000 to `sub_20814` (SCC channel A reset)
4. Passes 0x54FF00 to `sub_21976` (SCC channel B probe with WR13 readback test)
5. If SCC probe fails → error: passes 1 and 0x54FF00 to `sub_3DE5E` + `sub_21608`
6. `sub_200F2` — **MC68901 MFP full initialization** (see register table above)
7. `sub_3FA16(0x21)` — stores 0x21 in `byte_F01450`
8. `sub_23B2C`  — reads hardware variant again; selects SCSI terminator scheme
9. `sub_872DC`  — reads 0x5E0000 bits[6:1] and [0]; stores board config
10. `init_mem`  — memory size detection
11. `sub_502AC` — effects DSP RAM probe: write/read test at 0x540400–0x540407
12. `loc_2535C/25302` — sets up memory zone headers

### Boot diagnostics (from EOS Technical Documents — E4 platform)

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

## Memory Map

### CPU RAM and ROM

| Address Range | Content |
|---|---|
| 0x000000–0x0001FF | ROM vector table (hardware mirror via CS_PAL — same bytes as 0x010000–0x0101FF) |
| 0x000200–0x0003FF | Scratch RAM |
| 0x010000–0x0FEFFF | Flash ROM (eos30b.raw, full image at CPU base 0x010000) |
| 0xF00000–0xF7FFFF | CPU DRAM low bank (2× HM514260 256K×16) |
| 0xF00400–0xF063FF | API jump table mirror (0x6000 bytes copied from ROM 0x0F9400 on boot) |
| 0xF80000–0xFFBFFE | CPU DRAM high bank |
| 0xFFBFFE | ISP — initial stack pointer (from vector table) |

### Peripheral Map

Decoded by CS_PAL (IP872) + two 74ACT138 demultiplexers. All addresses word-aligned; byte devices on even addresses via D[15:8].

| Address | Device | Status | Notes |
|---|---|---|---|
| 0x400000 | CS_PAL control register 1 | **Confirmed** | Word write; controls chip selects and LCD A0 (bit 8) |
| 0x404000 | CS_PAL control register 2 | **Confirmed** | Word write; additional peripheral enables |
| 0x408000 | Hardware ID register | **Confirmed** | Read word; bits[11:9] = variant code 0–4 (0=proto, 1=e6400, 2=e6400 Ultra, …) |
| 0x480000–0x48000E | NCR5380-compatible SCSI | **Confirmed** | AM85C80 SCSI port; word-spaced byte registers. 0x48000A = Reg5 Bus&Status (bit6=DRQ) |
| 0x4A0000 | SCSI DMA data port | **Confirmed** | AM85C80 DMA read port; data burst via `move.b (a3)+,(a2)` DMA loop |
| 0x4C0000 | SCC channel A control | *Tentative* | AM85C80 Z85C30-compatible; channel A at +4, init writes 0x09/0x80 (WR9=RESA) |
| 0x500000–0x50000E | Unknown, word-spaced byte regs | *Unknown* | Byte access at offsets 0,2,4,6,8,A,E; write to +0xE controls bus; probed in SCSI boot check |
| 0x540000 | Unknown write | *Tentative* | Single word write; may be variant config or effects enable |
| 0x540400–0x540407 | Effects DSP RAM | *Tentative* | Longword read/write test (0x12345678/0x87654321) on boot |
| 0x54FF00 | SCC channel B control | *Tentative* | AM85C80 Z85C30-compatible; channel B at base; WR13 baud rate probe confirms Z85C30 |
| 0x560000–0x560007 | 82078 FDC | **Confirmed** | n82077aa-compatible register map |
| 0x580000–0x580003 | LM24014H LCD (T6963C) | **Confirmed** | Data/command selected via CS_PAL bit 8; single byte port |
| 0x5A0000–0x5A002F | MC68901 MFP | **Confirmed** | Word-spaced byte registers; full init in boot ROM sub_200F2 (see below) |
| 0x5C0000 | Unknown byte write | *Tentative* | Variant code (0–4) mirrored here from ID result — possible K-chip or G-chip config |
| 0x5E0000 | Hardware config register | *Tentative* | Bits[6:1] and bit[0] read at boot; stored as board config flags |

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
- [ ] Verify MC68901 MFP clock source and divider (currently using 24 MHz placeholder)
- [ ] Verify which CPU IPL level the MC68901 IRQ output connects to (driver uses IPL4 as placeholder)
- [ ] Confirm MFP data bus connection: D[15:8] (even byte) or D[7:0] (odd byte)? Driver uses umask16(0xff00).
- [ ] Identify device at 0x500000–0x50000E (K-chip IT433? second SCSI window? IDT7202 FIFO?)
- [ ] Confirm the AM85C80 SCC address windows (0x4C0000 and/or 0x54FF00)
- [ ] Identify device at 0x5C0000 (receives hardware variant byte on boot)
- [ ] Identify 0x5E0000 config register (bit layout and meaning)
- [ ] Determine 82078 FDC clock source (16 MHz from U56/ZX314 is likely)
- [ ] Map all CS signals to exact address ranges from schematic SK524
- [ ] Obtain SHA1 for eos30b.raw ROM (currently placeholder in driver)
- [ ] Map G-chip (sound engine, IC402), H-chip (digital filter, IC413), K-chip (IT433) addresses
- [ ] Understand effects DSP RAM layout at 0x540000+
- [ ] Identify K-chip register interface and MIDI routing  
- [ ] Identify EMU8000 vs G2.0-chip distinction (parts list shows both IC402 and IC405 — may be different board revisions)  
- [ ] Confirm interrupt levels for all peripherals (MC68901 → CPU IPL lines)  
- [ ] Decode CS_PAL (IP872) address ranges for each peripheral window  
- [ ] Determine sound RAM address space (G-chip side)  
- [ ] Compute SHA1 for eos30b.raw ROM  
- [ ] Obtain SK524 schematic page images for full address decode  
