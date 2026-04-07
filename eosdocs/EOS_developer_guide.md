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

## Voice Structure

The **voice** is the fundamental sound-producing unit in EOS presets. Each preset contains a variable-length linked list of voice structs, each describing a single layer of the sound: key/velocity ranges, tuning, filter, envelopes, LFOs, modulation routing (cords), and sample zone assignment. Voices are NOT stored in arrays — new voices are appended inline and the list is traversed using the `length` field to step from one voice to the next.

**Total size:** 306 bytes (0x132) per voice.

### Voice struct layout

| Offset | Size | Type | Name | Description |
|--------|------|------|------|-------------|
| 0 | 2 | short | length | Total size of this voice entry in bytes. Used to walk the linked list: next voice = (byte*)this + length. |
| 2 | 1 | byte | number | Voice number within preset (1-based). |
| 3 | 1 | byte | group | Voice group assignment. |
| 4–11 | 8 | — | *(padding)* | Reserved / unused. Cleared by `init_voice`. |
| 12 | 4 | range | key | Key range (low/fadeLow/fadeHigh/high). |
| 16 | 4 | range | velocity | Velocity range. |
| 20 | 4 | range | realtime | Realtime controller range. |
| 24 | 1 | byte | bVoiceType | Voice type (mono/poly/etc). Initialized by `init_voice_link`. |
| 25 | 1 | byte | bAssignGroup | Assign group for voice stealing. |
| 26 | 2 | short | delay | Voice delay time (ms). |
| 28 | 2 | short | linkedPreset | Linked preset number. |
| 30 | 2 | short | nStartOffset | Sample start offset. 16-bit value gives the offset within the sample to begin playback. |
| 32 | 1 | byte | bKeyTranspose | Key transpose (semitones, signed). |
| 33 | 1 | byte | bCoarseTune | Coarse tune (semitones, signed). |
| 34 | 1 | byte | bFineTune | Fine tune (cents, signed). |
| 35 | 1 | byte | bGlideRate | Glide/portamento rate. |
| 36 | 1 | byte | bNonTranspose | Non-transpose flag (1=sample pitch ignores key number). |
| 37 | 1 | byte | bSoloMode | Solo mode enable. |
| 38 | 1 | byte | bArpEnabled | Arpeggiator enable. |
| 39 | 1 | byte | bChorusStereoWidth | Chorus stereo width. |
| 40 | 1 | byte | bChorusAmount | Chorus wet/dry amount. |
| 41 | 1 | — | *(padding)* | Unused. |
| 42 | 1 | byte | bChorusInitialITD | Chorus initial inter-aural time delay. |
| 43–47 | 5 | — | *(padding)* | Unused. |
| 48 | 1 | byte | bLatchMode | Latch mode. |
| 49 | 1 | byte | bLatchChannels | Latch channels. |
| 50 | 1 | byte | bTriggerMode | Trigger mode (poly normally / poly release). |
| 51 | 1 | byte | bGlideCurve | Glide curve type. |
| 52 | 1 | byte | bVolume | Voice volume (0–127). |
| 53 | 1 | byte | bPan | Pan position. |
| 54 | 1 | byte | bSubmix | Submix bus assignment. Also addressed by "Initial Controller D" overlay (ParamID 0x5EF). |
| 55 | 1 | byte | bAmpEnvDepth | Amplitude envelope depth. |
| 56 | 1 | byte | bFilterType | Filter type selector. |
| 57 | 1 | byte | bInitialCtrlD | Initial controller D value. Shares offset range with submix via controller overlay mechanism (ParamIDs 0x5EC–0x5EF). |
| 58 | 1 | byte | vcfCutoff | Filter cutoff frequency. |
| 59 | 1 | byte | vcfQ | Filter resonance (Q). |
| 60 | 1 | byte | bFilterParam0 | Filter parameter 0 (morph). |
| 61 | 1 | byte | bFilterParam1 | Filter parameter 1. |
| 62 | 1 | byte | bFilterParam2 | Filter parameter 2. |
| 63 | 1 | byte | bFilterParam3 | Filter parameter 3. |
| 64 | 1 | byte | bFilterParam4 | Filter parameter 4. |
| 65 | 1 | byte | bFilterParam5 | Filter parameter 5. |
| 66 | 1 | byte | bFilterParam6 | Filter parameter 6. |
| 67 | 1 | byte | bFilterParam7 | Filter parameter 7. |
| 68–107 | 40 | — | *(internal)* | Runtime processing coefficients. Not exposed to the UI parameter system. No ROM or RAM descriptors exist for these offsets. Likely stores intermediate computed filter/tuning state. |
| 108 | 14 | envelope | amp_env | Amplitude envelope (6-stage: Atk1→Atk2→Dcy1→Dcy2→Rls1→Rls2). |
| 122 | 14 | envelope | filter_env | Filter envelope. |
| 136 | 14 | envelope | aux_env | Auxiliary envelope. |
| 150 | 6 | lfo | lfo1 | LFO 1. |
| 156–187 | 32 | — | *(lfo2 + padding)* | LFO 2 area. Byte 0 initialized to 1 (type), followed by LFO2 rate/shape/delay/variation/sync/lag parameters. Partially mapped by ROM descriptors. Remaining bytes are padding to align the cord array. |
| 188 | 96 | cord[24] | cords | Modulation routing: 24 patch cords (source→destination with signed amount). |
| 284 | 22 | sample_zone | sampleZone | Sample zone assignment (key/velocity ranges, sample number, original key). |

