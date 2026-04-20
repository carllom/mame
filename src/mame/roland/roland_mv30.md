# Roland MV-30 MAME Driver Reference

Run driver with: `./mametinyd mv30 -window -resolution 960x256 -nofilter -flop1 mv30os109.dsk`
Add `-debug` for the debugger and `-log` for logging output.

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

### LED register (TC23SC060)

| Reg. | Bit7 | Bit6 | Bit5 | Bit4 | Bit3 | Bit2 | Bit1 | Bit0 |
|------|------|------|------|------|------|------|------|------|
| 08 | - | CH1(G) 0 | CH8(R) 7 | - | - | BEAT0(G) 8 | - | P EDIT(R) 25 |
| 09 | - | CH2(G) 1 | CH7(R) 6 | COMPU(mixmode)(R) 12 | - | BEAT0(R) 8 | - | T EDIT(R) 26 |
| 0A | - | CH3(G) 2 | CH6(R) 5 | MANUAL(mixmode)(R) 13 | - | BEAT1(G) 9 | - | SYSTEM(R) 27 |
| 0B | - | CH4(G) 3 | CH5(R) 4 | SONG SELECT(R) 14 | - | BEAT2(G) 10 | - | DISK(R) 28 |
| 0C | - | CH4(R) 3 | CH5(G) 4 | STATUS(R) 15 | REC(R) 19 | BEAT3(G) 11 | P REALTIME(R) 21 | P MICROSTEP(R) 29 |
| 0D | - | CH3(R) 2 | CH6(G) 5 | LOCATE(R) 16 | - | - | T REALTIME(R) 22 | T MICROSTEP(R) 30 |
| 0E | - | CH2(R) 1 | CH7(G) 6 | MARK(R) 17 | START/STOP(G) 20 | - | COMPUMIX(R) 23 | TIMBRE EDIT(R) 31 |
| 0F | - | CH1(R) 0 | CH8(G) 7 | TEMPO(R) 18 | - | - | PLAY(R) 24 | CHAINPLAY(R) 32 |

LED registers are inverted before being sent out on the led bus, so a 1 bit in the register gives a 0 on the LEDn line, which turns on the led: SCm (high) connected through led to LEDn (low).

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
- **Sound**: PCM engine initialized, register writes active, sequencer timer chain functional
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

9. **HSO CAM same-tick match suppression** — See dedicated section below.

---

## HSO CAM Same-Tick Match Suppression (`hso_cam_committed`)

### Summary

A new bitmask `hso_cam_committed` was added to `i8x9x_device` to prevent
freshly committed HSO CAM entries from triggering in the same `internal_update`
call that committed them. This fixes a critical timing issue where the MV-30
firmware's sequencer timer chain died at boot and never recovered, preventing
the sequencer from advancing and producing any audio output.

### Background: i8x9x HSO (High Speed Output) CAM

The i8x9x HSO subsystem contains an 8-slot Content-Addressable Memory (CAM).
Software arms a timer event by writing a command byte to SFR 0x06
(`HSO_COMMAND`) and a 16-bit match value to SFR 0x04 (`HSO_TIME`). The write
to `HSO_TIME` commits the entry into the first available CAM slot. The
hardware continuously compares each active slot's time value against TIMER1
(or TIMER2). When a match occurs, the associated action executes (set IOS1
bits, trigger IRQ, etc.) and the slot is freed.

In MAME, `commit_hso_cam()` stores the entry and immediately calls
`internal_update(total_cycles())`, which scans all active slots for matches
against the current timer value. This means a newly committed entry whose
target time equals the current TIMER1 value will fire **instantly** — within
the same call that committed it.

### The Problem on Real Hardware

On real silicon, the HSO comparator operates synchronously with the timer
clock. TIMER1 increments once every 8 CPU clock cycles (state times). The
comparator evaluates matches at timer increment boundaries. When software
writes `HSO_TIME`, the value is latched into the CAM, but it cannot
participate in a comparison until the **next** timer tick boundary. If the
written value happens to equal the current TIMER1 value, the comparison
window for that tick has already passed — the entry will not match until
TIMER1 wraps all the way around (65,536 ticks later, ~43.7ms at 12 MHz).

The existing `timer_time_until()` function already handles this correctly for
**scheduling purposes**: it has `if(!tdelta) tdelta = 0x10000;` which maps a
zero delta (target == current) to a full wrap. But `internal_update()` was
checking for exact equality (`t == current_timer1`) on **all** active slots
including the one just committed, bypassing `timer_time_until()`'s wrap logic.

### The MV-30 Failure Mode

The MV-30 firmware uses four HSO channels (commands 0x38–0x3B) as software
timers via IOS1 bits 0–3, with IRQ_SOFT (INT5) as the interrupt source:

| HSO Cmd | IOS1 Bit | Purpose                              |
|---------|----------|--------------------------------------|
| 0x38    | 0        | Fast timer (~100µs period)           |
| 0x39    | 1        | MIDI/system timer (~1ms period)      |
| 0x3A    | 2        | Sequencer timer (tempo-dependent)    |
| 0x3B    | 3        | Tempo subdivider                     |

