Title: cpu/mcs96: Fix MULUB (indexed, 3-operand) and DIV/DIVU dividend handling

Branch: mcs96-fixes (worktree /home/carl/source/mame-pr)

These arithmetic bugs in the MCS-96 core were found while bringing up the Roland S-330, whose firmware relies on 32-by-16-bit division.

**MULUB, indexed, 3 operands (opcode 5F)**
The fetched operand was overwritten rather than multiplied, so the result was just the second operand zero-extended. The other MULUB forms were already correct.

**DIV and DIVU, all addressing modes**
The high word of the 32-bit dividend was ORed into the low word instead of being shifted into bits 16–31, so any dividend of 64K or more gave a wrong quotient and remainder.

**DIV (signed) remainder and overflow**
The remainder was computed with the dividend treated as unsigned. That also converted a negative divisor to unsigned, so any negative operand gave a wrong remainder; DIVB already does this correctly. The division is now done in 64 bits as well, because `0x80000000 / -1` is undefined behaviour in 32 bits and raises SIGFPE on x86 hosts. It now sets V/VT instead.

Reference: Intel *80C196KB User's Guide*, order number 270651-003 (November 1990).
- Table 3-1A, Instruction Summary (p. 15): DIV/DIVU store the quotient in D and the remainder in D+2 (`D ← (D,D+2)/A, D+2 ← remainder`), and only V/VT are affected. Three-operand MULB/MULUB is `D,D+1 ← B × A`.
- Opcode table (p. 18): 5F is the indexed form of three-operand MULUB, and 8C–8F / FE 8C–8F are DIVU / DIV.

Intel *16-Bit Embedded Controller Handbook* (1991), MCS-96 Instruction Set:
- 24. DIV (p. 3-14): "divides the contents of the destination LONG-INTEGER operand by the contents of the INTEGER word operand, using signed arithmetic"; low word ← (DEST)/(SRC), high word ← (DEST) MOD (SRC).
- 26. DIVU (p. 3-15): the same with unsigned arithmetic and a DOUBLE-WORD destination.
- 69. MULUB, three operands (p. 3-36): (DEST) ← (SRC1) × (SRC2), unsigned, with a WORD result.

Tested: DEBUG=1 build, `-validate`, and S-330 firmware on roland_s50.cpp `s330`. A standalone check of quotient, remainder and V against a reference covered signed and unsigned operands, including overflow.
