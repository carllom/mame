# Roland S-330 System 1.03 Reference

## Overview

System 1.03 is the main operating system for the Roland S-330. It is loaded from disk by the ROM boot process and provides the user interface, sound engine control, MIDI processing, disk operations, and parameter editing. The system uses a bank-switched architecture with overlapping code segments.

## Concepts

- 16 _voices_ polyphony
- 8 MIDI receive channels in parallel; each MIDI channel maps to a _patch_
- 32 _tones_ in internal memory
- 16 _patches_ in internal memory
- _Samples_ stored in two _wave banks_ (A and B), 18 _segments_ per bank
- A _tone_ = sample data + tone parameter set
- A _subtone_ borrows wave data from another tone but uses its own parameters
- A _patch_ = up to two tones assigned to a key with performance parameters
- _Function data_ = parameters for Play/Func mode
- _MIDI data_ = MIDI receive parameters

## Operating Modes

| Mode | Description                                        |
|------|----------------------------------------------------|
| Play | Default mode — play sounds                         |
| Edit | Edit tones, assign tones to note ranges             |
| DISK | Read/write disk data                                |
| FUNC | Master tune, init parameters, select controller     |
| MIDI | Set MIDI channel, monitor MIDI data                 |
| UTIL | Load utility disk, special functions                |

## System Disk Layout

| Bank   | Size  | Virtual Addr | Disk Location   | Segment Name |
|--------|-------|-------------|-----------------|--------------|
| H1     | 2000h | 4000        | 2400-4400 (T1)  | HBANK1       |
| H2     | 2800h | 8000        | 4400-6C00       | HBANK2       |
| L1     | 1F00h | 0100        | 6C00-8B00 (T3)  | LBANK1       |
| L5.1   | 0400h | 0100        | 8C00-9000       | LBANK5A      |
| L2     | 1F00h | 0100        | 9000-AF00 (T4)  | LBANK2       |
| L5.5   | 0400h | 0500        | B000-B400       | LBANK5B      |
| L3     | 1F00h | 0100        | B400-D300 (T5)  | LBANK3       |
| L5.9   | 0400h | 0900        | D400-D800       | LBANK5C      |
| L4     | 1F00h | 0100        | D800-F700 (T6)  | LBANK4       |
| L5.D   | 0400h | 0D00        | F800-FC00       | LBANK5D      |
| L5.11  | 0F00h | 1100        | 1400-2300       | LBANK5E      |

Note: L5 (LoBank 5) is loaded in split 400h-byte chunks interleaved with the other overlays, plus a final chunk at 1400-2300.

## RAM Layout

| Address Range | Description                                |
|---------------|--------------------------------------------|
| 0000-00FF     | CPU internal registers/variables           |
| 0100-1FFD     | LoBank RAM — banked 8K (overlays 0-7)      |
| 2000-3FFF     | ROM — 8K BIOS                              |
| 4000-5FFF     | Fixed RAM — System1 (HiBank H1)            |
| 6000-7FFF     | Fixed RAM — System variables               |
| 8000-BFFF     | HiBank RAM — banked 16K (System2/3)        |
| C000-FFFF     | I/O space                                  |

## Interrupt Vectors (4008-402B)

All interrupt handlers are no-op (→ 402C dummy handler) except:

| Address | Interrupt           | Handler |
|---------|---------------------|---------|
| 401C    | Software Timer      | 4657    |
| 4020    | Serial Port (MIDI)  | 4C06    |

## System Function Vectors (4030-404D)

| Address | Description                                    |
|---------|------------------------------------------------|
| 4004    | System entry point (called by ROM after load)  |
| 4030    | Wave R/W callback                              |
| 4033    | Format callback                                |
| 4036    | Called from VDP functions                       |

## System Registers (Internal RAM)

### Core System State

