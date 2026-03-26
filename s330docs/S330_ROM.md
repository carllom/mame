# Roland S-330 ROM Reference

## Overview

The ROM occupies address 2000-3FFF (8K) and, when banked in as LoBank 0, also provides code at 0100-1FFF. It contains the BIOS-level functionality: VDP graphics, LCD control, floppy disk I/O, key scanning, SA-16 initialization, bank switching, and a built-in monitor.

Author: Carl Lom / 2014+

## ROM Layout

| Address Range | Description                          |
|---------------|--------------------------------------|
| 0100-0192     | VDP API vectors (49 entry points)    |
| 0193-0FFF     | VDP ROM code                         |
| 1000-1EFF     | CRT character generator font data    |
| 2000-207F     | Interrupt vectors                    |
| 2080          | Reset entry point                    |
| 2100-23FF     | ROM monitor                          |
| 2400-24E6     | ROM function API vectors (77 entries)|
| 24E7-35D3     | ROM function code                    |
| 35D4-3613     | LCD extra font characters            |
| 3800-39BF     | Valid disk header template           |
| 39C0-3B41     | (tables, unknown)                    |
| 3BC0-3F40     | Logarithmic tables (for TVF etc.)    |

## Memory Accesses by ROM

### Read-Only Data (in banked system RAM at 4000+)

| Address     | Description                                    |
|-------------|------------------------------------------------|
| 4004        | System entry point — called after disk load    |
| 4030        | Wave R/W callback — called when changing track |
| 4033        | Format callback — called when changing track   |
| 4036        | Called from VDP functions                       |
| 4060-407F   | KeyOn key code table (4 rows × 8 bits)         |
| 4080-409F   | KeyOff key code table (4 rows × 8 bits)        |

### Read/Write Data

| Address     | Description                              |
|-------------|------------------------------------------|
| 6000-600F   | Raw key value table                      |
| 6010-601F   | Previous key value table                 |
| 6020-602F   | KeyOn table                              |
| 6030-603F   | KeyOff table                             |
| 609A-609E   | VDP-related system variables             |
| 7A00-7BFF   | Floppy disk sector buffer (512 bytes)    |

### Registers Used

| Register | Type | Description                        |
|----------|------|------------------------------------|
| R00      | word | ZERO_REG — always zero             |
| R07      | byte | SERBUF — serial buffer             |
| R08      | byte | INT_MASK — interrupt mask          |
| R09      | byte | INT_PEND — interrupt pending       |
| R0E      | byte | BAUD_REG — baud rate               |
| R0F      | byte | IOPORT1 — I/O port 1              |
| R10      | byte | IOPORT2 — I/O port 2              |
| R11      | byte | SER_CTRL — serial control          |
| R14      | byte | WSR — window selection register    |
| R18      | word | SP — stack pointer                 |
| R1A-R22  |      | Monitor-only temp vars             |
| R24      | byte | Numeric parameter / bank number    |
| R32      | word | Character delta (vdp_w_deltchr)    |
| R34      | word | Font address / sector buffer ptr   |
| R36      | word | Source address (CG copy / load)    |
| R4C      | word | Pixel X coordinate (start)         |
| R4E      | word | Pixel Y coordinate (start)         |
| R50      | word | Pixel X coordinate (end)           |
| R52      | word | Pixel Y coordinate (end)           |
| R58      | word | VDP memory offset                  |
| R5B      | byte | VDP register number                |
| R5C      | word | Data pointer (strings, buffers)    |
| R66      | byte | Number of rows (tile fill)         |
| R6C      | byte | Tile G / FDC status                |
| R6D      | byte | Tile R                             |
| R72      | byte | CurrBank (0bBBBBBHH: B=bank, H=hibank) |
| R74      | byte | Raw keypress values from panel     |
| R75      | byte | LCDConfig1: move direction, shift  |
| R76      | byte | LCDConfig2: display, cursor, blink |
| R77      | byte | LCD RAM address                    |
| R78      | byte | LCD Column                         |
| R79      | byte | LCD Row                            |
| R82      | bits | bit0: LCD page (0=right, 1=left); bit1: LCD-page related |
| R84      | bits | bit2: keypress available           |
| R8A      | byte | VDP Column                         |
| R8B      | byte | VDP Row                            |
| R8C      | byte | VDP Attribute (color + chargen)    |
| R8D      | byte | VDP Cursor Attribute               |
| R8E      | byte | Current Character / integer value  |
| R8F      | byte | Current Key pressed                |
| R92      | byte | VDP position delta (pixel ops)     |
| R94      | word | CallAddr — bank call address       |
| R97      | byte | Pixel color[2:0] (B=2, G=1, R=0)  |
| R98      | byte | VDP byte read/write data           |
| R9E      | byte | RAM start wave (0-17). ×3 for bytes |
| R9F      | byte | Number of waves to load/save       |
| RA0      | byte | Disk start wave (0-35)             |
| RA1      | byte | Wave bank # (0-1)                  |
| RA2      | byte | Drive number                       |
| RA3      | byte | Disk side                          |
| RA4      | byte | Disk track                         |
| RA5      | byte | Disk sector                        |
| RA7      | byte | Copy of FDC STATUS                 |
| RA8      | byte | Boot retry counter                 |
| RA9      | byte | IOCTL copy                         |
| RAC      | byte | Count / format parameter           |
| RB8      | word | MidiTxTail                         |
| RBA      | word | MidiTxHead                         |
| RBC      | word | MidiRxTail                         |
| RBE      | word | MidiRxHead                         |

