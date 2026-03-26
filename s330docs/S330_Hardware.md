# Roland S-330 Hardware Reference

## Overview

The Roland S-330 is a 16-voice polyphonic digital sampler. It receives on 8 MIDI channels in parallel.
Each MIDI channel is mapped to a _patch_. Up to two _tones_ can be assigned to a key via a patch.

## CPU

- **Processor:** Intel 8097-90 (MCS-96 family, 16-bit)
- 8 MHz main clock

### CPU Pin Assignments

| Pin     | Signal    | Description                    |
|---------|-----------|--------------------------------|
| HSI0    | ENVINT    | Envelope interrupt input       |
| HSO0    | MUTE      | Mutes all audio channels       |
| HSO3    | STROB     | Sample input strobe            |
| RxD     | MIDI In   | MIDI serial data in            |
| TxD     | MIDI Out  | MIDI serial data out           |
| ACH4    | (unknown) | Voltage offset?                |
| ACH7    | DAC out   | From DAC output                |

### Internal Registers

| Address       | Description                            |
|---------------|----------------------------------------|
| 0000-0017     | I/O registers                          |
| 0018 (word)   | Stack pointer                          |
| 001A-00EF     | General purpose registers              |
| 00F0-00FF     | Power-down RAM / Registers             |

### Port Registers

| Register | Description                                      |
|----------|--------------------------------------------------|
| 1FFE     | Port 3                                           |
| 1FFF     | Port 4                                           |

## Clock Sources

| Frequency (MHz) | Purpose                            |
|------------------|------------------------------------|
| 8.0              | CPU clock                          |
| 14.3496          | (unknown)                          |
| 20.0             | (unknown)                          |
| 24.0             | (unknown)                          |
| 26.880           | (unknown)                          |
| 13.7             | Filter clock                       |

## Memory Map

| Address Range | Size  | Description                          |
|---------------|-------|--------------------------------------|
| 0000-0017     |       | CPU internal I/O registers           |
| 0018          |       | Stack pointer                        |
| 001A-00EF     |       | CPU internal registers               |
| 00F0-00FF     |       | Power-down RAM/Registers             |
| 0100-1FFD     | ~8K   | RAM bank area (or ROM bank 0)        |
| 1FFE-1FFF     |       | CPU Port 3 & 4                       |
| 2000-3FFF     | 8K    | ROM                                  |
| 4000-7FFF     | 16K   | RAM (fixed, system area)             |
| 8000-BFFF     | 16K   | RAM (banked HiBank)                  |
| C000-FFFF     | 16K   | I/O area                             |

## Bank Switching (C600)

The S-330 has two separate sets of banked RAM, both selected via port C600:

```
C600 write format: -AAAA-BB
  AAAA = LoBank: 8K chunk (0100-1FFF), 8 banks + ROM
  BB   = HiBank: 16K chunk (4000-7FFF or 8000-BFFF)
```

### Physical RAM

- 4 x 32Kx8 SRAM chips, divided into 2 pairs forming 2 x 64Kx8 banks (wired as 32Kx16 for the 16-bit data bus)
- **Bank 1** (LoBank): A0-A12 wired to a1-a13, A13/A14 to BA/BB → 4 x 16K chunks
- **Bank 2** (HiBank): A0-A11 wired to a1-a12, A12-A14 to BD/BE/BF → 8 x 8K chunks
- BC pin is N/C (bit 2 in bank register appears unused)

### Bank Configurations

| C600 Value | LoBank (0100-1FFF) | HiBank (8000-BFFF) | Usage              |
|------------|--------------------|--------------------|---------------------|
| 00000011   | ROM                | -                  | ROM memory context  |
| 00001011   | RAM bank 1         | -                  | Overlay 1           |
| 00010011   | RAM bank 2         | -                  | Overlay 2           |
| 00011011   | RAM bank 3         | -                  | Overlay 3           |
| 00100011   | RAM bank 4         | -                  | Overlay 4           |
| 00101011   | RAM bank 5         | -                  | Overlay 5 / Chunk 3 |
| 0xxxx001   | -                  | Chunk 1 (4000-5FFF)| System bank H1      |
| 0xxxx010   | -                  | Chunk 2 (8000-A7FF)| System bank H2      |