| Register | Type  | Description                                            |
|----------|-------|--------------------------------------------------------|
| R72      | byte  | Current bank number                                    |
| R74      | byte  | bit0: 0 = allow ROM monitor break via MIDI FF; bit2: controller-related; bit5-7: controller type (table index) |
| R77      | byte  | LCD RAM address                                        |
| R78      | byte  | LCD Column                                             |
| R79      | byte  | LCD Row                                                |
| R82      | bits  | bit0: Page 0 active                                    |
| R84      | bits  | bit0: MIDI TX pending; bit2: keypress available; bit4: dial stopped; bit5: related to bit4 |
| R85      | bits  | bit0: Timer0 input avail; bit2: mouse stopped; bit4: mouse button; bit6: INC/DEC allowed; bit7: Inc/Dec+MouseBtn |
| R86      | bits  | bit2: mouse stopped event; bit6: dial stopped event    |
| R87      | bits  | Key mode bits (see table @59D8). bit1: 0=one tone, 1=tone1&2 sounding |

### Display State

| Register | Type  | Description                   |
|----------|-------|-------------------------------|
| R8A      | byte  | VDP Column                    |
| R8B      | byte  | VDP Row                       |
| R8C      | byte  | VDP Attribute                 |
| R8D      | byte  | VDP Cursor Attribute          |
| R8E      | byte  | Current character             |
| R8F      | byte  | Current key pressed           |
| R90      | byte  | Translated character (from key)|

### Sound State

| Register  | Type  | Description                                    |
|-----------|-------|------------------------------------------------|
| RW88      | word  | Bitfield of currently playing channels (SA-16) |
| RW9A      | word  | Bitfield for new tones to play (copy of C001/3)|
| R9E       | byte  | MemWave — RAM start wave (0-17)                |
| R9F       | byte  | NumWaves — number of waves to load/save        |
| RA0       | byte  | DiskWave — disk start wave (0-35)              |
| RA1       | byte  | WaveBank — wave bank # (0-1)                   |
| RC4       | byte  | Current note channel (SA-16 index)             |
| RC6       | byte  | Tone offset (0..1E in steps of 2)              |
| RC8       | byte  | Patch number                                   |
| RDE       | byte  | MIDI Data1 — note number                       |
| RDF       | byte  | MIDI Data2 — velocity                          |
| RE0       | byte  | MaxPatch — max patch number in current bank    |
| RE6       | byte  | Number of new notes to play                    |

### Disk State

| Register  | Type  | Description                              |
|-----------|-------|------------------------------------------|
| RA2       | byte  | Drive number (0-3)                       |
| RA3       | byte  | Disk side (0-1)                          |
| RA4       | byte  | Disk track (0-79)                        |
| RA5       | byte  | Disk sector (0-9)                        |
| RA6       | bits  | Disk status: bit0=ready, bit1=unrecognized, bit2=writeprotect, bit3=load error, bit4=validated |
| RAC       | byte  | (various uses)                           |

### MIDI Buffers

| Register  | Type  | Description                              |
|-----------|-------|------------------------------------------|
| RWB8      | word  | MidiTxTail — insert point                |
| RWBA      | word  | MidiTxHead — consume point               |
| RWBC      | word  | MidiRxTail — insert point                |
| RWBE      | word  | MidiRxHead — consume point               |

### UI State

| Register  | Type  | Description                              |
|-----------|-------|------------------------------------------|
| RWAE      | word  | Current parameter definition address     |
| RWB2      | word  | Current Patch Address (ptr to patch bank)|
| RWB4      | word  | Pointer to tone_param struct             |
| RWB6      | word  | Current Tone Address (ptr to tone bank)  |
| RD4       | byte  | Mouse movement hysteresis filter counter |
| REA       | byte  | Current mode (0=Play,1=Edit,2=Disk,3=Func,4=Midi,5=Util) |
| REB       | byte  | Current submenu item / active page       |
| REC       | byte  | Selected patch                           |
| REE       | byte  | Disk activity status + error flags       |
| RF0       | byte  | Value input delta (INC/DEC → +1/-1)      |
| RF1       | byte  | Input delta (accumulated)                |
| RF2       | byte  | Input key value                          |
| RF3       | byte  | Mouse movement state (7 or 9 = moving)   |
| RFA       | byte  | Current param_uipos                      |
| RFB       | byte  | Next param_uipos                         |
| R100      | byte  | Utility number                           |

## System Variables (6000+)

### Key Tables