## Boot Process

```
ROM:2557 load_system:
  1. Clear INTMEM_A7
  2. Select drive 0
  3. Force FDC interrupt
  4. FDC restore (recalibrate)
  5. Seek to track 79 (last track)
  6. Seek to track 0 (first track)
  7. Load boot sector (track 0)
  8. Load system program
  9. Validate (check INTMEM_A7 error bits)
  10. Jump to RAM_4004 (system entry)
```

On failure, retries up to the count specified in R_A8.

## VDP API (0100-0192)

49 vectored entry points for the TMS3556 video display processor.

### VDP Init & Memory

| # | Vector | Impl   | Name              | In                                          | Out        | Description                                           |
|---|--------|--------|-------------------|---------------------------------------------|------------|-------------------------------------------------------|
| 0 | 0100   | 0193   | vdp_init          | —                                           | —          | Initialize VDP. Sets registers, loads CG data into all 4 char generators |
| 1 | 0103   | 01AB   | vdp_setupmem      | —                                           | —          | Configure VDP base address registers (BAMT, BAMTF, BAMP, BAPA, BAGC0-3) |
| 2 | 0106   | 021D   | vdp_copyCgData    | R58W=DestAddr, R36W=SourceAddr              | —          | Copy CG data to VDP RAM. 80 chars × 10 bytes/char    |
| 3 | 0109   | 0242   | vdp_w_dblreg      | R24=Register, R25=DataRegA, R26=DataRegB    | —          | Write double VDP register                             |
| 4 | 010C   | 0257   | vdp_w_baseadr     | R58W=Offset, R5B=Register                   | —          | Write VDP base address register                       |
| 5 | 010F   | 027E   | vdp_set_offset    | R58W=Offset                                 | —          | Set VDP memory access offset                          |
| 6 | 0112   | 0296   | vdp_w_byte        | R58W=Offset, R98=Data                       | —          | Write byte to VDP at offset                           |
| 7 | 0115   | 02B4   | vdp_r_byte        | R58W=Offset                                 | R98=Data   | Read byte from VDP at offset                          |
| 8 | 0118   | 02D1   |                   |                                             |            |                                                       |
| 9 | 011B   | 02DD   |                   |                                             |            |                                                       |

### Character Conversion

| # | Vector | Impl   | Name              | In                                          | Out                    | Description                                |
|---|--------|--------|-------------------|---------------------------------------------|------------------------|--------------------------------------------|
| 10| 011E   | 02EA   | to_uppercase      | R8E=character                               | R8E=uppercase char     | If < 0x20: set to space. If >= 0x60: subtract 0x20 |
| 11| 0121   | —      | (nullsub)         | —                                           | —                      | No-op                                      |
| 12| 0124   | —      | (nullsub)         | —                                           | —                      | No-op                                      |
| 13| 0127   | 0302   |                   |                                             |            |                                                       |
| 14| 012A   | 0309   |                   | R8A=Col, R8B=Row, RAC=?                     |            |                                                       |
| 15| 012D   | 0322   |                   |                                             |            |                                                       |
| 16| 0130   | 032C   |                   |                                             |            |                                                       |
| 17| 0133   | 0336   |                   |                                             |            |                                                       |
| 18| 0136   | 0355   |                   |                                             |            |                                                       |

### Screen Fill/Clear

| # | Vector | Impl   | Name              | In                                          | Out        | Description                                           |
|---|--------|--------|-------------------|---------------------------------------------|------------|-------------------------------------------------------|
| 19| 0139   | 0371   | vdp_fillscr       | —                                           | —          | Fill entire screen with blank chars (CG0/White). 21 rows |
| 20| 013C   | 0382   | vdp_clearscr      | VdpRow=start row                            | —          | Clear screen from VdpRow downward. CG0/White          |
| 21| 013F   | 03BC   | vdp_t_fillrows    | R25=TileB, R6C=TileG, R6D=TileR, R66=NumRows | —       | Fill tile rows with specified RGB tile values          |
| 22| 0142   | 0461   | vdp_t_clrsplit    | —                                           | —          | Fill tilemap area of split screen (bottom 8 rows) with white |

