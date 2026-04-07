# Instructions for the reverse engineering and analysis of Roland MV-30 Studio M

## MAME driver

- MAME driver for the Roland MV-30 Studio M is located in src/mame/drivers/roland/roland_mv30.cpp.
- Documentation and information on the hardware and software is located in src/mame/drivers/roland/roland_mv30.md. Update this file with verified information as it is discovered.

## Ghidra (through MCP server)

Use the Ghidra project accessible through ghidra MCP plugin to document the disassembled firmware and system software. Use it to figure out how the hardware is accessed and used. The Ghidra project should be updated with new information as it is discovered. The following sections describe the conventions to follow when updating the Ghidra project.

When looking at the code, use decompiled code for speed, but verify the decompilation with disassembly for complex functions or when the decompilation looks incorrect.

### Memory layout

The Ghidra project contains the memory layout present immediately after the bootloader finishes and the main firmware starts executing. There are multiple overlays occupying the same address space. The banking system is explained below.

The MV-30 memory system is banked. The 64KB address space is divided into 4 windows of 16KB each (0000-3FFF,4000-7FFF,8000-BFFF,C000-FFFF).
The system ram is 512KB, upgradeable to 1MB, and is divided into 1KB pages, which can mapped to the 4 windows in a banked manner.
The system software (MV30.SYS) is loaded from disk on boot into pages 0x0A-0xA0 (151KB).
The system also distinguishes between read/write operations and execute operations. Different pages can be mapped for read/write and execute operations.
Special page numbers are used to map ROM (0x780) and memory-mapped I/O (0x474) into the address space.
The page numbers are set through registers, which are memory-mapped at addresses 0x100-0x10E. The mapping of page numbers to windows and types is as follows:

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

The memory layout is as follows:

| Name | Page Number | Window | Type | Description | Main/overlay space |
| ---- | ----------- | ------ | ---- | ----------- | ------------------ |
| SYSOL_P2F | 0x2F | 0000-3FFF | Execute | MV30.SYS offset 9400h | Main |
| ROM  | 0x780 | 0000-3FFF | Execute | | Overlay |
| SYSDT_P85 | 0x85 | 0000-3FFF | Data | MV30.SYS offset 1EC00h | Overlay |
| SYSOL_P3F | 0x3F | 4000-7FFF | Execute | MV30.SYS offset D400h | Main |
| SYSDT_PA1 | 0xA1 | 4000-7FFF | Data | Uninitialized RAM | Overlay |
| SYSOL_P0F | 0x0F | 8000-BFFF | Execute | MV30.SYS offset 1400h | Overlay |
| SYSOL_P2E | 0x2E | 8000-BFFF | Execute | MV30.SYS offset 9000h | Main |
| SYSOL_P4F | 0x4F | 8000-BFFF | Execute | MV30.SYS offset 11400h | Overlay |
| SYSDT_P1F | 0x1F | 8000-BFFF | Data | MV30.SYS offset 5400h | Overlay |
| SYSDT_P8F | 0x8F | 8000-BFFF | Data | MV30.SYS offset 21400h | Overlay |
| SYSDT_PB1 | 0xB1 | 8000-BFFF | Data | Uninitialized RAM | Overlay |
| SYSOL_P0A | 0x0A | C000-FFFF | Execute | MV30.SYS offset 0. System entry | Main |
| SYSOL_P53 | 0x53 | C000-FFFF | Execute | MV30.SYS offset 12400h | Overlay |
| SYSOL_P63 | 0x63 | C000-FFFF | Execute | MV30.SYS offset 16400h | Overlay |
| SYSOL_P73 | 0x73 | C000-FFFF | Execute | MV30.SYS offset 1A400h | Overlay |
| SYSOL_P83 | 0x83 | C000-FFFF | Execute | MV30.SYS offset 1E400h | Overlay |
| PERIP | 0x474 | C000-FFFF | Data | Peripherals and I/O | Overlay |

> Addresses in Ghidra referencing other memory regions should be annotated with memory references pointing to the correct overlay if they reference a memory that lies in overlay space. Otherwise they will map to the main memory space which may contain an incorrect bank.

### Code conventions

When annotating the disassembly, the following code conventions should be followed:

#### Functions

Functions should follow PascalCase naming convention. The prefix of the function name should indicate the module or subsystem it belongs to, if applicable. For example, functions related to audio processing might have the prefix "Audio", while functions related to MIDI handling might have the prefix "MIDI". If a function does not clearly belong to a specific module, it can be named based on its functionality. For example, a function that initializes the audio system might be named "AudioInitialize".

Functions should have a plate comment describing their purpose, parameters, and return value. The comment should be concise but informative enough to understand the function's role in the system.

> Note: When writing function names, ensure they go into overlays of type "Program". For 4000-7FFF that is SYSOL_P3F. The overlay type is not visible in Ghidra, use the table in the "Memory layout" section for reference. There might be multiple overlays of type "Program" for the same window, in that case choose the one that was recently mapped or the one that has a function entry point at the same address.



#### Function variable

Variables should follow camelCase naming convention. The name of the variable should be descriptive of its purpose.

Variable types should be annotated in the variable declaration. For example, if a variable is an integer, it should be declared as "int variableName". If the exact type is unknown, but the size is known, it can be declared as byte/word/dword variableName.

> Note: When writing function variables, ensure they go into overlays of type "Program". For 4000-7FFF that is SYSOL_P3F. The overlay type is not visible in Ghidra, use the table in the "Memory layout" section for reference. There might be multiple overlays of type "Program" for the same window, in that case choose the one that was recently mapped or the one that has a function entry point at the same address.

#### Global variables

Global variables should follow camelCase naming convention. Constants should be in uppercase with underscores. The name of the variable should be descriptive of its purpose.

Global variables should be annotated with their type and an end of line comment describing their purpose and usage. If the end of line comment is larger than 40 characters, a plate comment should be used instead.

Even if a variable cannot be named or typed with certainty, it should still be typed with size-based types (byte/word/dword) if it can be determined.

> Note: When writing global variables, ensure they go into overlays of type "Data". For 4000-7FFF that is SYSOL_PA1. The overlay type is not visible in Ghidra, use the table in the "Memory layout" section for reference.

#### Structures

Structures should follow PascalCase naming convention. The name of the structure should be descriptive of its purpose. For example, a structure representing an audio buffer might be named "AudioBuffer".

When creating a structure, fill in the comment field for the structure with a description of its purpose and usage. Each field in the structure should also have a comment describing its purpose and usage.

### When to update the Ghidra information

Present the changes you are about to make in the Ghidra project and ask for confirmation before applying them. Present the changes in a terse table format.

The annotations in Ghidra was made based on the information available at the time of the last update. Variable names could prove to be inaccurate as more information is discovered. Focus on annotating unknown functions (prefix FUN_) and variables (prefix DAT_) first. If a function or variable is already named but new information suggests a different name, it should be renamed to reflect the new information. However, if the existing name is still somewhat accurate and the new information does not provide a clear alternative, it can be left as is until more information is available.