| Address     | Description                   |
|-------------|-------------------------------|
| 6000-6001   | Current keyvalue bitmask      |
| 6010-6011   | Last keyvalue bitmask         |
| 6020-6021   | KeyOn bitmask                 |
| 6030-6031   | KeyOff bitmask                |

### Key Code Translation (4060-409F)

| Address     | Description                   |
|-------------|-------------------------------|
| 4060-4067   | Row 0 KeyOn codes             |
| 4068-406F   | Row 1 KeyOn codes             |
| 4070-4077   | Row 2 KeyOn codes             |
| 4078-407F   | Row 3 KeyOn codes             |
| 4080-4087   | Row 0 KeyOff codes            |
| 4088-408F   | Row 1 KeyOff codes            |
| 4090-4097   | Row 2 KeyOff codes            |
| 4098-409F   | Row 3 KeyOff codes            |

### Formatting Data

| Address     | Description                                      |
|-------------|--------------------------------------------------|
| 404E-4056   | Array[9] written to track 0-7 during format      |
| 4057-405F   | Array[9] written to track 8+ during format       |

Arrays start at index 8, counted backwards to 0; during write, index wraps from 9 to 0.

### Floppy Sector Buffer

| Address     | Description               |
|-------------|---------------------------|
| 7A00-7BFF   | 512-byte sector buffer    |

## Bank Function Organization

### Bank 0 — System Core (sysch1, fixed at 4000-5FFF)

Main loop, ISRs, SA-16 control, MIDI processing, input handling, RC-100/mouse drivers.

| Function          | Address | In / Out                                  | Description                          |
|-------------------|---------|-------------------------------------------|--------------------------------------|
| sys_start         | 459C    | —                                         | System entry point. Init display, SA-16, start main loop |
| init_display      | 4584    | —                                         | Initialize VDP + LCD displays         |
| timer_isr         | 4657    | —                                         | Software timer interrupt handler      |
| midi_isr          | 4C06    | —                                         | Serial port / MIDI receive interrupt  |
| sa16_all_chn_on   | 48EC    | RW9A=channel bitmask                      | Enable SA-16 channels from bitmask   |
| sa16_all_chn_off  | 48FB    | RW88=channel bitmask                      | Disable SA-16 channels               |
| find_neg_sample   | 4946    | —                                         | Find negative-going zero crossing in sample |
| get_AD_sample     | 4973    | —                                         | Read A/D converter sample            |
| init_mousereg     | 4F13    | —                                         | Initialize mouse registers           |
| get_dial_input    | 4F35    | — / RF0=delta, RF1=accumulated            | Read dial input from RC-100          |
| rc100_receive     | 5066    | — / R8F=keycode                           | Process incoming RC-100 serial data  |
| RC100_keyprs      | 517C    | R8F=keycode                               | Handle RC-100 key press              |
| RC100_keyrel      | 51A1    | R8F=keycode                               | Handle RC-100 key release            |
| rc100_send        | 51C6    | R24=data byte                             | Send byte to RC-100                  |
| mouse_read        | 5684    | — / R8F=keycode (movement/button)         | Read mouse position/buttons          |
| strob_delay       | 56E3    | —                                         | Strobe timing delay for mouse        |

Primary UI navigation and page loading.

| Function          | Address | Description                          |
|-------------------|---------|--------------------------------------|
| load_activ_page   | 055E    | Load active page (toggles hibank1/2) |
| copy_wordlist     | 06A4    | Copy word list                       |
| copy_bytelist     | 06B9    | Copy byte list                       |
| get_menupos       | 07C4    | Get current menu position            |
| print_submenuitem | 0812    | Print submenu item text              |
| get_toneparam     | 0A01    | Get tone parameter                   |

### Bank 2 — Display/Print Functions

Screen rendering and parameter display.