- LoBank 0 (bank bits = 0000) maps the ROM to 0000-1FFF
- LoBanks 1-7 are 8K RAM overlays loaded from disk
- Utilities are loaded into LoBank 6 & 7

## I/O Map (C000-FFFF)

### SA-16 Sample Playback Chip

| Address | R/W | Description                                    |
|---------|-----|------------------------------------------------|
| C001    | W   | Channel select port (low byte)                 |
| C003    | W   | Channel select port (high byte)                |
| C005    | W   | Sample property port (low byte)                |
| C007    | W   | Sample property port (high byte)               |
| C009    | W   | Sample data port LSB (top nybble)              |
| C00B    | W   | Sample data port MSB                           |
| C00D    | W   | (unknown)                                      |
| C00F    | W   | (unknown)                                      |
| C011    | W   | Sample memory bank pointer (in 0x1000 portions). Bits 0-5 = bank, bits 6-7 = direction? |
| C809    | R/W | Sample data stream (low) — auto-increment address  |
| C80B    | R/W | Sample data stream (high) — auto-increment address |
| D001    | W   | SA-16 frequency register                       |
| D201    |     | SA-16 register bank end                        |
| E001    | W   | Sample chunk RAM address pointer (for C009/C00B)|

#### SA-16 Sample Configuration Registers

Sample configuration uses 32-register blocks starting at specified base addresses. Increase each address by 0x20 for consecutive channels.

To change a 16-bit sample property: write the register number, then write low byte to C005 and high byte to C007.

| Register Offset | Description                                   |
|-----------------|-----------------------------------------------|
| +01             | (unknown)                                     |
| +03             | Sample length (high?)                         |
| +05             | Sample length (low?)                          |
| +07             | Channel level (read from)                     |
| +09             | Bitfield: bank for loop start/end             |
| +0B             | Sample loop start                             |
| +0D             | Sample loop end                               |
| +0F             | (read from)                                   |
| +11             | (read from)                                   |
| +13 - +1F       | (unknown)                                     |

### I/O Control (C000)

| Bit | Name | Description                                       |
|-----|------|---------------------------------------------------|
| 0   | /RST | Reset                                             |
| 1   | RP   | Record/Play (defines usage of channel 1 output)   |
| 2   | BSY  | Busy signal to TVF                                |
| 3   | IL   | (unknown)                                         |

Writing bit 2 of data bus = 1 latches the RP signal.

### Floppy Disk Signals (C200)

**Read:**

| Bit | Name | Description                              |
|-----|------|------------------------------------------|
| 0   | DCHG | Disk change (0 = disk present)           |
| 1   | DRDY | Disk ready (1=open, 0=closed)            |
| 2   | INTQ | FDC command end/done                     |
| 3   | DRQ  | FDC data available (1 = data ready)      |

**Write:**

| Bit | Name  | Description              |
|-----|-------|--------------------------|
| 0   | SEL0  | Drive select 0           |
| 1   | SEL1  | Drive select 1           |
| 2   | SSEL  | Side select / INUSE      |

### LCD Controller - DM1620 (C300)

| Address | Description       |
|---------|-------------------|
| C300    | LCD register      |
| C302    | LCD data          |

### External Control Port - M60013 Gate Array (C400-C500)

The external control port supports Mouse, RC-100 controller, and DT-100 digitizer tablet.

| Address | R/W | Description                                 |
|---------|-----|---------------------------------------------|
| C400    | R/W | External control port data                  |
| C500    | W   | External control pin directions             |

**C400 Read format:**

| Bit | Description          |
|-----|----------------------|
| 0   | Data in              |
| 1   | Receive mode (0=write mode) |
| 4   | Data clock           |

**C400 Write format:**

| Bit | Description          |
|-----|----------------------|
| 5   | Data clock           |
| 6   | Data out             |

**Device-specific pin mapping:**

| Pin | RC-100  | Mouse     | DT-100   |
|-----|---------|-----------|----------|
| MX0 | DATA1   | Up        | /SENSE   |
| MX1 | /ATN    | Down      | EOC      |
| MX2 | GND     | Left      | SO       |
| MX3 | GND     | Right     | /SW      |
| MX4 | CLK1    | Left btn  | /SCK     |
| MX5 | CLK2    | Right btn | SI       |
| MX6 | DATA2   | Strobe    | /CS      |

