# E-mu E6400 — EOS Firmware Developer Guide

Firmware analysis notes for the EOS (Emulator Operating System) running on the E-mu E6400 Ultra sampler. All firmware references are from **eos30b.raw** (EOS v3.00b) unless otherwise noted. For hardware register maps, memory layout, and board-level details, see [e6400_hardware.md](e6400_hardware.md).

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

### Firmware Update Disk Format

Firmware updates are distributed as raw 1.44 MB (1,474,560 byte) floppy disk images. The flash image is written starting at **sector 0** (byte offset 0) of the disk — there is no filesystem; the floppy is a raw sector dump. The flash image (header + code) occupies the first N sectors, and the remainder of the disk is filled with padding.

**Disk layout:**

| Offset | Size | Content |
|---|---|---|
| 0x000000 | 0x400 | Flash header (magic, version, sector count, checksum — see [Flash Header](e6400_hardware.md#flash-header-cpu-0x010000-0x0103ff)) |
| 0x000400 | sector_count × 512 | Firmware code (vector table at +0, code follows) |
| header + code | remainder | Padding to end of 1.44 MB disk |

The padding byte varies by era:
- **EOS ≤ 3.x** (v2.80f, v3.00b): `0x55` — matches erased flash state
- **EOS ≥ 4.x** (v4.10a, v4.62): `0xF6` — standard floppy format fill byte

**Firmware reads the disk via `sub_2E1E8`** ("Loading System from Floppy" / "Saving System to Floppy"):
1. Reads flash base address: `move.l #$10000,d2`
2. Reads sector count from flash header: `move.l ($1001C).l,d1`
3. Adds 2 for the header sectors: `addq.l #2,d1`
4. Transfers `d1` sectors between floppy and flash via the FDC

**Extracting ROM images from update disks:** The `.raw` ROM files used by the MAME driver are the flash image portion only (header + code), extracted from offset 0 of the floppy disk image. Size = `0x400 + sector_count × 512`, where sector_count is the big-endian 32-bit value at floppy offset `0x1C`.

---

## MIDI Note Processing

### MIDI Event Queue

The firmware uses a **1024-byte circular buffer** at `$F3FE70` for internal MIDI events:

| Offset | Size | Function |
|--------|------|----------|
| +$00 | 32-bit | Queue item count |
| +$02 | 16-bit | Size threshold (compared to 3 in dequeue guard) |
| +$04 | 8-bit | Overflow flag (set to 1 when write catches read) |
| +$06 | 16-bit | Write index (masked with 0x3FF for 1024-entry wrap) |
| +$08 | 16-bit | Read index (masked with 0x3FF) |
| +$0A | 1024 bytes | Circular data buffer |

**Event format:** 4 bytes per event: `[channel, velocity, note, terminator]`. Terminator non-zero = note-on, zero = note-off.

**Enqueue functions:**
- `sub_7E4FE` — Note-on: queues `(channel, 0, note, 0)` [terminator=0 is overwritten]
- `sub_7E446` — Note-off: queues `(channel, 0x7F, note, 0)`

**Dequeue function:** `sub_7E5CC` — registered as a timer callback (type 3) via `sub_7EE6C` during synth init. Fires periodically, checks queue depth ≥ 4, then:
1. Dequeues 4 bytes: channel (d3), velocity (d2), note (d1), terminator
2. **Terminator non-zero** → calls `sub_7E68C` (note-on handler)
3. **Terminator zero** → calls `sub_7EA2A` (note-off handler)

### Note-On Processing Chain

```
sub_7E68C   (Note-on handler)
  ├─ Velocity curve lookup: (current_VelCurve)[raw_vel] → synth velocity
  ├─ Optional callback via function pointer at $F033E0
  ├─ Store channel/velocity/note in synth registers ($F34026–$F3402C)
  └─ sub_7EA42 → sub_8185C (voice dispatcher)
       ├─ Checks voice bitmap at $F34056
       ├─ sub_816E8 (voice configuration)
       │    ├─ sub_806B6 (pitch calculation)
       │    ├─ sub_81CE0 (sample/loop setup)
       │    └─ sub_822C6 → sub_828CA (master voice init)
       └─ Returns allocated voice ID
```

### Master Voice Initialization — `sub_828CA`

Called with: voice context pointer (a5), voice index (d3), secondary index (d1).

1. **Voice descriptor lookup:** `dword_86906[voice_N]` + $F41540 → voice descriptor base. Each descriptor is **0x374 bytes** (884 decimal). The table `dword_86906` contains pre-computed offsets for 128 voices.

2. **State machine install:** Stores handler function pointers:
   - +$1E: `sub_8434A` (voice tick handler)
   - +$22: `sub_84B12` (voice release handler)
   - +$26: `sub_84B2C` (voice stop handler)

3. **Parameter setup:** Calls multiple initialization subroutines:
   - `sub_8253C` — Pitch configuration: base pitch + transpose + sample tuning + key follow. Stores at voice register block offset +$5A. Also configures pitch envelope initial values and rates.
   - `sub_82F56` — Sample/zone configuration.
   - `sub_826A6` — Filter coefficient init from preset data (copies 13 longwords of envelope/filter data).
   - `sub_82ED2` — Amplitude envelope init.
   - `sub_83E88` — LFO / auxiliary parameters.

4. **Filter envelope init** at voice register block +$1DE: Clears 7 longwords + 1 word, sets +$0E to 0x1000.

5. **Start playback:** Calls `sub_86C60` with voice index → adds voice to timer-driven execution linked list. The voice state machine (`sub_8434A`) will then be called on each tick.

### Voice State Machine — `sub_8434A`

This is the per-voice "tick" function, invoked periodically (~every 10 timer units). It processes all voice modulation and triggers hardware writes:

1. **Modulation loop:** Iterates 18 modulation sources (at descriptor offset +$254, 16-byte stride). Each source has: source pointer, destination pointer, amount, enable flag, and accumulator. Computes `source_value × amount >> 12`, applies delta to destination.

2. **Envelope generators:** Calls `loc_84022` three times for envelope segments at descriptor offsets +$13C, +$172, +$1A8 (likely: amplitude, filter, auxiliary envelopes).

3. **Volume / randomization** at offset +$1DE: Applies volume envelope with clamping (0–127), lookup via table `word_8145E`. Uses a PRNG (`dword_F0345A`, LCG with multiplier 0x72D138B5) for random modulation.

4. **Pitch processing** at offset +$B0: Velocity-to-pitch mapping via `word_8125E` table, pitch envelope computation with mode-dependent filtering ($2D flag selects direct vs. blended mode).

5. **Key follow / crossfade** at offset +$A0: Amplitude scaling based on note number with breakpoint and slope.

6. **Hardware update calls:**
   - `sub_8328C` — Computes final pitch and stereo pan, calls `sub_22EC0` to write G-chip oscillator registers.
   - `sub_8341C` — Filter processing, calls filter coefficient generator `sub_7BE20`, may write to H-chip filter IC.
   - `sub_8393C` — Unknown (possibly output routing or DMA control).

7. **Reschedule:** Sets next tick time = current `timer_value` + 10, stores at descriptor +$2A.

### Audition Key

The Audition button (SC2/SI4 in E-IV key matrix) generates **event type 0x16** through the module dispatch system, not directly through the keyboard key code handler.

**Call flow:**
```
Event 0x16 dispatched
  → sub_44072 (Module 0xA handler)
    → sub_440CC → sub_2BC30 (loads handler from $F00B68)
      → sub_2BBB8 (Audition key handler)
        ├─ sub_2BB98: enqueue note-on (note=_auditionkey, vel=0, channel)
        │    └─ sub_7E4FE: writes 4 bytes to MIDI queue at $F3FE70
        └─ sub_7E446: enqueue note-off (note=_auditionkey, vel=0x7F, channel)
             └─ writes 4 bytes to MIDI queue
```

**Audition note:** Stored at `_auditionkey` ($F00B62), default value **0x27** (MIDI note 39, D♯2). Configurable in Master settings as `v007_AuditionKey` (range 0–127, parameter ID 7).

The audition key bypasses keyboard transpose and zone mapping — it directly injects MIDI note-on/note-off events at a fixed note number and velocity (on=0, off=0x7F). The channel is determined by the MIDI mode setting: mode 2 uses the current MIDI channel from `sub_A0224`, otherwise channel 0.

---

## UI Widget Class Hierarchy (Firmware vtable system)

The EOS firmware implements a C++ single-inheritance UI widget class hierarchy using virtual method tables (vtables). Each class has a `vtbl_<class>` header (8 bytes: reserved dword + method count) followed by an array of `{this_adjust, func_ptr}` pairs. Objects store their vtable pointer at offset +0x08. All `this_adjust` values are zero (simple single inheritance, no thunking).

47 classes identified, ~1700+ total virtual method entries. Constructors chain upward (call parent, then overwrite vtable); destructors chain downward.

### Virtual method slot map (ui_widget base, 26 slots)

Slot indices marked with `(event N)` are dispatched through `widget_dispatch_event` (slot 14), which switches on event type 0–10.

| Slot | Method | Description |
|------|--------|-------------|
| 0 | `__dtor` | Virtual destructor |
| 1 | `widget_get_parent` | Returns parent widget ptr (+0x0C) |
| 2 | `widget_set_parent` | Sets parent widget ptr (+0x0C) |
| 3 | `widget_get_rect` | Returns bounding rect (+0x18/+0x1C) |
| 4 | `widget_set_rect` | Sets bounding rect |
| 5 | `widget_get_canvas` | Returns canvas/gfx context (+0x10) |
| 6 | `widget_get_display` | Returns display handle (+0x14) |
| 7 | `widget_set_tag` | Sets userdata dword (+0x20) |
| 8 | `widget_get_tag` | Returns userdata dword (+0x20) |
| 9 | `widget_show` / `page_activate` | Show widget (sets +0x24=1, calls draw) |
| 10 | `widget_hide` / `page_deactivate` | Hide widget (sets +0x24=0) |
| 11 | `widget_is_visible` | Returns visibility flag (+0x24) |
| 12 | `__draw` | Draw widget content |
| 13 | `widget_erase` | Erase widget area |
| 14 | `widget_dispatch_event` / `page_handle_event` | Event dispatch (switch on type) |
| 15 | `on_cursor` (event 0) | Arrow key navigation. param[6]: 0=left, 1=up, 2=down, 3=right |
| 16 | `on_select` (event 5) | Confirm/enter. Routed through overlay system |
| 17 | `on_encoder` (event 1) | Data wheel. param[4]=signed delta |
| 18 | (event 2) | Unused — never overridden |
| 19 | `on_key` (event 3) | Key press. param[6]=keycode, param[7]=state |
| 20 | (event 4) | Rarely used (field edit trigger) |
| 21 | `on_inc_dec` (event 6) | Inc/dec. param[6]: 0=prev, 1=next |
| 22–23 | (events 7–8) | Unused — never overridden |
| 24 | `on_notify` (event 9) | Timer/callback notification |
| 25 | `on_softkey` (event 10) | Function key. param[6]=key_id |

### Page-specific slots (ui_page adds slots 26–38)

| Slot | Method | Description |
|------|--------|-------------|
| 26 | `page_set_overlay` | Set overlay child widget (+0x38) |
| 27 | `page_get_overlay` | Get overlay child widget |
| 28 | `page_get_child_list` | Get linked list of children (+0x30) |
| 29 | `page_add_child` | Add child to page |
| 30 | `page_remove_child` | Remove child from page |
| 31 | `page_set_focus` | Set focused child (+0x34), notifies old/new |
| 32 | `page_get_focus` | Get focused child |
| 33 | `page_get_mode` | Get page mode byte (+0x2E) |
| 34 | `page_draw_label` | Draw vertical title text from +0x26 string |
| 35 | `page_fill_rect` | Fill page bounding rect |
| 36 | `page_draw_border` | Draw border lines around page rect |
| 37 | `page_draw_children` | Draw all visible children |
| 38 | `page_find_next_focus` | Navigate focus: find nearest widget in direction (0–3) |

### Field-specific slots (ui_value_widget adds slots 26–33)

| Slot | Method | Description |
|------|--------|-------------|
| 26 | `field_get_data_ptr` | Returns data pointer (+0x26) |
| 27 | `field_get_format` | Returns format info (+0x36) |
| 28 | `field_set_enabled` | Set enabled flag (+0x3A), affects visibility |
| 29 | `field_is_enabled` | Returns enabled flag |
| 30 | `field_on_value_changed` | Value change callback (base no-op) |
| 31 | `field_draw_label` | Draw label text from +0x2E, right-aligned |
| 32 | `field_erase_value` | Erase value display area |
| 33 | `field_erase_label` | Erase label text area |

### Event routing

`page_handle_event` (slot 14 for page classes) routes events at the page level:
- Event 0 (cursor): → self.`on_cursor` (slot 15)
- Event 5 (select): → overlay widget if present, else self.`on_select` (slot 16)
- Event 6 (inc/dec): → self.`on_inc_dec` (slot 21)
- Event 10 (softkey): → self.`on_softkey` (slot 25)
- All other events: → focused child's `dispatch_event` (slot 14)

### Class hierarchy

```
ui_object (0xE5C56, 1 slot)
├── ui_component (0xEA312, 5 slots)
│   ├── edit_state (0xD1672, 10 slots)
│   └── ui_event_handler (0xE430E, 5 slots)
├── ui_widget (0xE5AC6, 26 slots)
│   ├── ui_page (0xE6510, 39 slots)
│   │   ├── ui_list_page (0xE8420, 45 slots)
│   │   │   ├── preset_list_page (0xC56E2, 48 slots) — "P%03d"
│   │   │   ├── sample_list_page (0xD86B8, 47 slots) — "S%03d"
│   │   │   └── sequence_list_page (0xDABE0, 48 slots) — "s%03d"
│   │   ├── ui_grid_page (0xE77E6, 43 slots)
│   │   │   └── folder_browser_page (0xC2474, 44 slots) — "FLDR"
│   │   ├── ui_sub_page (0xDE0EE, 56 slots)
│   │   │   ├── ui_field_page (0xE0644, 65 slots)
│   │   │   │   └── ui_multi_field_page (0xE3C26, 73 slots)
│   │   │   │       └── ui_param_editor (0xD7D06, 74 slots)
│   │   │   └── ui_param_sub_page (0xDE806, 57 slots)
│   │   ├── ui_dialog (0xEA938, 39 slots)
│   │   │   └── ui_confirm_dialog (0xEAF50, 39 slots) — Yes/No/Cancel
│   │   ├── goto_page (0xC701C, 39 slots) — "Go to..." navigation
│   │   ├── ui_percent_bar (0xE959C, 47 slots) — "%ld%%"
│   │   ├── ui_value_page (0xE2CF0, 42 slots)
│   │   ├── about_page (0xD529A, 39 slots) — "Emulator Operating System"
│   │   ├── memstats_page (0xDB41A, 39 slots) — "Memory Statistics"
│   │   └── dest_selector_page (0xD27BC, 39 slots) — "Select destination..."
│   ├── ui_child (0xE3FF8, 28 slots)
│   │   ├── ui_envelope_widget (0xD9D84, 28 slots)
│   │   ├── ui_module_widget (0xDBCA2, 35 slots)
│   │   ├── ui_slider (0xDFB56, 29 slots) — "100%"
│   │   ├── ui_scroll_area (0xE005C, 30 slots)
│   │   ├── ui_key_map (0xE23CE, 28 slots)
│   │   └── ui_draw_area (0xE27EC, 28 slots)
│   ├── ui_value_widget (0xEA0F8, 34 slots)
│   │   ├── ui_numeric_field (0xE9B8E, 39 slots) — "%ld"
│   │   │   ├── ui_labeled_field (0xE1BA0, 46 slots) — "%s:"
│   │   │   ├── ui_data_field (0xD20D6, 39 slots)
│   │   │   │   └── goto_field (0xC6EDC, 39 slots) — "Go to drive/folder/sample..."
│   │   │   └── ui_linked_field (0xE3AAE, 46 slots) — "%s:"
│   │   └── ui_column_field (0xE55DA, 43 slots)
│   ├── ui_tab_bar (0xE8934, 32 slots)
│   │   ├── ui_page_tabs (0xDF212, 33 slots)
│   │   │   └── ui_font_tabs (0xDF68E, 33 slots)
│   │   ├── ui_soft_keys (0xDF0FA, 34 slots)
│   │   ├── ui_tab_item_a (0xE4610, 35 slots)
│   │   └── ui_tab_item_b (0xE4A30, 36 slots)
│   └── ui_button (0xEB8E0, 29 slots)
└── ui_list_node (0xEB52A, 17 slots)
```

### Object layout

All objects using this vtable system share a common header:

| Offset | Size | Field |
|--------|------|-------|
| +0x00 | 4 | (reserved / allocation metadata) |
| +0x04 | 4 | (reserved) |
| +0x08 | 4 | vtable pointer → `vtbl_<class>` |
| +0x0C | ... | class-specific fields |

### Virtual dispatch pattern

```c
vtable = *(obj + 8);
this_adj = *(short *)(vtable + 8 + N*8);      // always 0
func = *(ptr *)(vtable + 8 + N*8 + 4);
func(this_adj + obj, ...);                     // virtual call to slot N
```

### Vtable data layout

```
vtbl_<class>:   apientry_head { dword 0, dword num_methods }
                apientry[0]   { dword this_adjust, dword pFunc }
                apientry[1]   { dword this_adjust, dword pFunc }
                ...
```