| Function          | Address | Description                          |
|-------------------|---------|--------------------------------------|
| print_4blank      | 02E4    | Print 4 blank characters             |
| print_voicemode   | 035E    | Print voice mode                     |
| print_patchNbr    | 03B1    | Print patch number                   |
| print_voicegrp    | 0466    | Print voice group                    |
| print_note_w      | 0670    | Print note (word)                    |
| print_tone_w      | 06C3    | Print tone (word)                    |
| prn_tone_startpt  | 0732    | Print tone start point               |
| prn_tone_looppt   | 073B    | Print tone loop point                |
| prn_tone_endpt    | 0758    | Print tone end point                 |
| print_2dignbr     | 07D3    | Print 2-digit number                 |
| prn_patchname     | 0930    | Print patch name                     |
| prn_tonename      | 094F    | Print tone name                      |
| prn_tone_info     | 0960    | Print tone info                      |
| prn_tonpatlst     | 0A25    | Print tone/patch list                |
| prn_pprm_label    | 0B06    | Print patch parameter label          |
| prn_tprm_label    | 0BA8    | Print tone parameter label           |
| display_msg       | 0E8C    | Display message                      |
| prn_remtime       | 1110    | Print remaining time                 |
| cmd_deltone       | 1A7C    | Command: Delete tone                 |
| cmd_delbank       | 1BC2    | Command: Delete bank                 |
| clear_bank        | 1BE6    | Clear bank                           |
| cmd_delalltones   | 1C00    | Command: Delete all tones            |
| cmd_copy          | 1C21    | Command: Copy                        |
| cmd_move          | 1D2F    | Command: Move                        |

### Bank 3 — Sound Engine

Voice allocation, TVF, note handling, MIDI monitoring.

| Function          | Address | Description                          |
|-------------------|---------|--------------------------------------|
| wait_32           | 01AA    | Wait 32 cycles                       |
| print_err         | 02B2    | Print error message                  |
| tvf_cutoff_calc   | 0714    | Calculate TVF cutoff value           |
| note_to_freq      | 0981    | Translate note number to frequency   |
| vdp_prn_hex       | 0B2D    | Print hex to VDP                     |
| midimon_prn_msg   | 0B6A    | MIDI monitor: print message          |
| vdp_setoff_attr   | 0C8B    | Set VDP offset + attribute           |
| keyprs_handler    | 0E5A    | Key press handler                    |
| note_handler      | 0EA9    | Note on/off handler                  |

### Bank 4 — Disk Functions

All disk load/save/format operations.

| Function          | Address | Description                          |
|-------------------|---------|--------------------------------------|
| chk_disk_type     | 0A05    | Check disk type/version              |
| load_parameters   | 0D2F    | Load parameters from disk            |
| load_patchparam   | 0D58    | Load patch parameters                |
| load_toneparam    | 0D68    | Load tone parameters                 |
| cmd_loadset       | 0DAB    | Command: Load Set                    |
| cmd_loadblock     | 0DDE    | Command: Load Block                  |
| show_disk_label   | 0E94    | Show disk label                      |
| load_all_waves    | 0ED1    | Load all wave data                   |
| cmd_loadfunc      | 0EF1    | Command: Load Function data          |
| cmd_loadmidi      | 0F18    | Command: Load MIDI data              |
| cmd_dirtone       | 0F4C    | Command: Directory Tone              |
| cmd_dirpatch      | 0F8A    | Command: Directory Patch             |
| cmd_chgsys        | 0FE8    | Command: Change System               |
| cmd_loadtone      | 10B9    | Command: Load Tone                   |
| cmd_saveset       | 13A7    | Command: Save Set                    |
| cmd_saveblock     | 13E6    | Command: Save Block                  |
| cmd_savefunc      | 1483    | Command: Save Function               |
| cmd_savemidi      | 14D9    | Command: Save MIDI                   |
| cmd_savesys       | 1516    | Command: Save System                 |
| cmd_backup        | 1528    | Command: Backup                      |
| cmd_format        | 1532    | Command: Format                      |

### Bank 5 — Edit / SysEx / Controller

Parameter editing, MIDI SysEx, external controller setup.

