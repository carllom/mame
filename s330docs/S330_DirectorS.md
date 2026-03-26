# Roland S-330 Director-S Reference

## Overview

Director-S is an optional sequencer application for the Roland S-330, loaded from a dedicated disk. It extends the base system with song/pattern editing, a microscope editor, time calculation tools, and disk copy/transfer utilities. Director-S shares a large portion of its codebase with System 1.03 but adds additional overlays for the sequencer features.

## Disk Layout

### Binary Segments

| Name           | Size       | RAM Address  | Disk Offset          | Notes                          |
|----------------|------------|-------------|----------------------|--------------------------------|
| System1.bin    | 8K (2000h) | 4000-5FFF   | 02400-043FF (T1)     | HiBank H1                     |
| System2.bin    | 10K (2800h)| 8000-A7FF   | 04400-06BFF          | HiBank H2                     |
| System2_2.bin  | 6K (1800h) | A800-BFFF   | 10C00-123FF          | HiBank H2 extension           |
| System3.bin    | 16K (4000h)| 8000-BFFF   | 12400-163FF          | HiBank H3                     |
| Overlay1.bin   | <8K (1EFEh)| 0100-1FFD   | 06C00-08AFD (T3)     | LoBank L1                     |
| Overlay2.bin   | <8K (1EFEh)| 0100-1FFD   | 09000-0AEFD (T4)     | LoBank L2                     |
| Overlay3.bin   | <8K (1EFEh)| 0100-1FFD   | 0B400-0D2FD (T5)     | LoBank L3                     |
| Overlay4.bin   | <8K (1EFEh)| 0100-1FFD   | 0D800-0F6FD (T6)     | LoBank L4                     |
| Overlay5.bin   | <8K (1EFEh)| 0100-1FFD   | (split chunks)       | LoBank L5 — assembled from 5 parts |
| Overlay6.bin   | <8K (1EFEh)| 0100-1FFD   | 16800-186FD (T10)    | LoBank L6                     |
| Overlay7.bin   | <8K (1EFEh)| 0100-1FFD   | 18800-1A6FD          | LoBank L7                     |
| R_7500.bin     | 1K (400h)  | 7500-7900   | 1A800-1ABFF          | Extra RAM data (loaded with L7)|

### Overlay 5 Split Layout

Overlay 5 is not contiguous on disk; it is loaded in 400h-byte chunks from gaps between the other overlays:

| Chunk   | Disk Offset   | RAM Address |
|---------|---------------|-------------|
| OV5_1   | 08C00-08FFF   | 0100-04FF   |
| OV5_2   | 0B000-0B3FF   | 0500-08FF   |
| OV5_3   | 0D400-0D7FF   | 0900-0CFF   |
| OV5_4   | 0F800-0FBFF   | 0D00-10FF   |
| OV5_5   | 01400-022FF   | 1100-1FFD   |

### Full Disk Map

```
00000-013FF  (boot sector + data)
01400-022FF  OV5_5 (1000h) — also loaded to FC00-10AFF at T7
02400-043FF  SY1   (2000h) T1
04400-06BFF  SY2   (2800h)
06C00-08AFD  OV1   (1EFEh) T3
08C00-08FFF  OV5_1  (400h)
09000-0AEFD  OV2   (1EFEh) T4
0B000-0B3FF  OV5_2  (400h)
0B400-0D2FD  OV3   (1EFEh) T5
0D400-0D7FF  OV5_3  (400h)
0D800-0F6FD  OV4   (1EFEh) T6
0F800-0FBFF  OV5_4  (400h)
10C00-123FF  SY2_2 (1800h)
12400-163FF  SY3   (4000h)
16800-186FD  OV6   (1EFEh) T10
18800-1A6FD  OV7   (1EFEh)
1A800-1ABFF  R_7500 (400h)
1B000+       Song directory & song data
```

### Song Data Area