### Number/String Conversion

| # | Vector | Impl   | Name              | In                                          | Out                                       | Description                     |
|---|--------|--------|-------------------|---------------------------------------------|-------------------------------------------|---------------------------------|
| 23| 0145   | 04CA   | nbr2str           | R24=byte value                              | R26=hundreds, R25=tens, R24=ones (ASCII)  | Byte to 3-digit decimal string  |
| 24| 0148   | 04F8   | byte2hex          | R24=byte value                              | R24=lo nybble hex, R25=hi nybble hex      | Byte to 2-char hex string       |

### Screen Position

| # | Vector | Impl   | Name              | In                                          | Out                    | Description                                |
|---|--------|--------|-------------------|---------------------------------------------|------------------------|--------------------------------------------|
| 25| 014B   | 051F   | pos2off           | R8A=Col, R8B=Row                            | R58W=VDP memory offset | Offset = 1400h + row×82 + col×2            |

### Character Printing

| # | Vector | Impl   | Name              | In                                          | Out           | Description                                      |
|---|--------|--------|-------------------|---------------------------------------------|---------------|--------------------------------------------------|
| 26| 014E   | 0532   | vdp_prnchr_clmp   | R8A=Col, R8B=Row, R8C=Attrib, R8E=Char     | [Updated R8A] | Print char with screen wrapping/clamping         |
| 27| 0151   | 0546   | vdp_prnchr_ws     | R8A=Col, R8B=Row, R8C=Attrib, R8E=Char     | [Updated R8A] | Print char, whitespace mapped to char 0          |
| 28| 0154   | 054D   | vdp_prnchr        | R8A=Col, R8B=Row, R8C=Attrib, R8E=Char     | [Updated R8A] | Print char at position. Increments column        |
| 29| 0157   | 0570   | vdp_prnchr_v_cl   | R8E=intvalue                                | [Updated R8B] | Print char vertically with row clamp (max 22)    |
| 30| 015A   | 0578   | vdp_prnchr_v      | R8A=Col, R8B=Row                            | [Updated R8B] | Print char vertically, decrements col, increments row |

### Number Printing

| # | Vector | Impl   | Name              | In                                          | Out  | Description                                           |
|---|--------|--------|-------------------|---------------------------------------------|------|-------------------------------------------------------|
| 31| 015D   | 057F   | vdp_prnint        | R8E=intvalue                                | —    | Print integer (0-255) with leading zero suppression   |
| 32| 0160   | 05BC   | vdp_prnint_fmt    | R8E=intvalue, RAC=format (--S+nnnn)         | —    | Print formatted int. S=sign, +=1-based, nnnn=digits. Handles negative with '-' prefix |
| 33| 0163   | 0608   | vdp_prnhex        | R8E=intvalue, R8A=Col, R8B=Row, R8C=Attrib | [Updated R8A] | Print byte as 2-digit hex                      |

### Cursor

| # | Vector | Impl   | Name              | In                                          | Out  | Description                                           |
|---|--------|--------|-------------------|---------------------------------------------|------|-------------------------------------------------------|
| 34| 0166   | 0623   | vdp_prncurs_fls   | R8A=Col, R8B=Row                            | —    | Print flashing cursor (char 0xA0)                     |
| 35| 0169   | 0638   | vdp_prncurs       | R8A=Col, R8B=Row                            | —    | Print normal cursor (space char 0x20)                 |
| 36| 016C   | 064D   |                   |                                             |      |                                                       |
| 37| 016F   | 07A8   |                   |                                             |      |                                                       |
| 38| 0172   | 0816   |                   |                                             |      |                                                       |

### Drawing Functions

| # | Vector | Impl   | Name              | In                                               | Out   | Description                                       |
|---|--------|--------|--------------------|--------------------------------------------------|-------|---------------------------------------------------|
| 39| 0175   | 0870   | vdp_w_deltchr     | R32=char delta                                   | —     | Print char with delta offset (CurrChar += word_32)|
| 40| 0178   | 0880   |                   |                                                  |       |                                                   |
| 41| 017B   | 0A30   | vdp_clamp_xypix   | R4C=x, R4E=y                                    | [Clamped to 0:0 – 319:209] | Clamp pixel coords to screen bounds |
| 42| 017E   | 0A52   | vdp_setpixel      | R4CW=x, R4E=y, R92=VDP pos delta, R97=color[2:0] | —   | Set pixel. color: bit2=B, bit1=G, bit0=R         |
| 43| 0181   | 0AE4   | vdp_drawline      | R4C=startX, R4E=startY, R50=endX, R52=endY, R92=pos delta, R97=color[2:0] | — | Draw line using fixed-point DDA algorithm |

### Specialized Display