| Function              | Address | Description                       |
|-----------------------|---------|-----------------------------------|
| cmd_set_extctl        | 04C9    | Set external controller type      |
| read_alphanum_key     | 0D59    | Read alphanumeric key (RC-100)    |
| cmd_init_patpar       | 0DD5    | Init patch parameters             |
| cmd_init_midipar      | 0DE9    | Init MIDI parameters              |
| cmd_init_fncpar       | 0E16    | Init function parameters          |
| cmd_pat_initall       | 0E6F    | Init all patches                  |
| cmd_ton_initall       | 0E75    | Init all tones                    |
| cmd_pat_copyall       | 1008    | Copy all patches                  |
| cmd_ton_copyall       | 103C    | Copy all tones                    |
| cmd_copypage          | 10B5    | Copy page                         |
| cmd_pat_swapall       | 11D8    | Swap all patches                  |
| cmd_ton_swapall       | 1217    | Swap all tones                    |
| cmd_swappage          | 12AF    | Swap page                         |
| toneaddr2waveram      | 158F    | Convert tone address to wave RAM  |
| waveram_read          | 15C7    | Read wave RAM                     |
| cmd_loop2end          | 1665    | Command: Loop to End              |
| cmd_end2loop          | 16DF    | Command: End to Loop              |
| handle_rq1            | 1A2F    | Handle SysEx RQ1 (request)        |
| handle_dt1            | 1A3D    | Handle SysEx DT1 (data set)       |
| handle_wsd            | 1AA0    | Handle SysEx WSD (wave send data) |
| handle_rqd            | 1AB4    | Handle SysEx RQD (request data)   |
| send_sysex_dataset    | 1AE3    | Send SysEx data set               |
| parse_sysex           | 1D54    | Parse incoming SysEx              |
| parse_sysex_hdr       | 1D8B    | Parse SysEx header                |

### Bank 6 & 7 — Utilities

Loaded on demand from the utility disk.

## UI System

### Navigation Model

The system uses a hierarchical mode → menu → submenu → command model:

1. **MODE** key opens the mode menu (Play/Edit/Disk/Func/Midi/Util)
2. Select mode with Up/Down, confirm with EXE
3. Each mode has window menus; select with Up/Down, confirm with EXE
4. **SUB** key opens submenu (tone/patch selection)
5. **COM** key opens command menu for the current window
6. Parameters edited with INC/DEC keys, mouse, or RC-100 dial

### Controller Types

| Value | Controller    |
|-------|---------------|
| 0     | None          |
| 1     | Mouse         |
| 2     | RC-100        |

### Window Data Structure

```
window_struc {
    db field0       ; Window type/display flags
    db menuLabels   ; Index to menu label string list
    db menuFunc     ; Index to menu function pointer table
    db field3       ; Index: hi nybble → string list, lo nybble → pointer table
    dw prmLabelAddr ; Pointer to parameter label string list (FF-terminated)
    dw prmDataAddr  ; Pointer to param_data array (FFFE-terminated)
    db field8       ; Number of parameters / first param index
    db field9       ; Display configuration
    db fieldA       ; Command configuration (FF = use fieldB)
    db fieldB       ; Alternate command configuration
}
```

### Parameter Data Structure

```
param_data {
    dw offset       ; Offset within data area
    db dataarea     ; bit0-4: area type, bit5-7: flags
    db valuetype    ; Index of value type (enum/range/string)
}
```

### Parameter UI Position Structure

```
param_uipos {
    db prev         ; Previous param index (FBh-FEh = special)
    db next         ; Next param index (FFh = none)
    db alt          ; Alternate navigation
    db linked       ; Linked param index (FFh = none)
    db column       ; Screen column
    db row          ; Screen row
}
```

## Window Definitions

