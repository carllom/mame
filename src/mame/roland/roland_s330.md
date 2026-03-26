# Roland S-330 — MAME Driver Notes

## Overview

The Roland S-330 (1988) is an 8-bit digital sampler derived from the W-30 Music
Workstation. In MAME it is implemented in `roland_s50.cpp` as `roland_s330_state`,
which inherits from `roland_w30_state`.

The driver is incomplete; synthesis is not emulated. The machine boots, the UI
is functional, and floppy loading works. See "Known Missing / TODO" below.

---

## Hardware Summary

| Component | Part | Notes |
|-----------|------|-------|
| Main CPU | Intel C8095-90 (MCS-96) | 12 MHz (24 MHz XTAL ÷ 2) |
| Wave chip | Roland SA-16 (custom) | 26.88 MHz XTAL; synthesis not emulated |
| VDP | TMS3556NL | 14.3496 MHz XTAL; MODE_MIXED |
| LCD (front panel) | HD44780 (DM1620-5BL7 / MW-5F) | 2×16 character display |
| FDC | WD1772-02 | 3.5" DD floppy (ND-362S-A) |
| Panel controller | M60013 (keysw, 4 rows) | Scanned via D806 |
| Output | BU3905 DAC | Connected to SA-16 SH callback |

---

## Address Map (`s330_mem_map`)

```
0000–1FFF  bank1_view[0]: ROM (boot)
           bank1_view[1]: banked PSRAM (LoBank 1–7)
2000–3FFF  ROM (fixed second half)
4000–7FFF  common_ram (PSRAM window)
8000–BFFF  bank2_view[1]: banked PSRAM (HiBank 0–3)
C000–FFFF  SA-16 wave chip (odd bytes, umask 0xFF00)
C200       FDC status (r) / floppy select (w)
C300–C302  HD44780 LCD (even bytes)
C600       PSRAM bank register (r/w)
C800–C806  WD1772 FDC registers
D000       TMS3556 vram_r
D002       TMS3556 initptr_r (r) / vram_w (w)   ← see note below
D004       TMS3556 reg_r (r) / reg_w (w)
D806       M60013 keysw_r (r) / keysw_w (w)
F00C       LED indicators (w)
```

### D002 mapped to `initptr_r`, not `vram_r`

The firmware performs a dummy read from D002 and immediately discards the value
(overwrites R with `#21h`). The purpose is solely to set the TMS3556 internal
`m_init_read` flag so that the first sequential read from D000 starts at
`VDP_BAMP`, not `VDP_BAMP-1`. Mapping D002 to `vram_r` (as on the S-50) causes
an off-by-one that corrupts cursor restore and LCD VRAM copies.

---

## PSRAM Bank Register (C600)

```
Bit pattern:  -AAAA-BB
  AAAA (bits 6:3) = LoBank: selects 8K overlay at 0000–1FFF
                    0 = ROM, 1–7 = banked PSRAM overlays
  BB   (bits 1:0) = HiBank: selects 16K chunk at 8000–BFFF
  bit 2 (BC pin)  = not connected
```

The W-30 shares the same register address but uses a different bit arrangement;
`roland_s330_state::psram_bank_w` overrides the base class to implement the
S-330 layout. Unlike the W-30, bank2_view[0] (ROM mirror at 8000–BFFF) is never
used — `m_bank2_view.select(1)` is always set.

---

## Boot Sequence and Controller Selection

The S-330 determines the external controller type (None / Mouse / RC-100) by
sampling which panel key is held during power-on:

| Key held | Controller |
|----------|-----------|
| Left     | None (panel keys only) |
| Down     | Mouse |
| Right    | RC-100 |

Real hardware samples the key before the CPU starts. MAME keyboard input has at
least one frame of latency, so the held key never arrives in time.

**Fix:** `roland_s330_state::keysw_r` injects the appropriate value into the
first boot scan of row 1 (before `m_boot_inject_done` is set) based on the
`CTRLTYPE` configuration DIP:

| DIP value | Injected key | Controller |
|-----------|-------------|-----------|
| 0 (default) | 0xFB (Left) | None (panel keys) |
| 1 | none (0xFF) | Mouse |
| 2 | 0xEF (Right) | RC-100 |

`m_boot_inject_done` is reset in `machine_reset()` so the injection works after
a soft reset. If the user is physically holding a key on row 1, the DIP is
ignored.

---

## Voice Loop / HSI0 Spin

The firmware's voice-processing loop at address 0x15F2 spins on:

```
jbc hsi_status, 1, 1584   ; wait for HSI0 (SA-16 ENVINT)
```

On real hardware this is released by the SA-16 wave chip asserting ENVINT at the
audio frame rate. Since SA-16 synthesis is not emulated, the spin never releases,
preventing keyscan, panel display updates, and all UI operation.

**Fix:** `roland_s330_state::vdp_timer` (called every scanline) calls the base
`vdp_timer` and then asserts `HSI0_LINE`:

```cpp
TIMER_DEVICE_CALLBACK_MEMBER(roland_s330_state::vdp_timer)
{
    roland_s50_base_state::vdp_timer(timer, param);
    m_maincpu->set_input_line(i8x9x_device::HSI0_LINE, ASSERT_LINE);
}
```

This is a stopgap; when SA-16 synthesis is implemented the `set_input_line` call
should be removed and ENVINT should be driven by the wave chip's `int_callback`.

---

## ADC Channel 7 (`analog_dac_value`)

The `find_neg_sample` routine (firmware 0x4946) polls ACH7 1022 times and
compares the result against a threshold that alternates by iteration phase
register R32:

- R32 == 1: needs ADC value ≤ 0x1FF
- R32 != 1: needs ADC value ≥ 0x204

Values 0x200–0x203 (ADC_MSB 0x80) fail both tests and cause an infinite stall.

**Fix:** `analog_dac_value` reads R32 from CPU data space and returns 0x1FF or
0x204 accordingly. This is a placeholder; the real value should come from the
SA-16 DAC output once synthesis is emulated.

---

## VDP (TMS3556NL)

### Register configuration (from firmware log)

| Register | Value | Function |
|----------|-------|---------|
| CM1 (reg 4) | 0x50 | display control |
| CM2 (reg 5) | 0xC2 | display control |
| CM3 (reg 6) | 0xC8 | bits 7:6 = 0b11 → MODE_MIXED |
| CM4 (reg 7) | 0x00 | bits 7:5 = 0 → global bg_color = black at vblank |

The VDP operates in MODE_MIXED: each character row is either a text row or a
bitmap row, selected by the CG flag byte at the start of the name table row.

### Palette

The TMS3556 attribute byte encodes color as **bit0=R, bit1=G, bit2=B**.
The standard `RGB_3BIT` macro is bit2=R, bit0=B — wrong for this hardware.

**Fix:** `init_vdp_palette` builds an explicit 8-color table using `pal1bit()`:
```cpp
palette.set_pen_color(i, pal1bit(i>>0), pal1bit(i>>1), pal1bit(i>>2));
```
`pal1bit()` returns 0 or 255, ensuring index 0 is true black (not dark gray as
RGB_3BIT would give).

### Delimiter Characters (TMS3556 zone attributes)

Changes to `tms3556.cpp` (affects all users of TMS3556, not just S-330):

The S-330 firmware uses **delimiter characters** (character code 0x20 with the
UNL top bit clear, i.e. `name_lo & 0x7F == 0x20`) to set per-zone background
colors on a text row. The attribute byte format is:

```
name_hi = | BF | GF | RF | MSK | INC | BB | GB | RB |
             7    6    5    4     3    2    1    0
```

- bits 7:5 (BF:GF:RF) — foreground color of the delimiter cell itself
- bit 4 (INC) — incrustation (not yet implemented)
- bit 3 (MSK) — masking flag for this zone
- bits 2:0 (BB:GB:RB) — new zone background color applied from this cell onward

The new background color persists for all subsequent characters in the row until
the next delimiter or end of row. At the end of each character row the background
color is restored to the value defined by CM4 bits 7:5.

**Masking:** If CM2 bit 3 is set (masking globally active), and both the previous
zone and the current delimiter have their MSK bit set, the delimiter cell itself
is rendered in the *previous* zone's background color rather than the new zone
color. This allows seamless zone boundaries. The new `m_zone_msk` member in
`tms3556_device` tracks whether the previous zone had MSK set.

---

## LCD (HD44780)

- Model: DM1620-5BL7 (2 lines × 16 characters, branded MW-5F)
- Clock: 270,000 Hz (datasheet typical; not measured from hardware)
- Mapped at C300–C302 (even bytes)
- Pixel layout: 5×8 dot matrix in a 6×9 cell grid, 2 rows → bitmap 96×18
- Palette: index 0 = light gray (131,136,139), index 1 = dark (92,83,88)

---

## Known Missing / TODO

- **SA-16 synthesis**: The wave chip plays no sound. When implemented, remove
  the `HSI0_LINE ASSERT_LINE` workaround from `vdp_timer` and connect ENVINT
  properly, and replace `analog_dac_value` with the real DAC output.
- **ACH4 (volume dial)**: `analog_vol_ctrl` returns a fixed 0x1FF; needs a real
  dial input.
- **C400 / C500 (Ext port)**: I/O direction and data pins not mapped.
- **TMS3556 INC (incrustation)**: Delimiter bit 4 not implemented.
- **FDC C200 status bits**: Higher bits not fully verified.
- **Machine config**: The `s330(machine_config&)` function is a provisional
  implementation based on the W-30 config. A proper standalone config with
  verified clocks and I/O hookups is needed.
- **ROM dump**: Only system version 1.03 (`S33s103.img`) has been tested.