### Sub-structs

#### range (4 bytes)

Key/velocity/controller range with crossfade support.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0 | byte | low | Range lower bound. |
| 1 | byte | fadeLow | Crossfade-in width (from low). |
| 2 | byte | fadeHigh | Crossfade-out width (from high). |
| 3 | byte | high | Range upper bound. |

#### envelope (14 bytes)

Six-stage envelope generator: Attack 1 → Attack 2 → Decay 1 → Decay 2 → Release 1 → Release 2. Each stage has a rate and a target level.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0 | byte | atk1rate | Attack 1 rate. |
| 1 | byte | atk1level | Attack 1 target level. |
| 2 | byte | atk2rate | Attack 2 rate. |
| 3 | byte | atk2level | Attack 2 target level. |
| 4 | byte | dcy1rate | Decay 1 rate. |
| 5 | byte | dcy1level | Decay 1 target level (sustain level when dcy2rate=0). |
| 6 | byte | dcy2rate | Decay 2 rate. |
| 7 | byte | dcy2level | Decay 2 target level. |
| 8 | byte | rls1rate | Release 1 rate. |
| 9 | byte | rls1level | Release 1 target level. |
| 10 | byte | rls2rate | Release 2 rate. |
| 11 | byte | rls2level | Release 2 target level (normally 0). |

*Note: offsets 12–13 exist in the struct (14 bytes total) but are unnamed — likely padding.*

#### lfo (6 bytes)

Low-frequency oscillator parameters.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0 | byte | rate | LFO rate/frequency. |
| 1 | byte | shape | Waveform shape (sine, triangle, square, etc). |
| 2 | byte | delay | Onset delay time. |
| 3 | byte | variation | Random variation amount. |
| 4 | byte | sync | Sync mode (free-run, key sync, etc). |
| 5 | byte | — | Padding. |

#### cord (4 bytes)

Single modulation patch cord routing.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0 | byte | source | Modulation source ID. |
| 1 | byte | destination | Modulation destination ID. |
| 2 | byte | amount | Signed modulation depth. |
| 3 | byte | — | Padding. |

EOS provides 24 cords per voice, stored contiguously at offset 188 (96 bytes total).

#### sample_zone (22 bytes)