| # | Vector | Impl   | Name              | In  | Out  | Description                                           |
|---|--------|--------|-------------------|-----|------|-------------------------------------------------------|
| 44| 0184   | 0B94   | vdp_keydisp       | —   | —    | Draw 8 lines of keyboard graphics with A-H labels. Uses gfx_keyboard tile data |
| 45| 0187   | 0CBD   | vdp_wavescope     | —   | —    | Draw waveform scope: bordered frame with measurement grid, 64 data rows |

### Sample RAM Operations

| # | Vector | Impl   | Name              | In                                               | Out  | Description                                       |
|---|--------|--------|--------------------|--------------------------------------------------|------|---------------------------------------------------|
| 46| 018A   | 0E64   | smp_clear         | [609A]=wave# start, [609B]=bank#, [609E]=# waves | —    | Clear sample RAM. Max 18 waves. 3×0x1000 samples/wave |
| 47| 018D   | 0EB5   | smp_copy          | [609A]=src wave#, [609B]=src bank#, [609C]=dst wave#, [609D]=dst bank#, [609E]=# waves | — | Copy sample data between wave slots. Max 18 waves |
| 48| 0190   | 0F31   | smp_reverse       | [609A]=src wave#, [609B]=src bank#, [609C]=dst wave#, [609D]=dst bank#, [609E]=# waves | — | Reverse sample data. Reads source backwards, writes dest forwards |

## ROM Function API (2400-24E6)

77 vectored entry points for system-level functions.

### Boot / System Loading

| # | Vector | Impl   | Name              | In                                | Out                    | Description                                        |
|---|--------|--------|-------------------|-----------------------------------|------------------------|----------------------------------------------------|
| 0 | 2400   | 2548   | boot_system       | —                                 | —                      | Try to boot disk. 3 retries. Drives FDC restore, seeks track 0→79→0, loads bootsect + sysprog |
| 1 | 2403   | 25DC   | load_sysprog      | —                                 | RA7=FDC status accum.  | Load system: 0x2000 bytes → $4000-5FFF, 0x2800 bytes → $8000-A7FF, then overlays into banks 1-5 |
| 2 | 2406   | 26C9   | load_overlay      | DiskTrk=starting track            | —                      | Load overlay: 0x1EFE bytes → $100-1FFE in current bank |
| 3 | 2409   | 2718   | load_bootsect     | —                                 | —                      | Load + validate system disk boot sector. Checks 4-byte header signature. Displays version/copyright on LCD+VDP |

### SA-16 Sound Chip

| # | Vector | Impl   | Name              | In                                | Out  | Description                                             |
|---|--------|--------|-------------------|-----------------------------------|------|---------------------------------------------------------|
| 4 | 240C   | 2861   | sa16_init         | —                                 | —    | Initialize SA-16. Disable all 16 channels, clear all internal registers up to D201h |

### Hi-Bank Switching

| # | Vector | Impl   | Name              | In                                | Out  | Description                                             |
|---|--------|--------|-------------------|-----------------------------------|------|---------------------------------------------------------|
| 5 | 240F   | 289D   | set_hibank1       | —                                 | —    | Set 16K bank to 1 (CurrBank bits 0-2 = 001)            |
| 6 | 2412   | 28A9   | set_hibank2       | —                                 | —    | Set 16K bank to 2 (CurrBank bits 0-2 = 010)            |
| 7 | 2415   | 28B5   | set_hibank3       | —                                 | —    | Set 16K bank to 3 (CurrBank bits 0-2 = 011)            |

### Bank Switching & Execution

| # | Vector | Impl   | Name              | In                                | Out  | Description                                             |
|---|--------|--------|-------------------|-----------------------------------|------|---------------------------------------------------------|
| 8 | 2418   | 28C1   | set_bank          | R24=Bank#                         | —    | Shift bank# left by 3, OR into CurrBank, write BANKSEL |
| 9 | 241B   | 28D0   | bank0_exec        | R94w=Address                      | —    | Push CurrBank, switch to bank 0, call [CallAddr], restore bank |
| 10| 241E   | 28E7   | bank1_exec        | R94w=Address                      | —    | Execute function in Bank 1                              |
| 11| 2421   | 28FE   | bank2_exec        | R94w=Address                      | —    | Execute function in Bank 2                              |
| 12| 2424   | 2915   | bank3_exec        | R94w=Address                      | —    | Execute function in Bank 3                              |
| 13| 2427   | 292C   | bank4_exec        | R94w=Address                      | —    | Execute function in Bank 4                              |
| 14| 242A   | 2943   | bank5_exec        | R94w=Address                      | —    | Execute function in Bank 5                              |
| 15| 242D   | 295A   | bank6_exec        | R94w=Address                      | —    | Execute function in Bank 6                              |
| 16| 2430   | 2971   | bank7_exec        | R94w=Address                      | —    | Execute function in Bank 7                              |
| 17| 2433   | 2988   | bank_exec         | R94w=Address, R24=Bank# (8=current) | —  | Execute function in specified bank                      |
| 18| 2436   | 29A8   | bank_jmp_fnc      | R94w=Address                      | —    | Execute function in current bank via [CallAddr]         |