### Bank Select (C600)

| Address | R/W | Description                              |
|---------|-----|------------------------------------------|
| C600    | W   | Memory bank toggle (see Bank Switching)  |

### Floppy Disk Controller - WD1772 (C800)

| Address | R/W | Description          |
|---------|-----|----------------------|
| C800    | R   | FDC status register  |
| C800    | W   | FDC command register |
| C802    | R/W | FDC track register   |
| C804    | R/W | FDC sector register  |
| C806    | R/W | FDC data register    |

### Video Display Processor - TMS3556 (D000)

| Address | R/W | Description          |
|---------|-----|----------------------|
| D000    | R   | VDP memory read      |
| D002    | W   | VDP memory write     |
| D004    | R/W | VDP register R/W     |

#### VDP Enable Lines

| E1 | E2 | RWM | Operation             |
|----|----|-----|-----------------------|
| 1  | 1  | x   | Inactive              |
| 0  | 1  | 1   | Read VDP reg (DB out) |
| 0  | 1  | ↓   | Write VDP reg (DB in) |
| 1  | 0  | ↓   | Write VDP mem (DB in) |
| 0  | 0  | 1   | Read VDP mem (DB out) |

#### VDP Registers

| Reg | Name  | Description              |
|-----|-------|--------------------------|
| 0   | RP    | Register pointer         |
| 1   | COL   | Column (lsb of address)  |
| 2   | ROW   | Row (msb of address)     |
| 3   | STAT  | Status                   |
| 4   | CM1   | Time base control        |
| 5   | CM2   | Decoder control          |
| 6   | CM3   | Mode/memory control      |
| 7   | CM4   | Full page attribute      |
| 8   | BAMT  | Buffer start             |
| 9   | BAMP  | CPU address register     |
| A   | BAPA  | Display start            |
| B   | BAGC0 | Character generator 0    |
| C   | BAGC1 | Character generator 1    |
| D   | BAGC2 | Character generator 2    |
| E   | BAGC3 | Character generator 3    |
| F   | BAMTF | Buffer end               |

Initialization: Write any value to CM1, then write CM2-CM4 to reset.

#### VDP Attribute Byte

| Bit   | Description                        |
|-------|------------------------------------|
| 7-5   | Foreground color (Blue, Green, Red)|
| 4-3   | Character generator (charset) #   |
| 2     | 0=normal, 1=inverted              |
| 1     | Height: 0=normal, 1=double        |
| 0     | Width: 0=normal, 1=double         |

Character byte bit 7: 0=normal, 1=flashing.

### Key Scan Port (D806)

| Address | R/W | Description                                |
|---------|-----|--------------------------------------------|
| D806    | W   | Key scan initiate (write 0 to start)       |
| D806    | R   | Key scan data (read twice for 2 rows of 6) |

#### Scan Matrix (active low: SS bits low, read SD port)

| Bit | SS0 (Row 0) | SS1 (Row 1) |
|-----|-------------|-------------|
| SD0 | MODE        | PAGE        |
| SD1 | MENU        | SUB         |
| SD2 | DEC         | Left        |
| SD3 | Up          | Down        |
| SD4 | INC         | Right       |
| SD5 | COM         | EXE         |
| SD6 | N/C         | N/C         |
| SD7 | N/C         | N/C         |

12 buttons total.

**Entering Monitor Mode:** Hold MODE + MENU + DEC + PAGE + SUB at boot.

### LED Control (D800) / Key Scan Port (D806)

| Address | R/W | Description                                |
|---------|-----|--------------------------------------------|
| D800    | W   | LED control register                       |
| D806    | W   | Key scan initiate (write 0 to start)       |
| D806    | R   | Key scan data (read twice for 2 rows of 6) |

### LED Port (F00C)

| Bit | LED       | Color |
|-----|-----------|-------|
| 0   | Disk      | Green |
| 1   | EXE       | Red   |
| 2   | COM       | Red   |
| 3   | SUB       | Red   |
| 4   | MENU      | Red   |
| 5   | PAGE      | Red   |
| 6   | MODE      | Red   |
| 7   | N/C       | -     |

0 turns the LED on, 1 turns it off.

