# Roland MV-30 MAME Driver Reference

## Hardware Overview

The MV-30 "Studio M" (1990) is a combined sequencer and sound module using a
Roland U-220 PCM sound engine with D-70-style TVF filters and effects.

### ICs & Clocks

| IC   | Part                  | Clock   | Role                          |
|------|-----------------------|---------|-------------------------------|
| IC19 | Intel 80C196KB-12     | 12 MHz  | Main CPU                      |
| IC1  | NEC uPD72068GF-389    | 32 MHz  | Floppy disk controller        |
|      | Roland MB87419/MB87420| 32.768 MHz | PCM sound engine (LP-1)    |
|      | MB654419U             | TBD     | TVF (time variant filter)     |
|      | TC23SC104AF-007       | —       | RCC (reverb/chorus/effects)   |
|      | TC23SC060             | —       | Key scan / LED gate array     |
|      | MN53015RRA            | —       | FSK gate array (tape sync)    |
|      | T6963C                | —       | LCD controller (240×64)       |
|      | BCC gate array        | —       | Banking, DMA, interrupt mux   |

### Memory

- **ROM**: 16 KB at boot (mapped to 0x0000–0x3FFF via page 0x780)
- **RAM**: 512 KB (pages 0x000–0x1FF, 1 KB granularity)
- **PCM wave ROM**: 6 × 512 Kbit ROMs (shared with D-70 waveforms)

---

## CPU: 80C196KB

The 80C196KB combines the i8x9x peripheral set (HSI/HSO, ADC, timers, serial)
with the i8xc196 extended instruction set (BMOV, CMPL, DJNZW, PUSHA, POPA).

### MAME Class Hierarchy

```
cpu_device
  └─ mcs96_device            (src/devices/cpu/mcs96/mcs96.cpp)
      └─ i8x9x_device        (src/devices/cpu/mcs96/i8x9x.cpp)
          └─ i80c196_device   (same file — NOT i8xc196_device)
              └─ c80c196kb_device
```

**Important**: `i8xc196_device` is a separate class (different branch). The
MV-30 uses `i80c196_device` / `c80c196kb_device`.

### Key CPU Modifications for MV-30

1. **AS_OPCODES support** — Added to MCS96 core (`mcs96.cpp/h`). The 80C196KB
   has separate execute and data bank registers. MAME's AS_PROGRAM handles
   data access; AS_OPCODES handles instruction fetch. The driver provides
   `opcodes_map()` with execute-bank-aware lambdas.

2. **EXTINT1 (INT13)** — Added `fetch_196_full()` in `i80c196_device`. Checks
   EXTINT1 before the standard INT00–INT07 priority chain. IMASK1 bit 5
   enables it; vector at 0x203A.

3. **IMASK1/IPEND1** — SFR registers at 0x13/0x12 for INT08–INT15. Added via
   `internal_regs()` override in `i80c196_device`.

4. **PUSHA/POPA** — Save/restore IMASK1:WSR alongside standard registers.

5. **IOC1 bit 1** — Routes EXTINT to EXTINT1 when set (instead of standard EXTINT).

### Stub Opcodes (TODO)

- `bmov` / `bmovi` — Block move
- `cmpl` — Compare long
- `idlpd` — Idle/powerdown

---

## Banking System

Eight 16-bit page registers at SFR 0x100–0x10E control four 16 KB windows:

| Register | Window          | Type    |
|----------|-----------------|---------|
| 0x100    | 0x0000–0x3FFF   | Execute |
| 0x102    | 0x4000–0x7FFF   | Execute |
| 0x104    | 0x8000–0xBFFF   | Execute |
| 0x106    | 0xC000–0xFFFF   | Execute |
| 0x108    | 0x0000–0x3FFF   | Data    |
| 0x10A    | 0x4000–0x7FFF   | Data    |
| 0x10C    | 0x8000–0xBFFF   | Data    |
| 0x10E    | 0xC000–0xFFFF   | Data    |

Physical address = `page_register_value × 0x400`.

### Special Pages

- **0x780** = ROM (boot page for window 0)
- **0x474** = I/O devices (data window 3 power-on default)
- **0x000–0x1FF** = RAM (512 KB)

### MAME Implementation

- Four `memory_view` objects (`m_view0`–`m_view3`)
- View 0 has two entries: `[0]` = ROM (boot), `[1]` = banked RAM
- View 3 has two entries: `[0]` = banked RAM, `[1]` = I/O devices
- Views 1 and 2 have one entry each: `[0]` = banked RAM
- Bank register writes dynamically select view entries
- Execute banks handled by `opcodes_map()` with runtime lambdas

---

## I/O Device Map (view3[1], base 0xC000)

Devices mapped 0x800 apart:

| Address       | Device         | Bus   | Handler                               |
|---------------|----------------|-------|---------------------------------------|
| C000–C7FF     | RCC            | 8-bit | `rcc_r` / `rcc_w` (stub)             |
| C800–C8FF     | Key scan / LED | 8-bit | `keyscan_r` / `keyscan_w`            |
| D000–D001     | FDC (uPD72068) | 16-bit| `upd72068_device::map`               |
| D800–D81F     | PCM (LP-1)     | 16-bit| `mb87419_mb87420_device::read/write` |
| E000–E003     | FSK            | 8-bit | `fsk_r` / `fsk_w` (stub)             |
| E800–E802     | LCD (T6963C)   | 8-bit | `t6963c_device::read/write`          |
| F000–F0FF     | TVF (MB654419U)| 16-bit| `tvf_r` / `tvf_w` (stub)             |

---

## BCC Gate Array

The BCC (Banking/Clock/Control) gate array handles:

1. **Memory banking** — Page registers at SFR 0x100–0x10E
2. **DMA controller** — FDC ↔ RAM transfers via registers at SFR 0x118–0x11D
3. **Interrupt multiplexer** — Routes multiple sources to CPU EXTINT1

### DMA Registers (SFR 0x118–0x11D)

| Address | Purpose                                                    |
|---------|------------------------------------------------------------|
| 0x118   | Transfer byte count (decrements to 0)                      |
| 0x11A   | Low 16 bits of physical RAM byte address (auto-increment)  |
| 0x11C   | Low byte = high address bits; high byte = control (0x87)   |

Physical address = `((reg_0x11C & 0xFF) << 16) | reg_0x11A`

When count reaches 0, BCC fires DMA-complete interrupt. Reading 0x11D
acknowledges DMA-complete. The BCC has **no TC connection** to the FDC —
only CPU PORT2 bit 5 connects to FDC TC. The firmware's DMA-complete handler
asserts TC via software.

### Interrupt Controller

The BCC multiplexes four sources onto CPU EXTINT1, intercepting the vector
fetch at 0x203A to add a source-specific offset:

| Source | Priority | Vector Offset | Handler Base |
|--------|----------|---------------|--------------|
| KEY    | 1 (low)  | +0x04         | 0x0124       |
| FDC    | 2        | +0x08         | 0x0128       |
| FSK    | 3        | +0x0C         | 0x012C       |
| DMA    | 4 (high) | +0x10         | 0x0130       |

- Priority-based: higher-numbered sources preempt lower
- Pending sources tracked as bitmask (`m_bcc_irq_pending`)
- Edge-sensitive: `bcc_clear_source()` deasserts EXTINT before promoting
  the next source to guarantee a rising edge for re-triggering
- Vector intercept installed in **both** view0[0] (ROM) and view0[1] (RAM)

---

## CPU Port Connections

### PORT1 (P10–P17)

- P10–P12: Analog slider MUX select (8 sliders via ACH0)
- P13: MUX inhibit

### PORT2 (P20–P27)

- P23: /READY from floppy (input, active low)
- P24: /DSKCHG from floppy (input, active low)
- P25: TC to FDC (output)
- P26: /DRVSEL to floppy (output, active low)

### ADC Channels

- ACH0 (P00): Slider value via 8-way MUX
- ACH5 (P05): Encoder direction latch
- ACH6 (P06): Encoder moved flag

### HSO

- HSO2: Resets encoder "moved" latch

---

## FDC: uPD72068

Custom class `upd72068_device` in `src/devices/machine/upd765.cpp`, derived
from `upd765_family_device`. Key differences from standard uPD765:

- `auxcmd_w()` at register offset 1 (aux command register)
- Motor enable via aux command (not PORT2)
- Logging: `command_end` with st0/st1/st2, `auxcmd_w`, `recalibrate_start`, `poll`

The FDC uses 3.5" DD floppies (720 KB, MFM, PC format).

---

## TVF: MB654419U (Time Variant Filter)

Mapped at F000–F0FF (7-bit word addresses, 16-bit data bus). Stub only —
no filtering logic implemented.

### Observed Register Usage (from firmware)

- **Reg 0x20**: Voice/channel select (values 0x00–0x0C = 13 voices)
- **Regs 0x00–0x0C**: Global/control registers
  - 0x0A = 0x001F (likely channel enable mask)
  - 0x04 = 0x1000
  - 0x0C = 0x0016
- **Regs 0x10–0x1B**: Per-voice filter parameters (written after voice select)
  - 0x18 = 0x4000
  - Others typically 0x0000
- **Regs 0x08–0x09**: Can be 0x0000 or 0xFFFF (bypass/enable flags?)
- Write-only during normal operation (0 reads observed)
- ~1,358 writes in 30 seconds of operation