| Disk Offset | Description                                             |
|-------------|---------------------------------------------------------|
| 1B000+      | Song directory — 128 entries × 30h bytes each           |
| 1C800+      | Song data (variable, e.g. "Mr Slick")                   |

## RAM Layout

| Address Range | Description                                |
|---------------|--------------------------------------------|
| 0000-00FF     | Internal RAM — registers/variables         |
| 0100-1FFD     | LoBank RAM — banked 8K (Overlay 0-7)       |
| 2000-3FFF     | ROM — 8K BIOS                              |
| 4000-7FFF     | Fixed RAM — 16K System1 + variables        |
| 8000-BFFF     | HiBank RAM — banked 16K (System2/2_2/3)    |
| C000-FFFF     | I/O space                                  |

## Register Differences from System 1.03

Director-S shifts several system register addresses compared to System 1.03:

| S103 | DirS | Delta | Description                          |
|------|------|-------|--------------------------------------|
| C4   | C2   | -2    | (function-related)                   |
| C8   | C6   | -2    | Selected multipatch                  |
| CA   | C8   | -2    | Selected patch                       |
| D4   | 7300 | -     | Mouse inactivity counter             |
| D5   | 7301 | -     | Dial inactivity counter              |
| EA   | D2   | -18   | MenuItemIndex (current mode)         |
| EB   | D3   | -18   | SubMenuItemIndex (current window)    |
| EC   | D4   | -18   | SelectedPatch                        |
| F0   | D8   | -18   | Input delta                          |
| F1   | D9   | -18   | Accumulated dial delta               |
| F2   | DA   | -18   | Input key value                      |
| F3   | DB   | -18   | Mouse movement state                 |

### Key RAM Addresses (Director-S specific)

| Address | Description                              |
|---------|------------------------------------------|
| 6C8C    | ActiveWindow structure                   |
| 6CA4    | winf3_adrlist                            |
| 6CA6    | winf3_banklist                           |
| 6CA8    | winf4_adrlist                            |
| 6CAA    | winf4_banklist                           |
| 6CAC    | winf5_adrlist                            |
| 6CAE    | winf5_banklist                           |
| 6CB0    | prmLabel address                         |
| 6CB2    | prmData address                          |
| 6CB4    | Current window function address pointer  |
| 6CB6    | Current window function bank pointer     |
| 6ED8    | buf_toneList (200h bytes)                |
| 7300    | Mouse inactivity counter                 |
| 7301    | Dial inactivity counter                  |
| 7388    | ActiveWindow base                        |
| 7900    | Stack                                    |

## Window Definitions

Director-S adds several windows beyond what System 1.03 offers, particularly for the sequencer:

| Index | Name             | Mode | field0 | Description                  |
|-------|------------------|------|--------|------------------------------|
| 00    | Play_Keyboard    | Play | 1      | Keyboard display             |
| 01    | Play_PatchDisp   | Play | 1      | Patch display                |
| 02    | Song_Write       | Song | 0Ch    | Song write/record            |
| 03    | Edit_PatchPrm    | Edit | 2      | Patch parameters             |
| 04    | Edit_Split       | Edit | 2      | Split point editing          |
| 05    | Edit_PatchMap    | Edit | 3      | Patch map                    |
| 06    | Edit_TonePrm    | Edit | 2      | Tone parameters              |
| 07    | Edit_Loop        | Edit | 2      | Loop point editing           |
| 08    | Edit_LFO         | Edit | 2      | LFO parameters               |
| 09    | Edit_TVF         | Edit | 4      | TVF envelope                 |
| 0A    | Edit_TVA         | Edit | 4      | TVA envelope                 |
| 0B    | Edit_ToneMap     | Edit | 5      | Tone map display             |
| 0C    | Edit_Delete      | Edit | Ah     | Delete tone/bank             |
| 0D    | Edit_CopyMove    | Edit | Ah     | Copy/Move                    |
| 0E    | Edit_DispWave    | Edit | Ah     | Wave display                 |
| 0F    | Disk_LoadSound   | Disk | 2      | Load sound set               |
| 10    | Disk_LoadSong    | Disk | Bh     | Load song                    |
| 11    | Disk_LoadTone    | Disk | Ah     | Load tone                    |
| 12    | Disk_DirPatch    | Disk | 2      | Directory: patches           |
| 13    | Disk_DirTone     | Disk | 2      | Directory: tones             |
| 14    | Disk_LabelSet    | Disk | 8      | Label set                    |
| 15    | Disk_SaveSound   | Disk | 2      | Save sound set               |
| 16    | Disk_SaveSong    | Disk | Bh     | Save song                    |
| 17    | Disk_Format      | Disk | 20h    | Format disk                  |
| 18    | Disk_DelSong     | Disk | 1Dh    | Delete song                  |
| 19    | Disk_SaveSys     | Disk | 20h    | Save system                  |
| 1A    | Disk_ChangeSys   | Disk | 2      | Change system                |
| 1B    | Func_Master      | Func | Ah     | Master settings              |
| 1C    | Func_Initialize  | Func | 2      | Initialize parameters        |
| 1D    | Midi_Message     | MIDI | 6      | MIDI message setup           |
| 1E    | Midi_Prog        | MIDI | 7      | MIDI program numbers         |
| 1F    | Midi_Monitor     | MIDI | Ah     | MIDI monitor                 |
| 20    | Tool_TimeCalc    | Tool | 1Bh    | Time calculator              |
| 21    | Song_SongName    | Song | 21h    | Song name editor             |
| 22    | Ptrn_Standard    | Ptrn | Dh     | Pattern editor (standard)    |
| 23    | Song_SongPrm     | Song | 14h    | Song parameters              |
| 24    | Ptrn_Microscope  | Ptrn | 15h    | Pattern editor (microscope)  |
| 25    | Tool_DiskCopy    | Tool | 1Ch    | Disk copy                    |
| 26    | Tool_Transfer    | Tool | 1Dh    | Transfer                     |
| 27    | Song_Initialize  | Song | 1Eh    | Song initialize              |
| 28    | Play_MutePlay    | Play | 1Fh    | Mute play                    |
| 29    | Play_ToneMap     | Play | 5      | Tone map (play mode)         |

## Window Structure (Director-S format)

The Director-S window structure is an extended version of the System 1.03 format:

```
window_struc {
    db field0       ; Window type/flags
    db menuLabels   ; Index → menu label string list
    db menuFunc     ; Index → menu function pointer table (FFFE-terminated addr[], FF-terminated bank[])
    db field3       ; Index: hi nybble → string list, lo nybble → pointer table
    db field3b      ; Sub-index or parameter variant
    db field3c      ; Sub-index or parameter variant
    dw prmLabelAddr ; Pointer to parameter label string list (FF-terminated)
    dw prmDataAddr  ; Pointer to param_data array (FFFE-terminated)
    db paramCount   ; Number of parameters / first param index
    db displayCfg   ; Display configuration byte
}
```

### Active Window Loading (load_activ_page)

`DirS.O1.load_activ_page` (04AB) copies window data from System2 bank:

1. Copies `window_struct` → `ActiveWindow` (6C8C)
2. Copies sub-structures consecutively to `winDataCopy` area (6CB8+)
3. Sets up pointer tables for function lists:
   - `f3_adrlist` → [6CA4], `f3_banklist` → [6CA6]
   - `f4_functbl[winindex]` → [6CA8] address, [6CAA] bank
   - `f5_adrlist` → [6CAC], `f5_banklist` → [6CAE]

## Code Comparison: System 1.03 vs Director-S

Director-S shares most of its core code with System 1.03, but with address offsets. Key function offset table:

### System Functions (common)