### TVF - Time Variant Filter (MB654419U)

TVF registers are accessed via two 16-bit ports:

| Address | Description                              |
|---------|------------------------------------------|
| F000    | TVF register address (low byte)          |
| F002    | TVF register address (high byte)         |
| F004    | TVF data (low byte)                      |
| F006    | TVF data (high byte)                     |

Port F000/F002 acts as a register selector, F004/F006 as data.

#### Tone Register Offsets

Each tone has a base register offset. Tone 0 starts at offset 3, incrementing to 0x10 for tone 13, then wrapping to 1 and 2 for tones 14 and 15.

#### TVF Register Groups

| Base | Usage        | Port1 Value                                              |
|------|--------------|----------------------------------------------------------|
| 00h  | Cutoff       | Calculated (TVF EG + keyfollow + aftertouch)             |
| 10h  | (unknown)    | 0                                                        |
| 20h  | Resonance    | logtable[80h - resonance_param] / 4                      |
| 20h  | TVF Off      | 4000h                                                    |
| 40h  | (unknown)    | 0 or 3800h                                               |
| 60h  | (unknown)    | R2E                                                      |

Resonance = 0 (offset 80h into log table) yields the maximum value (FFFFh). Divided by 4 gives max numeric value of 4000h.

### Output Path (BU3905S Gate Array)

The output uses an analog mux/demux (4053) and a gate array (BU3905S) for output assignment.

```
DAC DO0-DO15 → Analog switches (3x 2-channel mux) → 8 output channels (CH1-CH8) with 14.5kHz filters
                                                    → 1 input channel with 13.7kHz filter
```

**4053 Analog Mux/Demux:**
- 3 switch bits (A, B, C) each select between DAC output (0) and ground/mute (1)
- /INH always low (always active)
- VDD = +8V, VSS = -8V

**BU3905S Gate Array:**
- Address lines: A0-A3 (from A1-A4)
- Data: D0-D7
- CH0-2: output channel group select
- XEN: inhibit
- AXI: SA-16 sample/hold
- AXO: input mux (gates sample input into comparator)
- QX0-7: enables individual channel for DAC output (one bit low at a time)

**ADC:** DAC feeds a comparator; result goes to successive approximation register (SAR) for sample input digitization.

### Floppy Disk Format

| Parameter     | Value                    |
|---------------|--------------------------|
| Bytes/sector  | 512 (0x200)              |
| Sectors/track | 9                        |
| Sides         | 2                        |
| Tracks        | 80 (0-79)               |
| Cylinder size | 0x2400 bytes             |

### Video Output Cable (8-pin)

| Pin | Signal | Wire Color |
|-----|--------|------------|
| 1   | +5V    | Gray       |
| 2   | GND    | Yellow     |
| 3   | N/C    | White      |
| 4   | H-Sync | Pink       |
| 5   | V-Sync | Brown      |
| 6   | Red    | Red        |
| 7   | Green  | Green      |
| 8   | Blue   | Blue       |

HC245 buffer: pin 6→H(4), pin 4→V(5).

## Disk Identifiers

| Version Byte | Disk Type            |
|--------------|----------------------|
| 01           | S-50 Sample          |
| 04           | S-550 Sample         |
| 05           | S-550 System         |
| 06           | S-550 DirectorS      |
| 08           | S-330 System         |
| 09           | S-330 Utility        |
| 0A           | S-330 DirectorS      |
| 0C           | S-550 CD/HD-5        |
| 10           | S-50 System          |
| 31           | S-770 System         |
| 32           | S-770 Sample         |

## ROM Monitor

The ROM contains a simple monitor program accessible at boot by holding MODE+MENU+DEC together with PAGE+SUB.

### Monitor Commands

| Command                | Description                                 |
|------------------------|---------------------------------------------|
| `Raaaacccc`            | Read memory (address aaaa, count cccc)      |
| `Waaaaccccdd..`        | Write memory (address aaaa, count cccc, data dd+) |
| `Gaaaa????`            | Go to address (execute from aaaa)           |
| `M`                    | Print monitor info                          |
| `Iaaaaccccvv`          | Fill memory (address aaaa, count cccc, value vv)  |

The monitor also accepts debug break via MIDI (send 0xFF) when bit 0 of R74 is 0.