The same chip appears in Roland S-50, S-330, and W-30 drivers (commented out,
referencing `mb654419u_device` which doesn't exist yet).

---

## Other Peripherals

### PCM Sound Engine (MB87419/MB87420 LP-1)

U-220 compatible. 32.768 MHz clock. Wave ROMs shared with D-70. IRQ routed
to CPU INPUT_LINE_IRQ1. Actively accessed (~28K PCM register writes in 30s).

### LCD (T6963C)

240×64 pixel display with 8 KB RAM. Actively shows UI (cursor/display updates
observed in logs).

### Key Scan (TC23SC060)

8×8 matrix via SC0–SC7 rows. Buttons and slider select buttons. 8 analog
sliders via MUX on ACH0. Rotary data dial via ACH5/ACH6 with HSO2 latch reset.

### RCC (TC23SC104AF-007)

Reverb/chorus/effects processor. Stub only — reads return 0.

### FSK (MN53015RRA)

Tape sync gate array. Stub only.

### MIDI

Bit-bang receiver via `midi_in_w()` with timer-based delivery to CPU serial
port. TX via MCS96 built-in serial TX callback.

---

## Build & Run

```bash
# Build (tiny target with debug symbols)
make SUBTARGET=tiny -j12 DEBUG=1

# Run with floppy disk image
./mametinyd mv30 -flop1 MV30_OS_V1_091.dsk -nodebug -log

# Run with debugger
./mametinyd mv30 -flop1 MV30_OS_V1_091.dsk -debug -log

# Firmware disassembly reference
cat mv30bios.asm
```

### Key Files

| File | Purpose |
|------|---------|
| `src/mame/roland/roland_mv30.cpp` | Machine driver (all hardware) |
| `src/devices/cpu/mcs96/mcs96.cpp` | MCS96 base CPU (AS_OPCODES support) |
| `src/devices/cpu/mcs96/mcs96.h` | MCS96 header (opcodes_config) |
| `src/devices/cpu/mcs96/i8x9x.cpp` | i8x9x/i80c196 CPU (EXTINT1, IMASK1) |
| `src/devices/cpu/mcs96/i8x9x.h` | CPU header (fetch_196_full, m_imask1) |
| `src/devices/machine/upd765.cpp` | FDC (upd72068_device) |
| `src/mame/tiny.lst` | Tiny build target list |
| `src/mame/mame.lst` | Full build target list |

---

## Current Status

- **Boot**: Fully boots from floppy (168 sector reads, OS loaded to RAM)
- **Display**: LCD active, shows UI
- **Input**: Key scan, sliders, rotary encoder all functional
- **Sound**: PCM engine initialized, register writes active
- **MIDI**: Basic bit-bang RX/TX
- **FDC**: Reads working, DMA working, interrupt chain complete
- **Timer crash**: `emu_timer::schedule_next_period` assertion in DEBUG mode only (pre-existing MAME issue)

### Not Yet Working / Stubs

- RCC (reverb/effects) — returns 0
- FSK (tape sync) — returns 0
- TVF (filter) — logs only, returns 0
- FDC write path (RAM → FDC) untested
- Sound output not verified (PCM writes occur but audio quality unknown)
- BMOV/BMOVI/CMPL/IDLPD opcodes are stubs

---

## Known Issues & Gotchas

1. **i80c196_device vs i8xc196_device**: These are DIFFERENT classes. MV-30
   uses i80c196_device. Do not modify i8xc196_device.

2. **View shadowing**: Bank registers and DMA registers must be installed in
   BOTH view0[0] and view0[1], otherwise the underlying view handler shadows
   them when the view changes.

3. **BCC vector intercept**: Must be in both view entries (ROM and RAM). The
   BCC always intercepts 0x203A regardless of banking state.

4. **EXTINT edge sensitivity**: When clearing a BCC source, must deassert
   EXTINT before promoting the next pending source. Otherwise the CPU won't
   see a rising edge and won't take the new interrupt.

5. **Floppy ready_r()**: Returns false when ready (active-low signal). The
   condition `!floppy->ready_r()` means "disk is ready".

6. **DMA has no TC**: The BCC gate array has no Terminal Count connection to
   the FDC. Only CPU PORT2 bit 5 connects to FDC TC. DMA-complete fires a
   BCC interrupt; the firmware handler asserts TC via software.

7. **DMA discard after count=0**: After the byte count reaches 0, the DMA
   must continue consuming FDC DRQ bytes (discarding them) to prevent FDC
   FIFO overrun while the FDC finishes the sector.

8. **DMA address**: `phys = ((adr_hi & 0xFF) << 16) | adr_lo`. The high byte
   of adr_hi (0x11D) is a control register (0x87 = enable), not address bits.