| Function          | S103    | DirS    | Delta  |
|-------------------|---------|---------|--------|
| init_display      | 4584    | 45BD    | +39    |
| sys_start         | 459C    | 45DF    | +43    |
| sa16_all_chn_on   | 48EC    | 4AA2    | +1B6   |
| sa16_all_chn_off  | 48FB    | 4AB1    | +1B6   |
| find_neg_sample   | 4946    | 4AFC    | +1B6   |
| get_AD_sample     | 4973    | 4B29    | +1B6   |
| init_mousereg     | 4F13    | 4EAA    | -69    |
| get_dial_input    | 4F35    | 4ECC    | -69    |
| rc100_receive     | 5066    | 50A2    | +3C    |
| RC100_keyprs      | 517C    | 51BD    | +41    |
| RC100_keyrel      | 51A1    | 51E2    | +41    |
| rc100_send        | 51C6    | 5207    | +41    |
| mouse_read        | 5684    | 57B0    | +12C   |
| strob_delay       | 56E3    | 580F    | +12C   |

### Bank 2 Functions (display)

| Function          | S103    | DirS    | Delta  |
|-------------------|---------|---------|--------|
| print_note_w      | 0670    | 05A8    | -C8    |
| print_tone_w      | 06CE    | 05FA    | -D4    |
| prn_patchname     | 0930    | 0842    | -EE    |
| prn_tonename      | 094F    | 0861    | -EE    |
| cmd_delbank       | 1BC2    | 1896    | -32C   |
| cmd_delalltones   | 1C00    | 18D4    | -32C   |
| cmd_copy          | 1C21    | 18F5    | -32C   |
| cmd_move          | 1D2F    | 1A03    | -32C   |

### Bank 3 Functions (sound engine)

| Function          | S103    | DirS    | Delta  |
|-------------------|---------|---------|--------|
| keyprs_handler    | 0E5A    | 0DA4    | -B6    |
| note_handler      | 0EA9    | 0DF6    | -B3    |

## Owner's Key Protection

Director-S includes an "Owner's Key" check that protects certain disk write operations:

### Check Logic (Overlay 7, @B4E)

```
if ($1FED bit 2 == 1):    // Key not inserted
    return 0 (fail)

if (current window in [Format, SaveSong, SaveSYS, DiskCopy, Transfer]):
    return 1 (allow)
else:
    return 0 (fail)
```

Protected windows (by index): 16h (SaveSong), 17h (Format), 19h (SaveSYS), 25h (DiskCopy), 26h (Transfer).

### Owner's Key References

| Location      | Description                           |
|---------------|---------------------------------------|
| OV1 @ 71C     | Owner key check                       |
| OV7 @ B6C     | Print "Check owner key" dialog        |
| OV7 @ B4E     | Validate owner's key                  |
| OV7 @ 16B7    | Call SY3.prn_ownrkey_lcd (92CA)       |

## Song Data Structures

Song data is stored on disk starting at offset 1B000+.

- HiBank#3 address 8550-86F0 contains song data in bank 6:300 format
- Song directory at disk offset 1B000: 128 entries × 30h bytes each
- Song name editing at window 21h (Song_SongName)

## LFO Parameter Example

The Edit_LFO window illustrates the parameter data system:

**Labels:** Rate, Sync, Mode, Delay, Offset, Polarity

**Parameter data:**
```
param_data < 669Fh, A4h, 13h >  ; area 4 (memory), offset 669F, type 13
param_data < 1Ch,   21h, 00h >  ; area 1, offset 1C, type 0 (Rate)
param_data < 1Dh,   21h, 15h >  ; area 1, offset 1D, type 15 (Sync)
param_data < 20h,   21h, 2Ah >  ; area 1, offset 20, type 2A (Mode)
param_data < 1Eh,   21h, 00h >  ; area 1, offset 1E, type 0 (Delay)
param_data < 23h,   21h, 17h >  ; area 1, offset 23, type 17 (Offset)
param_data < 22h,   21h, 25h >  ; area 1, offset 22, type 25 (Polarity)
```