Each timer is self-re-arming: when INT5 fires and finds IOS1 bit N set, the
handler computes the next target time (current + interval) and writes a new
HSO entry. This creates a recurring chain.

The sequencer timer (0x3A) is initially armed during boot at address 0x8081:

```asm
; Boot code at SYSOL_P2E::8081
LDB  HSI_status, #0x3A      ; HSO command = 0x3A (IOS1.2 + IRQ_SOFT)
ADD  HSI_time, TIMER1, [5D18h]  ; Target = TIMER1 + interval at 0x5D18
```

The interval variable at 0x5D18 lives in the **data bank** (page 0xA1 =
uninitialized RAM), not the execute bank (page 0x2E where the firmware code
resides and where 0x5D18 contains 0xF099). At boot, the data bank RAM is
all zeros, so the ADD computes `target = TIMER1 + 0 = TIMER1`.

**Without the fix**: `commit_hso_cam()` → `internal_update()` finds
`target == current_timer1` → immediate trigger → IOS1 bit 2 set + IRQ_SOFT
pending. But interrupts are still **disabled** during boot (the boot code
enables INT_MASK only later at 0x80B2). The boot code itself reads IOS1 at
0x80A6 to clear bit 2 as a side effect, and clears INT_PENDING at 0x80AC.
The 0x3A event is consumed before the INT5 handler ever runs.

After boot, the INT5 handler (at 0x01FB) is the **only** code path that
re-arms 0x3A. It reads IOS1 into a register, checks bit 2, and if set,
computes the next target and writes a new HSO entry. But IOS1 bit 2 is never
set again because no HSO 0x3A entry is active. There is also a main-loop
path (FUN_867a → FUN_8608 → FUN_86D6) that can arm 0x3A, but it checks a
snapshot of IOS1 taken during the INT5 handler — which also requires bit 2
to have been set. **Result: chicken-and-egg. The 0x3A timer chain is dead.**

Timers 0x38 and 0x39 survive because their initial intervals are non-zero
(programmed from firmware constants in the execute bank), so their first
`ADD HSI_time, TIMER1, [interval]` produces a target in the future, avoiding
the immediate-trigger problem.

**With the fix**: The initial 0x3A commit with `target == TIMER1` does NOT
trigger immediately. `timer_time_until()` correctly schedules the match for
65,536 ticks later (~43.7ms). When it fires, INT5 bit 2 handler runs, the
firmware has by then written a proper tempo interval to 0x5D18, and the
re-arm computes a correct future target. The recurring chain is established
and the sequencer starts advancing.

### Implementation

A new `u8 hso_cam_committed` bitmask in `i8x9x_device` tracks which CAM
slots were just committed:

**`commit_hso_cam()`** — Sets the bit for the newly used slot:
```cpp
hso_active |= 1 << i;
hso_cam_committed |= 1 << i;  // mark as just-committed
```

**`internal_update()`** — Skips just-committed slots during match check,
then clears the mask so they participate in future updates:
```cpp
for(int i=0; i<8; i++)
    if(BIT(hso_active, i) && !BIT(hso_cam_committed, i)) {
        // ... match check and trigger ...
    }
hso_cam_committed = 0;  // clear after first pass
```

The mask is also saved/restored (`save_item`) and reset in `device_reset()`.

### Why This Is Correct

1. **Matches real hardware timing**: On real silicon, a CAM entry cannot match
   on the same timer tick it was written. The comparator evaluates at tick
   boundaries; a mid-tick write is latched but not compared until the next
   tick. The `hso_cam_committed` mask models this one-tick latency.

2. **Existing wrap logic already handles the deferred case**: When the
   committed entry's time equals the current timer, `timer_time_until()`
   returns `current_time + 0x10000 * 8` (one full 16-bit wrap), which is
   correct — the entry fires on the next occurrence of that timer value.

3. **No impact on normal operation**: When software arms an HSO entry with
   a future target time (the common case), the entry would not have matched
   in `internal_update()` anyway. The mask only affects the edge case where
   target == current.

4. **Affects all i8x9x-based drivers**: This is a core CPU fix, not
   MV-30-specific. Other Roland drivers (D-50, D-70, S-330, MT-32, etc.)
   using the same HSO mechanism may also benefit, as any firmware that arms
   an HSO entry with a target equal to the current timer value would have
   experienced the same premature trigger.

### Verification

After the fix, the MV-30 sequencer timer chain (0x3A → 0x3B) starts
recurring. The INT5 handler processes bit 2 (sequencer tick) and bit 3
(tempo subdivider) events. The sequencer measure counter advances and audio
output begins.

---

## Fetch IRQ Dispatch Guard (`level < 0`)

### Summary