### Key Scanning

| # | Vector | Impl   | Name              | In                                | Out                       | Description                                    |
|---|--------|--------|-------------------|-----------------------------------|---------------------------|-------------------------------------------------|
| 19| 2439   | 29AA   | scan_keys         | —                                 | —                         | Scan 2 key rows from KEYPORT. Computes key-on/off via XOR with previous state |
| 20| 243C   | 29EC   | scan_keys_x4      | —                                 | R84 (bit2=0 if no key)    | Scan keys 4 times, set R84.2 if any key detected |
| 21| 243F   | 2A0B   | keycode           | —                                 | R8F=Keycode (FF=none)     | Get keycode of pressed/released key. Release has precedence. Uses keycode_keyon[]/keycode_keyoff[] |
| 22| 2442   | —      | (nullsub)         | —                                 | —                         | No-op                                           |

### VDP String/Data Functions

| # | Vector | Impl   | Name              | In                                                      | Out              | Description                                    |
|---|--------|--------|--------------------|--------------------------------------------------------|------------------|------------------------------------------------|
| 23| 2445   | 2A77   | vdp_r_chars       | R8A=Col, R8B=Row, RAC=Length                            | R5Cw=Buffer      | Read characters from VDP via pos2off + loop    |
| 24| 2448   | 2A96   | vdp_wdata         | R5C=VDPdata [offset, count, data+]                      | —                | Write every-other byte (attribs or chars) to VDP |
| 25| 244B   | 2AAE   | vdp_fillattrib    | @VDPColumn, @VDPRow, @VDPAttrib, RAC=Count              | —                | Fill VDP attribute bytes at position for count chars |
| 26| 244E   | 2AC7   | vdp_prnstr        | R5Cw=String [col, row, attrib, chars..., 0xFE]          | [Updated R5C]    | Print string to VDP. FF col = end. Updates bufptr for chaining |
| 27| 2451   | 2AD5   | vdp_prnstrpos     | @VDPColumn, @VDPRow, @VDPAttrib, R5Cw=String            | —                | Print string at position. Space → char 0. Terminates on 0xFE |
| 28| 2454   | 2B1E   | vdp_prnstr_v      | R5Cw=String [col, row, attrib, chars..., 0xFE]          | —                | Print vertical string with embedded coordinates |
| 29| 2457   | 2B2C   | vdp_prnstrp_v     | @VDPRow, @VDPAttrib, R5Cw=String                        | —                | Print vertical string at position              |
| 30| 245A   | —      | (nullsub)         | —                                                       | —                | No-op                                          |
| 31| 245D   | —      | (nullsub)         | —                                                       | —                | No-op                                          |

### FDC - Floppy Disk Controller