| Index | Name             | Mode | Description                  |
|-------|------------------|------|------------------------------|
| 00    | Play_Keyboard    | Play | Keyboard display             |
| 01    | Play_PatchDisp   | Play | Patch display                |
| 02    | Song_Write       | Play | Song write                   |
| 03    | Edit_PatchPrm    | Edit | Patch parameters             |
| 04    | Edit_Split       | Edit | Split point editing          |
| 05    | Edit_PatchMap    | Edit | Patch map                    |
| 06    | Edit_TonePrm    | Edit | Tone parameters              |
| 07    | Edit_Loop        | Edit | Loop point editing           |
| 08    | Edit_LFO         | Edit | LFO settings                 |
| 09    | Edit_TVF         | Edit | TVF envelope                 |
| 0A    | Edit_TVA         | Edit | TVA envelope                 |
| 0B    | Edit_ToneMap     | Edit | Tone map                     |
| 0C    | Edit_Delete      | Edit | Delete tone/bank             |
| 0D    | Edit_CopyMove    | Edit | Copy/Move                    |
| 0E    | Edit_DispWave    | Edit | Display waveform             |
| 0F    | Disk_LoadSound   | Disk | Load sound set               |
| 10    | Disk_LoadSong    | Disk | Load song                    |
| 11    | Disk_LoadTone    | Disk | Load tone                    |
| 12    | Disk_DirPatch    | Disk | Directory: patches           |
| 13    | Disk_DirTone     | Disk | Directory: tones             |
| 14    | Disk_LabelSet    | Disk | Label set (disk name)        |
| 15    | Disk_SaveSound   | Disk | Save sound set               |
| 16    | Disk_SaveSong    | Disk | Save song                    |
| 17    | Disk_Format      | Disk | Format disk                  |
| 18    | Disk_DelSong     | Disk | Delete song                  |
| 19    | Disk_SaveSys     | Disk | Save system                  |
| 1A    | Disk_ChangeSys   | Disk | Change system                |
| 1B    | Func_Master      | Func | Master settings              |
| 1C    | Func_Initialize  | Func | Initialize parameters        |
| 1D    | Midi_Message     | MIDI | MIDI message setup           |
| 1E    | Midi_Prog        | MIDI | MIDI program numbers         |
| 1F    | Midi_Monitor     | MIDI | MIDI monitor                 |

## Disk Type Validation

The system identifies disk types by the version bytes in the boot sector:

| ver0 | ver1 | disk_type | Description                        |
|------|------|-----------|------------------------------------|
| 0    | 0    | 1         | Unknown                            |
| 0    | 1    | 2         | D-50 compatible                    |
| 0    | 2    | 2         | Unknown                            |
| 0    | 4    | 0         | S-550 (rejected with error)        |
| 0    | 8    | 0         | S-330 system — accepted            |
| 0    | 9    | 5         | S-330 utility (version check 1.03) |
| 1    | x    | FF        | Unknown (error)                    |

## Tone/Patch/Sample Relationships

- A _sample_ occupies a wave segment in Bank A or B (18 segments per bank)
- A _tone_ pairs a sample with parameters (TVF, TVA, LFO, loop points, etc.)
- Tone number format: `Tnn` where first digit is bank (1-4), second is index (1-8)
- A _patch_ maps up to 2 tones to a keyboard range with performance parameters
- Patch number format: `Pnn` (0-15)
- Tone parameters include sampling rate indicator: `X2` means 15kHz sampling
- Sampling time is in 0.4-second steps (stored at offset 0x0F in the structure)

## Data Structures

### Patch Parameters (patchpar)

Each patch defines key mapping, velocity curves, and performance parameters for up to 2 tones:

```
patchpar {             ; (size varies, approx 48+ bytes)
    ; Key assignment
    db keymode         ; 00: Key mode (bit1: 0=one tone, 1=tone1&2)
    db tone1           ; 01: Tone 1 assignment
    db tone2           ; 02: Tone 2 assignment
    db splitpoint      ; 03: Split point (MIDI note)
    ; Velocity
    db velcurve1       ; 04: Velocity curve tone 1
    db velcurve2       ; 05: Velocity curve tone 2
    ; Performance
    db level           ; 06: Patch level
    db pan             ; 07: Pan position
    db output          ; 08: Output channel/routing
    db priority        ; 09: Voice priority
    ; Bend/aftertouch
    db bendrange       ; 0A: Pitch bend range
    db aftertouch      ; 0B: Aftertouch mode
    ; Name
    db name[12]        ; Patch name (ASCII)
}
```

### Tone Parameters (tonepar)

Each tone defines sample playback, filtering, and modulation parameters:

```
tonepar {              ; (size varies, approx 48+ bytes)
    ; Sample reference
    db wavebank        ; 00: Wave bank (A/B)
    db waveseg         ; 01: Wave segment
    db origkey         ; 02: Original key (root note)
    ; Loop
    dw startpt         ; 03-04: Sample start point
    dw looppt          ; 05-06: Loop start point
    dw endpt           ; 07-08: Sample end point
    db loopmode        ; 09: Loop mode
    ; Tuning
    db coarsetune      ; 0A: Coarse tune
    db finetune        ; 0B: Fine tune
    ; Filter (TVF)
    db tvf_cutoff      ; 0C: TVF cutoff frequency
    db tvf_resonance   ; 0D: TVF resonance
    db tvf_keyfollow   ; 0E: TVF key follow
    ; Sample info
    db samplingtime    ; 0F: Sampling time (in 0.4s steps)
    ; TVF Envelope
    db tvf_depth       ; 10: TVF depth
    db tvf_vel         ; 11: TVF velocity sensitivity
    db tvf_t1-t4       ; 12-15: TVF envelope times
    db tvf_l1-l3       ; 16-18: TVF envelope levels
    ; TVA Envelope
    db tva_level       ; 19: TVA level
    db tva_vel         ; 1A: TVA velocity sensitivity
    db tva_t1-t4       ; 1B-1E: TVA envelope times
    db tva_l1-l3       ; 1F-21: TVA envelope levels
    ; LFO
    db lfo_rate        ; 1C: LFO rate
    db lfo_sync        ; 1D: LFO sync
    db lfo_delay       ; 1E: LFO delay
    db lfo_mode        ; 20: LFO mode
    db lfo_polarity    ; 22: LFO polarity
    db lfo_offset      ; 23: LFO offset
    ; Name
    db name[12]        ; Tone name (ASCII)
}
```

### Function Parameters (funcpar)

Performance and master settings:

```
funcpar {
    db mastertune      ; Master tune setting
    db masterlevel     ; Master level
    db keymode         ; Key mode (bit flags)
    db controller      ; External controller type (0=None, 1=Mouse, 2=RC-100)
    ; ... additional fields
}
```

### MIDI Parameters (midipar)

Per-channel MIDI receive configuration:

```
midipar {
    db channel         ; MIDI receive channel
    db progchange      ; Program change enable
    db volume          ; Volume CC enable
    ; ... additional per-channel settings
}
```

### Tone Header (toneheader)

Stored in disk directory:

```
toneheader {
    db name[12]        ; Tone name
    db wavebank        ; Wave bank (A/B)
    db waveseg         ; Segment count
    db samprate        ; Sampling rate indicator
    db samptime        ; Sampling time
}
```

### Disk Label (DiskLabel)

```
DiskLabel {
    db name[60]        ; Disk label name (0x3C bytes)
}
```

### Envelope Struct (env)

Used for TVF and TVA envelope definitions:

```
env {
    db depth           ; Envelope depth
    db velocity        ; Velocity sensitivity
    db t1, t2, t3, t4  ; Time values (4 stages)
    db l1, l2, l3      ; Level values (3 stages)
}
```

## Info Messages

| Index | Message                |
|-------|------------------------|
| 00    | Completed              |
| 01    | Please Wait            |
| 02    | Disk Protected         |
| 03    | (not a sound disk)     |
| 04    | (not a system disk)    |
| 05    | Disk Error             |
| 06    | Sound Mem Full         |
| 07    | Wave Mem Full          |
| 08    | Not Ready              |
| 09    | Sampling...            |
| 0A    | (blank)                |
| 0B    | Execute?               |
| 0C    | Read Error             |
| 0D    | Write Error            |
| 0E    | Select Error           |
| 0F    | Check Tone             |
| 10    | Check Sound Disk       |
| 11    | Version Error          |
| 12    | Insert Sound Disk      |
| 13    | (Director-S only)      |
| 14    | Insert System Disk     |
| 15    | Loading...             |
| 16    | Saving...              |

## SysEx Commands

The system handles Roland-format SysEx messages (F0 41 ...):

| Command | Name | Description                              |
|---------|------|------------------------------------------|
| RQ1     | Request Data 1 | Request parameter data (→ DT1 response) |
| DT1     | Data Set 1     | Send/receive parameter data             |
| WSD     | Wave Send Data | Transfer wave sample data               |
| RQD     | Request Data   | Request wave sample data (→ WSD)        |