A bounds check was added to the interrupt dispatch loop in `mcs96ops.lst`
(the base `fetch` microcode) and in `i8x9x.cpp` (`fetch_196_full` for
80C196KB). Without this guard, a stale `irq_requested` flag could cause
the CPU to dispatch a bogus interrupt vector, corrupt the stack, and crash
the emulated system.

### The Bug

The MCS96 fetch sequence checks `irq_requested` at the start of every
instruction. If true, it scans for the highest-priority pending interrupt:

```c
int level;
for(level = 7; level >= 0 && !(PSW & pending_irq & (1<<level)); level--);
```

This loop simultaneously checks the interrupt mask (low byte of PSW) and
the pending interrupt register. It exits in one of two ways:

1. **Match found** (`level` = 0–7): A pending interrupt is enabled in the
   mask. Normal dispatch proceeds.
2. **No match** (`level` = -1): The loop scanned all 8 bits and found no
   pending interrupt that is also enabled in the mask.

The original code assumed case 2 could never happen because `check_irq()`
sets `irq_requested` only when `(PSW & pending_irq) && (PSW & F_I)`.
However, the flag can become stale between `check_irq()` and the next
`fetch`:

- **PUSHF / DI**: The firmware saves PSW and disables interrupts. This
  clears F_I in PSW, making all mask bits invisible to the loop, but
  `irq_requested` remains true from the prior `check_irq()`.

- **int_pending_w**: Software writes to INT_PENDING (SFR 0x09) to clear
  bits. `check_irq()` is called after the write, but `irq_requested` may
  have been true from an earlier evaluation with those bits still set.

- **Interrupt acknowledged between check and fetch**: Another interrupt
  source (e.g., EXTINT1 in `fetch_196_full`) may service the condition
  that caused `irq_requested`, but the flag is not re-evaluated before
  the INT00–INT07 scan.

With `level = -1`, the original code executed:

```c
pending_irq &= ~(1 << -1);     // Undefined behavior in C++
OP1 = -1;                       // level = -1
PC = any_r16(0x2000 + 2*(-1));  // Read from 0x1FFE — bogus vector
```

This would push a return address onto the stack and jump to an arbitrary
address, corrupting the firmware's execution state.

### The MV-30 Trigger

The MV-30 firmware makes heavy use of `PUSHF` + implicit `DI` in its main
loop and interrupt handlers:

- **FUN_88E5** (A/D conversion): Starts with `PUSHF` (saves PSW, disables
  interrupts), processes analog inputs, exits with `POPF` + `RET`.
- **INT5 handler** (0x01FB): Starts with `PUSHF`, reads IOS1, processes
  HSO timer events, exits with `POPF` + `RET`.
- **FUN_8608**: Checks IOS1 snapshot bits, exits with `POPF` + `RET` (the
  POPF matches a PUSHF earlier in the call chain from FUN_88E5).

Each `PUSHF` clears the I flag in PSW, making interrupts invisible to the
priority scan. If an HSO timer fires between `check_irq()` setting
`irq_requested = true` and the `fetch` executing, the scan finds no
dispatchable interrupt and `level` exits as -1. With four recurring HSO
timers (0x38–0x3B) firing at high rates, this race occurs frequently.

### Implementation

**`mcs96ops.lst`** (base MCS96 `fetch`):
```c
for(level = 7; level >= 0 && !(PSW & pending_irq & (1<<level)); level--);
if(level >= 0) {
    // ... normal interrupt dispatch ...
} else {
    irq_requested = false;
}
```

**`i8x9x.cpp`** (`fetch_196_full` for 80C196KB):
Same guard applied to the `else if(irq_requested)` branch after the
EXTINT1 check.

When `level < 0`, no interrupt is dispatched and `irq_requested` is
cleared. This prevents the CPU from re-entering the dead scan on every
subsequent fetch cycle. The flag will be set again correctly when
`check_irq()` is next called (on any write to INT_PENDING, INT_MASK, or
when a new interrupt source fires).

### Why This Is Correct

1. **Defensive, not masking a bug**: The `irq_requested` flag is an
   optimization cache of `(PSW & pending_irq) && (PSW & F_I)`. It is not
   guaranteed to be re-evaluated between every state change and the next
   fetch. The guard handles the inherent race without adding overhead to
   the common no-interrupt path.

2. **No interrupts are lost**: Clearing `irq_requested` when no interrupt
   is dispatchable does not suppress future interrupts. Any subsequent
   event that sets `pending_irq` bits or changes PSW will call
   `check_irq()`, which re-evaluates and sets `irq_requested` again if
   appropriate.

3. **Prevents undefined behavior**: `1 << -1` is undefined in C++.
   Even if a compiler happens to produce a no-op or wrapping shift, the
   subsequent vector read from `0x1FFE` is still a bug — that address
   is below the interrupt vector table and reads arbitrary memory.

4. **Affects both MCS96 base and 80C196KB**: The fix is applied in two
   places: the generated `fetch_full()` (from `mcs96ops.lst`) used by
   base MCS96 devices, and the hand-written `fetch_196_full()` in
   `i8x9x.cpp` used by 80C196KB devices like the MV-30.