| # | Vector | Impl   | Name              | In                                                      | Out              | Description                                    |
|---|--------|--------|--------------------|--------------------------------------------------------|------------------|------------------------------------------------|
| 32| 2460   | 2B40   | fdc_wait_cmdrdy   | —                                                       | —                | Busy-wait until FDC status bit 0 clear         |
| 33| 2463   | 2B49   | fdc_waitnodsk     | —                                                       | —                | Busy-wait until DISKSIG bit 0 set (no disk)    |
| 34| 2466   | 2B52   | fdc_wait_intr     | —                                                       | R24=FDC status   | Wait for FDC interrupt (DISKSIG bit 2), read status |
| 35| 2469   | 2B60   | fdc_restore       | —                                                       | —                | Send RESTORE (0x00) to FDC, wait for completion |
| 36| 246C   | 2B6F   | fdc_forceint      | —                                                       | R24=FDC status   | Send FORCE INTERRUPT (0xD8) to FDC             |
| 37| 246F   | 2B7B   | tbl_drvsel        | —                                                       | —                | Drive select lookup table (2 bytes, not a function) |
| 38| 2472   | 2B7D   | fdc_seldrive      | @DriveNbr, @DiskSide                                    | —                | Select drive/side. Uses tbl_drvsel, ORs with side bit, writes DISKSIG |
| 39| 2475   | 2B99   | fdc_seek_trk      | @DiskTrk                                                | R6C=FDC status   | Seek to track. Writes track to FDC_DATA, issues SEEK (0x10) |
| 40| 2478   | 2BB3   | clamp_trksect     | RA4=Track#, RA5=Sector#                                 | RA4, RA5 clamped | Clamp track 0-79, sector 1-9                   |
| 41| 247B   | 2BD4   | fdc_rd_sect       | @DiskTrk, @DiskSec                                      | RA7=FDC status   | Read sector to $7A00. READ_SECTOR (0x80). Byte-by-byte DRQ loop |
| 42| 247E   | 2C17   | fdc_wr_sect       | @DiskTrk, @DiskSec                                      | RA7=FDC status   | Write sector from $7A00. WRITE_SECTOR (0xA0)   |
| 43| 2481   | 2C5A   | read_sector       | RA2=Drive#, RA3=Side, RA4=Track#, RA5=Sector#           | Updated RA3-RA5, R34=BufPtr | Read sector + auto-advance (wraps sector→side→track) |
| 44| 2484   | 2C81   | write_sector      | RA2=Drive#, RA3=Side, RA4=Track#, RA5=Sector#           | Updated RA3-RA5, R34=BufPtr | Write sector + auto-advance              |
| 45| 2487   | 2CA8   | fdc_rd_waves      | RA0=Wave#, R9F=NumWaves, R9E=DestSample#, RA1=DestBank# | RA7=FDC status  | Read wave data from disk. 12-bit packed (3 bytes → 2 samples). Starts at track 8, 2 tracks/wave. Streams to SA-16 ports |
| 46| 248A   | 2D9E   | fdc_wr_waves      | RA0=TargetWave#, R9F=NumWaves, R9E=SourceSample#, RA1=SourceBank# | RA7=FDC status | Write wave data to disk. Inverse of fdc_rd_waves |
| 47| 248D   | 2EA8   | fdc_format_disk   | —                                                       | —                | Format 80 tracks × 2 sides × 9 sectors. Different sector sizes for tracks 0-7 vs 8+ |
| 48| 2490   | 2FAD   | fdc_wr_fill       | R25=Value, R34=Count                                    | RA7=FDC status   | Write fill bytes to FDC during format          |
| 49| 2493   | 2FC1   | fdc_wr_fill2      | R25=Value, R34=Count                                    | RA7=FDC status   | Write fill inner loop                          |
| 50| 2496   | 2FCF   | fdc_init_disk     | —                                                       | —                | Initialize disk: write F9 FF FF + zeroes to sectors 0:2 and 0:5 (FAT markers) |

### LCD Functions

| # | Vector | Impl   | Name              | In                                                      | Out  | Description                                    |
|---|--------|--------|--------------------|--------------------------------------------------------|------|------------------------------------------------|
| 51| 2499   | 3396   | lcd_init          | —                                                       | —    | Init LCD: FUNC_SET (2 rows, 8bit, 5×7), clear, increment, display on, cursor off |
| 52| 249C   | 33C5   | lcd_wait_10us     | —                                                       | —    | LCD register settle delay (~10μs, 14 iterations) |
| 53| 249F   | 33CC   | lcd_wait_500us    | —                                                       | —    | LCD clear delay (~500μs, 10×74 iterations)     |
| 54| 24A2   | 33D9   | lcd_clrscr        | —                                                       | —    | Clear LCD                                      |
| 55| 24A5   | 33E4   | lcd_curs_home     | —                                                       | —    | Move LCD cursor + screen to origin             |
| 56| 24A8   | 33EF   | lcd_curs_inc      | —                                                       | —    | Set cursor move direction: increment           |
| 57| 24AB   | 33FD   | lcd_curs_dec      | —                                                       | —    | Set cursor move direction: decrement           |
| 58| 24AE   | 340B   | lcd_scroll_on     | —                                                       | —    | Enable LCD display shift                       |
| 59| 24B1   | 3419   | lcd_scroll_off    | —                                                       | —    | Disable LCD display shift                      |
| 60| 24B4   | 3427   | lcd_disp_on       | —                                                       | —    | LCD display on                                 |
| 61| 24B7   | 3435   | lcd_disp_off      | —                                                       | —    | LCD display off                                |
| 62| 24BA   | 3443   | lcd_curs_on       | —                                                       | —    | LCD cursor visible                             |
| 63| 24BD   | 3451   | lcd_curs_off      | —                                                       | —    | LCD cursor hidden                              |
| 64| 24C0   | 345F   | lcd_blink_on      | —                                                       | —    | LCD cursor blink on                            |
| 65| 24C3   | 346D   | lcd_blink_off     | —                                                       | —    | LCD cursor blink off                           |
| 66| 24C6   | 347B   | lcd_curs_l        | —                                                       | —    | Move cursor left (cmd 0x10)                    |
| 67| 24C9   | 3486   | lcd_curs_r        | —                                                       | —    | Move cursor right (cmd 0x14)                   |
| 68| 24CC   | 3491   | lcd_scroll_l      | —                                                       | —    | Scroll screen left (cmd 0x18)                  |
| 69| 24CF   | 349C   | lcd_scroll_r      | —                                                       | —    | Scroll screen right (cmd 0x1C)                 |
| 70| 24D2   | 34A7   | lcd_loadromfont   | —                                                       | —    | Load default ROM font into LCD CGRAM (64 bytes from lcdfont) |
| 71| 24D5   | 34AB   | lcd_loadfont      | R34=FontAddr                                            | —    | Load custom font into LCD CGRAM. 64 bytes      |
| 72| 24D8   | 34C8   | lcd_set_adr       | @LCDRAMAddr                                             | —    | Set LCD RAM address (bit 7 set, write LCD_REG) |
| 73| 24DB   | 34D6   | lcd_set_char      | @LCDColumn, @LCDRow, R8E=Char                          | —    | Write char to LCD. Address = col + row×64      |
| 74| 24DE   | 34F0   | lcd_prnstr        | R5Cw=String [col, row, chars..., 0xFE]                  | —    | Print string to LCD                            |
| 75| 24E1   | 34F6   | lcd_prnstrpos     | @LCDColumn, @LCDRow, R5Cw=String                       | —    | Print string to LCD at position. Terminates on 0xFE |
| 76| 24E4   | 351B   | lcd_from_vdp      | @LCDColumn, @LCDRow, @VDPColumn, @VDPRow, RAC=NumChars | —    | Copy VDP to LCD. Translates: 0x1C→CGRAM0 (→), 0x1D→CGRAM1 (←), 0x15→CGRAM2 (±) |