Per-zone sample assignment within a voice. One voice typically has one sample zone, though the variable-length voice mechanism could support more.

| Offset | Type | Name | Description |
|--------|------|------|-------------|
| 0 | 4 | range | keyRange | Key range for this zone. |
| 4 | 4 | range | velRange | Velocity range for this zone. |
| 8 | 2 | short | samplenumber | Sample number (index into preset's sample table). |
| 10 | 2 | short | *(unknown)* | Unknown (possibly a secondary sample ref or flags). |
| 12 | 1 | byte | originalKey | Original key (root note for pitch calculation). |
| 13–21 | 9 | — | *(unknown)* | Remaining fields not yet identified. |

### Voice list traversal

Voices within a preset form a contiguous linked list. There is no pointer chain — instead, each voice's `length` field gives the byte stride to the next entry:

```
voice *get_next_voice(voice *v) {
    return (voice *)((byte *)v + v->length);
}
```

Iteration counts are maintained separately via `get_num_voices()`. Helper functions:

| Function | Address | Description |
|----------|---------|-------------|
| `init_voice` | 0x07019A | Zero-fill and initialize all fields/sub-structs to defaults. |
| `get_voice` | 0x06FA40 | Get pointer to first voice in a preset. |
| `get_next_voice` | 0x06FA62 | Step to next voice using length field. |
| `get_num_voices` | 0x06FA72 | Get voice count for a preset. |
| `sizeof_voice` | 0x06FA88 | Return sizeof(voice) — 306. |
| `get_voice_length` | 0x06FA96 | Read a specific voice's length field. |
| `get_voice_smpl` | 0x06FAAC | Get pointer to sample_zone within a voice. |
| `get_voice_number` | 0x06FAD4 | Get voice number field. |
| `get_voice_entry` | 0x06FB04 | Get Nth voice by iterating the list. |
| `get_voice_param` | 0x06FDF4 | Read a parameter from a voice by descriptor ID (ROM param table). |
| `init_voice_link` | 0x0703E4 | Initialize voiceType/assignGroup/delay/linkedPreset fields. |
| `init_voice_filter` | 0x070476 | Initialize filter-related fields (cutoff, Q, type, params). |
| `init_voice_tuning` | 0x0704AA | Initialize tuning fields (coarseTune, fineTune, transpose, glide). |
| `voice_init_master` | 0x0828CA | Master voice init (descriptor setup, state machine, start playback). |
| `voice_start_playback` | 0x086C60 | Add voice to timer-driven execution list. |
| `voice_tick` | 0x08434A | Voice state machine tick (modulation, envelopes → hardware writes). |

### Parameter descriptor table

Voice fields are accessed by the UI and MIDI systems through a two-tier parameter descriptor table:

**ROM descriptors** (0x072BCE–0x073B86): Static voice parameters. Each 12-byte entry maps a parameter ID to a voice struct byte offset, with min/max/default values and display formatting flags. Covers most fields from offset 24 through 67, plus envelope/LFO/cord parameters.

**RAM descriptors** (eOS_api:F024xx–F02Fxx): Dynamic/computed parameters including filter response, range settings, cord routing, and submix assignment. These are evaluated at runtime and may involve indirect reads through helper functions rather than simple struct offsets.

The parameter ID namespace is partitioned:
- 0x000–0x5FF: ROM-table voice parameters (direct struct field access).
- 0x600+: RAM-table parameters (indirect access via handler functions).

---

## H-Chip (IC413) — Digital Filter Subsystem

The **H-chip** (IC413) is E-mu's proprietary digital filter IC, responsible for per-voice filtering (lowpass, highpass, bandpass, etc.), volume/pan attenuation, and output mixing. The H-chip sits between the G-chip sample oscillators and the DAC outputs on the polyphony board (AP503).

### Physical topology

A fully-populated EOS system (128-channel card) has **4 H-chips** organized as:

| H-chip | Data port offset | Assignment | Channels |
|--------|-----------------|------------|----------|
| 0 | +$28 | G-chip 1, even channels | 0, 2, 4, ... 62 |
| 1 | +$2A | G-chip 1, odd channels | 1, 3, 5, ... 63 |
| 2 | +$38 | G-chip 2, even channels | 64, 66, 68, ... 126 |
| 3 | +$3A | G-chip 2, odd channels | 65, 67, 69, ... 127 |

Each H-chip handles up to 32 filter channels. A 64-channel system (single G-chip) uses only H-chips 0 and 1.

### Bus architecture

The H-chip is **not** directly connected to the CPU bus at CSHCHIP (0x460000). Instead, the CPU accesses the H-chip indirectly through a **DSP bus** that sits on the polyphony board:

| Bus | CPU address | Usage |
|-----|------------|-------|
| DSP_BUS (primary) | 0x520000 | Default H-chip bus (word[32768]) |
| DSP_BUS (alternate) | 0x530000 | Used when `hw_variant_code == 4` (E6400 variant) |
| EXP_BUS | 0x540000 | Expansion polyphony board H-chip access (word[8192]) |

**Bus probe sequence** (from `hchip_init` at 0x0636C2):
1. Assert H-chip reset: clear CTRLREG1 bit 7 (`FUN_063AB4`)
2. Enable H-chip clock: set CTRLREG1 bit 10 (`FUN_063EE2`)
3. Delay 1ms for clocks to stabilize
4. Release H-chip from reset: clear CTRLREG1 bit 10 (`FUN_063F06`)
5. Write `DSP_BUS[0x3E] = 1` (bus enable)
6. Set `hchip_bus_base = DSP_BUS` (0x520000)
7. Set `hchip_chan_sel_ptr = DSP_BUS + 0x0E` (channel select register at 0x52000E)
8. Compute 4 data port pointers via `hchip_init_data_ptrs`
9. Write config registers: `$181D=0x88`, `$181E=0x10`, `$181F=6`
10. Read back `$181E` — if result is `0x50`, H-chip is present
11. If primary bus fails and `hw_variant_code == 4`, retry at 0x530000

**CTRLREG1** (0x400000) bit assignments for H-chip:

| Bit | Function | Set = | Clear = |
|-----|----------|-------|---------|
| 7 | H-chip / G-chip reset | Normal operation | Assert reset |
| 10 | H-chip clock enable | Clock running | Clock stopped |

### Register addressing — packed format

All H-chip register accesses use a **packed 16-bit address** encoding:

```
  15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
 [  —  |     register offset >> 1    |  —   —   —  |   channel   ]
       bits[14:8]                                    bits[4:0]
```

- **bits[4:0]**: Channel number (0–31) within the selected H-chip. Written to `hchip_chan_sel_ptr` (DSP_BUS + $0E).
- **bits[14:8]**: Register offset divided by 2. After shifting: `bus_offset = (packed >> 8) & 0x7E`. The actual bus write goes to `hchip_bus_base + bus_offset`.

**Example**: Packed address `0x5800` with channel 5 → `0x5805`:
- Channel = 5 (bits[4:0])
- Register offset = (0x58 >> 8) & 0x7E = 0x58 → bus address = `hchip_bus_base + 0x58`

The `hchip_write_reg16` and `hchip_write_reg32` functions (at 0x063E8A and 0x063EB6) implement this encoding:
```c
void hchip_write_reg16(uint16_t packed_addr, uint16_t value) {
    *hchip_chan_sel_ptr = packed_addr & 0x1F;           // select channel
    *(hchip_bus_base + ((packed_addr >> 8) & 0x7E)) = value;  // write register
}
```

### H-chip global registers

These registers use channel values in the "global" range (channel 0x18–0x1F for config, 0x14–0x17 for control). Packed address format applies.

| Packed addr | Bus offset | R/W | Init value | Description |
|-------------|-----------|-----|------------|-------------|
| $181D | +$18 ch29 | W | 0x88 | Config register A — filter mode / clock config |
| $181E | +$18 ch30 | R/W | 0x10 (write), reads 0x50 | Config register B — presence detection. Write 0x10, readback 0x50 = H-chip present |
| $181F | +$18 ch31 | W | 6 | Config register C — interpolation / oversampling config |
| $1800–$1813 | +$18 ch0–19 | W | 0 | Control registers (20 entries). Zeroed during `hchip_load_config`. |
| $1814–$1817 | +$18 ch20–23 | W | 0 | 32-bit control registers (zeroed during init) |

### H-chip per-channel registers (init values)

These are written for all 32 channels (0–31) during `hchip_init`. The packed address ORs the register code with the channel number.

**16-bit registers** (written via `hchip_write_reg16`):

| Packed base | Bus offset | Init value | Probable function |
|-------------|-----------|------------|-------------------|
| $5800 | +$58 | 0x0080 | Channel enable / voice active flag |
| $6A00 | +$6A | 0x0000 | Unknown (cleared) |
| $7800 | +$78 | 0x0000 | Unknown (cleared) |
| $0C00 | +$0C | 0x0000 | Filter coefficient A |
| $1C00 | +$1C | 0xFF00 | Filter coefficient B (max = bypass?) |
| $2C00 | +$2C | 0x0000 | Filter coefficient C |
| $3C00 | +$3C | 0x0000 | Filter coefficient D |
| $4C00 | +$4C | 0x0000 | Output gain / volume |
| $5C00 | +$5C | 0x0000 | Unknown (cleared) |
| $6C00 | +$6C | 0x0000 | Unknown (cleared) |
| $7A00 | +$7A | 0x0000 | Unknown (cleared) |
| $5A00 | +$5A | 0x0000 | Unknown (cleared) |
| $4A00 | +$4A | 0x0000 | Unknown (cleared) |
| $4800 | +$48 | 0x0000 | Unknown (cleared) |
| $6800 | +$68 | 0x0000 | Unknown (cleared) |

**32-bit registers** (written via `hchip_write_reg32`, zeroed first then selectively patterned):

| Packed base | Bus offset | Init value | Notes |
|-------------|-----------|------------|-------|
| $1400 | +$14 | 0x00000000 | Cleared |
| $3400 | +$34 | 0x00000000 | Then: every 4th channel starting at 0 → 0x8000FFFF; every 4th starting at 2 → 0x2000FFFF |
| $6400 | +$64 | 0x00000000 | Cleared |
| $7400 | +$74 | 0x00000000 | Cleared |
| $0800 | +$08 | 0x00000000 | Then: every 2nd channel (0,2,4,...) → 0x08000000 |
| $0400 | +$04 | 0x00000000 | Cleared |
| $2400 | +$24 | 0x00000000 | Cleared |
| $5400 | +$54 | 0x00000000 | Cleared |
| $4400 | +$44 | 0x00000000 | Cleared. **Also used for sample rate during voice init** (see below) |

**Post-init patterns** (after zeroing all 32 channels):

| Register | Channel stride | Value | Interpretation |
|----------|---------------|-------|---------------|
| $0800 (+$08) | Every 2 channels (0,2,4,...30) | 0x08000000 | Routing matrix — connects even channels to output bus A |
| $3400 (+$34) | Every 4 channels (0,4,8,...28) | 0x8000FFFF | Mix bus A config — full scale, bus A select |
| $3400 (+$34) | Every 4 channels (2,6,10,...30) | 0x2000FFFF | Mix bus B config — reduced scale, bus B select |

### Voice runtime H-chip registers

During live playback, voices write to the H-chip through **memory-mapped offsets** relative to a voice hardware base pointer. These are NOT packed-address format; they are direct bus offsets from the voice's assigned hardware slot.

Each voice runtime descriptor holds hardware pointers at:
- +$08: Voice A hardware base (G-chip/H-chip)
- +$0C: Voice A H-chip base (same region, different offset window)
- +$10: Voice B hardware base (stereo pair, if +$30 nonzero)
- +$14: Voice B H-chip base
- +$30: Stereo flag (nonzero = stereo pair active)
- +$31: H-chip mode (≤3 = direct DSP bus, >3 = expansion bus via EXP_BUS)

**Subcommand register** (`voice_base + $46`): Selects which parameter group the subsequent data writes target.

| Subcommand | Mode | Description |
|------------|------|-------------|
| 0 | Filter update | Subsequent writes to +$7C set filter cutoff/Q |
| 1 | Volume/pan update | Subsequent writes to +$71/+$73 set left/right volume |
| 4 | Mute / reset | Zeros volumes at +$71/+$73. Used by `hchip_mute_channel` |

**Data registers** (written after subcommand select):

| Offset | Size | Range | Written by | Description |
|--------|------|-------|-----------|-------------|
| +$44 | 16-bit | — | `hchip_init_all_channels` | Sample rate value (from DAT_F01C5E). Set during init. |
| +$46 | 16-bit | 0,1,4 | `voice_update_filter`, `voice_update_volume`, `hchip_mute_channel` | Subcommand select (see table above) |
| +$71 | 8-bit | 0–255 | `voice_update_volume`, `voice_update_volume_init` | Left channel volume |
| +$73 | 8-bit | 0–255 | `voice_update_volume`, `voice_update_volume_init` | Right channel volume |
| +$7B | 8-bit | — | `hchip_write_filter_coeff` | Filter coefficient data (mode-encoded, see filter section) |
| +$7C | 8-bit | 0–255 | `voice_update_filter` | Filter cutoff / resonance (Q) |

### Expansion bus channel addressing

When `expansion_present != 0` (expansion polyphony board installed), voices with H-chip mode > 3 use the **expansion bus** (EXP_BUS at 0x540000) instead of direct DSP bus access:

```c
// Channel select for expansion bus
HCHIP_CHAN_SEL = (voice_hw_ptr & 0x7400) >> 10;
EXP_BUS[0] = HCHIP_CHAN_SEL;    // write channel select to expansion bus port 0

// Remap address to expansion bus space
voice_base = (voice_hw_ptr & 0x3FF) | 0x540400;
```

The expansion bus maps 8 additional channels with select values 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, configured by `FUN_050114`.

**Channel address lookup table** (64 entries at 0x05082C): Maps logical voice channel indices (0–63) to signed 16-bit hardware address offsets. Used by `hchip_get_channel_addr` (0x05092C, 17 xrefs) to translate voice numbers to bus addresses.

### Volume and pan computation

Volume writes are computed by `voice_update_volume` (0x083678) and `voice_update_volume_init` (0x08393C):

1. **Amplitude envelope** → master volume (0–255 after clamping)
2. **Pan position** → pan offset (added to base 0x40 = center)
3. **Pan-to-volume curve**: `WORD_ARRAY_00082CD2` (128-entry lookup table) maps pan position to volume scaling. For mono voices:
   - Left volume = `master_vol + (pan_curve[pan + 0x40] >> 4)`
   - Right volume = `master_vol + (pan_curve[(pan + 0x40) ^ 0x7F] >> 4)`
   - The XOR with 0x7F creates the complementary curve for the opposite channel
4. **Stereo voices** use a different path with an additional stereo width factor at +$F6
5. **Dirty check**: `voice_update_volume` (the per-tick path) compares computed volume (+$F2) and pan (+$F4) against previous values and skips H-chip writes if unchanged. `voice_update_volume_init` (first tick) always writes.

MIDI CC#7 (channel volume) is applied when `midi_mode == 2` or `GlobPedOvr != 0`, with three curve options:
- Curve 0: Linear
- Curve 1: Quadratic (square of deviation from 0x1000)
- Curve 2: Custom curve via `BYTE_ARRAY_0008359E`

### Filter processing

Filter updates are computed by `voice_update_filter` (0x08341C):

1. **Cutoff**: Sum of cord outputs at voice +$FC and +$100, clamped to 0–0xFFF, then shifted right 5 to yield 0–127.
2. **Filter type dispatch**: `filter_type_dispatch` (0x07BE20) selects the active filter algorithm from a table at `DAT_F33E22`. Each filter type has its own coefficient computation function.
3. **Q / resonance**: Sum of cord outputs at voice +$FE and +$FA, shifted right 4, clamped 0–255.
4. **H-chip write sequence**: Subcommand 0 → write cutoff byte to +$7C.

**Filter mode encoding** (from `hchip_write_filter_coeff` at 0x050D82): The filter type index selects a mode byte from a 4-entry table at 0x050D7E:

| Index | Mode byte | Interpretation |
|-------|-----------|---------------|
| 0 | 0x37 | Mode A (default / 2-pole lowpass?) |
| 1 | 0x01 | Mode B (highpass?) |
| 2 | 0x13 | Mode C (bandpass?) |
| 3 | 0x25 | Mode D (parametric?) |

This mode byte is combined with the coefficient value and written to register +$7B.

### Coefficient tables

Three coefficient tables are stored in ROM and loaded via `hchip_set_mode` (0x0639F2) → `hchip_load_config` (0x0640CA):

**`hchip_boot_coeffs`** (0x0648EC, 128 words): Default boot configuration. Uniform pattern of `0x3ECA` / `0x0474` for all 128 entries (with minor variations in entries 6–7: `0x3ECA`/`0x0363` and `0x3ECA`/`0x0263`). This represents a flat/passthrough filter state.

**`hchip_reset_coeffs`** (0x063F64, 128 words): Reset/transition coefficients used during config load sequence. Structured as 4-byte entries: `[channel|register, 0xFF, bank_index, 0x30]` cycling through 16 indices across 4 banks for 64 channels total. Written with enable/disable bracketing during `hchip_load_config`.

**`hchip_alt_coeffs`** (0x064140, 128 words): Alternate filter configuration with varied coefficient data — possibly a different filter response curve for special modes.

### Configuration load sequence

`hchip_load_config` (0x0640CA) performs a glitch-free filter configuration transition:

1. Zero all 20 control registers ($1800–$1813)
2. Write `hchip_reset_coeffs` with enable=1 (unmuted transitional state)
3. Delay **150ms** for H-chip filters to settle
4. Write `hchip_reset_coeffs` with enable=0 (mute transitional)
5. Write target coefficients with enable=0 (muted target)
6. Write target coefficients with enable=1 (unmute target — live)

The `hchip_write_channels` function (0x064064) iterates all 128 entries in the coefficient table. Index bits[4:0] select the channel, bits[6:5] select which of the 4 H-chip data ports to write. When `enable=0` (muted), bit 15 is OR'd into all odd-indexed entries to set a mute flag.

### H-chip `$07F7` — clear/passthrough value

`hchip_clear_all` (0x063D80) writes `0x07F7` to all 32 channels on each of the 4 data ports (+$28, +$2A, +$38, +$3A). This value likely represents a unity-gain passthrough or full-mute filter coefficient, depending on the bit interpretation by the H-chip hardware.

### Function reference

| Function | Address | Description |
|----------|---------|-------------|
| `hchip_init` | 0x0636C2 | Full hardware init: probe bus, config regs, program all channels, set mode 1 |
| `hchip_init_data_ptrs` | 0x06363E | Compute 4 data port pointers (+$28/+$2A/+$38/+$3A) from bus base |
| `hchip_is_alternate_bus` | 0x0636A4 | Check if using $530000 alternate bus |
| `hchip_set_mode` | 0x0639F2 | Select operating mode: 0=clear, 1=boot coeffs, 2=alt coeffs |
| `hchip_clear_all` | 0x063D80 | Write $07F7 to all channels, zero control regs |
| `hchip_read_reg16` | 0x063E3C | Read 16-bit register via packed address |
| `hchip_write_reg16` | 0x063E8A | Write 16-bit register via packed address (27 xrefs) |
| `hchip_write_reg32` | 0x063EB6 | Write 32-bit register pair via packed address (27 xrefs) |
| `hchip_write_channels` | 0x064064 | Write 128-word coefficient table to all 4 H-chips |
| `hchip_load_config` | 0x0640CA | Config load with reset→settle→mute→load→unmute sequence |
| `hchip_load_config_wrapper` | 0x064128 | Thin wrapper for hchip_load_config |
| `hchip_select_bus` | 0x024144 | Select bus base from hardware mode / channel count |
| `hchip_setup_voice_bus` | 0x0503E4 | Store bus pointer, init 32 channels to default |
| `hchip_init_all_channels` | 0x050410 | Write sample rate init values to all 8 (or 16) channels |
| `hchip_get_channel_addr` | 0x05092C | Logical channel → hardware address via LUT (17 xrefs) |
| `hchip_mute_channel` | 0x050A22 | Mute: subcommand 4, zero +$71/+$73 volumes |
| `hchip_write_filter_coeff` | 0x050D82 | Write filter coefficient to +$7B with mode encoding (16 xrefs) |
| `filter_type_dispatch` | 0x07BE20 | Dispatch to active filter type's coefficient computation |
| `voice_update_pitch` | 0x08328C | Compute pitch, write G-chip oscillator registers |
| `voice_update_filter` | 0x08341C | Compute filter cutoff/Q, write H-chip +$46=0, +$7C |
| `voice_update_volume_init` | 0x08393C | First-tick volume/pan → H-chip +$46=1, +$71/+$73 |
| `voice_update_volume` | 0x083678 | Per-tick volume with dirty check, same H-chip writes |

### Globals reference

| Symbol | Address | Type | Description |
|--------|---------|------|-------------|
| `HCHIP_BUS` | 0x460000 | word[16384] | CPU chip-select for H-chip (CSHCHIP). Not used for data path — see DSP_BUS. |
| `DSP_BUS` | 0x520000 | word[32768] | Primary DSP bus — actual H-chip register access |
| `EXP_BUS` | 0x540000 | word[8192] | Expansion polyphony board bus |
| `hchip_bus_base` | 0xF3E0F0 | pointer | Active DSP bus base (0x520000 or 0x530000) |
| `hchip_chan_sel_ptr` | 0xF3E110 | pointer | Pointer to channel select register (bus_base + $0E) |
| `HCHIP_CHAN_SEL` | 0xF3DD38 | word | Channel select register shadow (expansion bus path) |
| `HCHIP_CHAN_DATA` | 0xF3DD40 | word | Channel data register (expansion bus path) |
| `hchip_data_ptrs` | 0xF3E100 | pointer[4] | Pointers to 4 H-chip data ports (+$28,+$2A,+$38,+$3A) |
| `hchip_present` | 0xF2592A | byte | 1 if H-chip detected during boot, 0 otherwise |
| `hchip_boot_coeffs` | 0x0648EC | word[128] | Boot/default coefficient table (ROM) |
| `hchip_reset_coeffs` | 0x063F64 | word[128] | Transitional reset coefficients (ROM) |
| `hchip_alt_coeffs` | 0x064140 | word[128] | Alternate coefficient table (ROM) |
| `CTRLREG1` | 0x400000 | word | Control register 1 — bits 7,10 control H-chip reset/clock |
| `CTRLREG1_copy` | 0xF0173C | word | Shadow copy of CTRLREG1 (read-modify-write via shadow) |
| `expansion_present` | 0xF01C5C | byte | Nonzero if expansion polyphony board is installed |

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