## Internal ROM Functions (not in jump tables)

### Boot Helpers

| Address | Name              | In                                | Out  | Description                                    |
|---------|-------------------|-----------------------------------|------|------------------------------------------------|
| 26F3    | load_2sect        | DiskTrk=track, R36=TargetAddr    | —    | Load 2 sectors (0x400 / 1KB) into address      |
| 27D8    | vdp_label_value   | R5C=Label w. pos, R2Aw=16-bit val| —    | Print label string then 16-bit hex value       |
| 27E8    | prnstr            | R5C=string w. pos                 | —    | Simple wrapper around vdp_prnstr               |

### TVF (Time-Variant Filter)

| Address | Name              | In                                | Out  | Description                                    |
|---------|-------------------|-----------------------------------|------|------------------------------------------------|
| 313D    | tvf_init          | —                                 | —    | Initialize TVF. Writes 64 records (3 bytes each) to filter table, then sets registers via sub_324F |
| 324F    | tvf_write16       | R28w=Base address, R2Ew=Data      | —    | Write 16-bit data to 16 consecutive TVF addresses. Writes to F000/F002 (addr) and F004/F006 (data) with 240μs delay |
| 327B    | wait_240us        | —                                 | —    | Wait 240μs                                     |

### RC-100 Serial Interface

| Address | Name              | In                                | Out                  | Description                                    |
|---------|-------------------|-----------------------------------|----------------------|------------------------------------------------|
| 3282    | rc100_init        | —                                 | —                    | Init RC-100 serial. Set bits 4,5 of C500h as output, clear EXT_CTRL |
| 3290    | rc100_rcv         | —                                 | R24=byte, Carry=1 ok | Serial receive. Checks ATN, clocks in 8 bits. Carry=0 if not ready |
| 330B    | rc100_snd_byte    | R24=Data byte                     | Carry=0              | Bit-bang 8 bits via EXT_CTRL port bits 5 (clk) and 6 (data) |
| 3389    | rc100_snd         | R24=Data byte                     | Carry=0 ok, Carry=1 busy | Send with ready check. Checks ATN before sending |

### Serial Monitor

| Address | Name              | In                                | Out                  | Description                                    |
|---------|-------------------|-----------------------------------|----------------------|------------------------------------------------|
| 6890    | mon_recv          | —                                 | R1A=received byte    | Wait for serial byte (SER_CTRL bit 6)          |
| 6897    | m_rcv_hexbyte     | —                                 | R1A=hex byte value   | Receive 2 ASCII hex chars, combine into byte   |
| 6916    | mon_emit          | R1A=Character                     | —                    | Send character via serial. Waits for CTS, 200μs delay |
| 6937    | m_emit_hexbyte    | R1A=Value                         | —                    | Send byte as 2 ASCII hex characters            |

## ROM Data Structures

### Interrupt Vectors (2000-207F)

The ROM interrupt vector table starts at 2000. After system load, the system program installs its own vectors at 4008-4028.

### Boot Sector (7A00)

| Offset | Description        |
|--------|--------------------|
| 7A04   | Identifier         |
| 7A20   | Version string     |

### Character Generator Data

| Address Range | Description              |
|---------------|--------------------------|
| 0FFF-14FF     | CG Data 0                |
| 14FF-19FF     | CG Data 1                |
| 19FF+         | CG Data 3                |
| 35D4-3613     | LCD font (extra chars)   |

### Log Tables (3BC0-3F40)

Used by TVF resonance calculations. Resonance parameter is an index into this table; result is divided by 4 for the TVF register value. Maximum table value is 0xFFFF, so maximum TVF numeric value is 0x4000.

### Level Curves (3B40-3E3F)

Array of 6 `level_curve` structs (128 bytes each). Velocity response curves.

### Disk Header Template (3800-39BF)

Template data for a valid system disk header, used during boot sector validation.

### LFO Delay Table (3E40-3F3F)

128 words — LFO delay time lookup table.

## VDP Memory Layout

Derived from ROM code (vdp_setupmem, pos2off, tile drawing):

| Area            | Base Address | Row Width | Description                              |
|-----------------|-------------|-----------|------------------------------------------|
| Character data  | 0x1400      | 82 bytes  | Text/attribute area, 40 cols × 21 rows   |
| Tile data       | 0x1828      | 122 bytes | Split-screen tiles (keyboard/wavescope)  |
| Char gen CG0    | 0xFFFE      |           | Character generator 0                    |
| Char gen CG1    | 0x04FF      |           | Character generator 1                    |
| Char gen CG2    | 0x09FF      |           | Character generator 2                    |
| Char gen CG3    | 0x0EFF      |           | Character generator 3                    |

- **Row format**: 40 chars × 2 bytes (attribute + character) + 2 bytes padding = 82 bytes/row
- **Tile row format**: 3 bytes/tile (B, G, R planes) × 40 tiles + 2 bytes control = 122 bytes/row
- **Pixel resolution**: 320 × 210 (clamped to 0-319 × 0-209)
- **String terminator**: 0xFE

### VDP Attribute Enum (VdpAttr)

| Bitmask | Field          | Value | Name       |
|---------|----------------|-------|------------|
| 0x18    | CharGen select | 0x00  | CG0        |
|         |                | 0x08  | CG1        |
|         |                | 0x10  | CG2        |
|         |                | 0x18  | CG3        |
| 0xE0    | Color          | 0x00  | Black      |
|         |                | 0x20  | Red        |
|         |                | 0x40  | Green      |
|         |                | 0x60  | Yellow     |
|         |                | 0x80  | Blue       |
|         |                | 0xA0  | Magenta    |
|         |                | 0xC0  | Cyan       |
|         |                | 0xE0  | White      |

### ROM Data Type Structs

```
lcd_coord {
    db col          ; LCD column
    db row          ; LCD row
}

vdp_coord {
    db col          ; VDP column
    db row          ; VDP row
}

level_curve {
    db velArray[128] ; Velocity curve array
}
```

## Key Code Tables

Key code translation tables reside at 4060-409F in system RAM (loaded from disk).

### Key Codes

| ON   | OFF  | Key          | Source     |
|------|------|--------------|------------|
| 1B   | -    | MODE         | Panel 0:0  |
| 1C   | -    | MENU         | Panel 0:1  |
| 1D   | -    | SUB          | Panel 1:1  |
| 1E   | -    | COM          | Panel 0:5  |
| 1F   | -    | EXE          | Panel 1:5  |
| 85   | -    | PAGE         | Panel 1:0  |
| 90   | 98   | Right        | Panel 1:4  |
| 91   | 99   | Left         | Panel 1:2  |
| 92   | 9A   | Up           | Panel 0:3  |
| 93   | 9B   | Down         | Panel 1:3  |
| 94   | 9C   | INC          | Panel 0:4  |
| 95   | 9D   | DEC          | Panel 0:2  |
| 96   | 9E   | RMB          | Mouse      |
| 97   | 9F   | LMB          | Mouse      |
| 10-13| -    | Mouse dirs   | Mouse      |
| 60   | -    | Mouse stop   | Mouse      |
| 80   | -    | REC          | RC-100     |
| 81   | -    | START/STOP   | RC-100     |
| 82-84| -    | F1-F3        | RC-100     |
| 88-8D| -    | PLAY-UTIL    | RC-100     |
| 30-39| -    | 0-9 (alpha)  | RC-100     |
| 0D   | -    | ENTER        | RC-100     |
| 19   | -    | INS          | RC-100     |
| 1A   | -    | DEL          | RC-100     |

## Hacker Mode

An undocumented diagnostic mode accessible in the S-330 System v1.03 utility disk:

1. Boot with UTIL disk inserted
2. Navigate: Mode → ↓↓↓ → Execute
3. Navigate: ↓ → Execute → Execute
4. Navigate: Menu → Dec/No → Submenu
5. Screen shows "hacker mode tone map"
6. Navigate: Mode → ↓↓↓↓↓ → Execute
7. Additional UTIL menu choices become available

(Source: Dennis Barton, CompuServe, September 1993)
