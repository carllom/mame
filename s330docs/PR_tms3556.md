Title: video/tms3556: Implement serial attributes (delimiters, masking), CM4 trailing bytes; fix scanline smear

Branch: tms3556-fixes (worktree /home/carl/source/mame-pr)

These fixes to the TMS3556 VDP were made while bringing up the Roland S-330, whose display relies heavily on serial attributes. Each commit is one logical change and can be reviewed on its own. `git diff -w` helps with the delimiter commit, which re-indents an existing block.

References below are to the *TMS3556 Family Users Manual* (Texas Instruments / M. Goddard, 22.03.84).

1. **Remove broken line doubling.** The copy targeted `bmp.pix(line, 1)`, which is column 1 of the same row rather than another row, and its length was in bytes rather than pixels. The result was the left half of every scanline shifted one pixel to the right.
2. **Alphanumeric/mosaic select for character generator 3 comes from DC5 (CM2 bit 3)**, not CM4 (Table 3.6, §4.2.4).
3. **Trailing bytes are loaded into CM4.** The 121st byte of a bitmap line, or the 81st byte of a text row in mixed mode, is copied into CM4 at the end of each line or row (§4.3.2, §4.4.1).
4. **Delimiters and serial background (§4.2.4–4.2.6).**
   - Character code >20 in any generator is a delimiter.
   - A delimiter cell is drawn in its BF/GF/RF colour, and BB/GB/RB set the background up to the next delimiter or the end of the row.
   - Alpha-mosaic characters carry their background forward in the same way.
   - The start of each row acts like a delimiter with the attributes in CM4, and the margin always uses the CM4 colour.
   - The zone state is kept per scanline, since every scanline of a row re-scans the same name table entries.
5. **Masking (§4.2.4, Figure 4.13; Table 4.5).** With DC3 (CM2 bit 5) set, characters in a masked zone are displayed as spaces in the zone's background colour, and a delimiter ending a masked zone takes that zone's background colour. CM4's MR bit gives the initial state for each row.
6. **525-line mode (BT2, CM1 bit 6, Table 3.4).** Selects 21 rows / 210 active lines instead of 25 / 250 (§4.2.1, §4.3). *Decision pending: the manual's standard 525-line timing is 32 columns; see notes.*

Tested: DEBUG=1 build and `-validate`; roland_s50.cpp `s330` <before/after>; exelv.cpp `exl100` <regression check>.

Notes, open questions (not addressed by this PR):
- The manual's bit layouts disagree with the emulation in three places: CG1/CG0 (Table 4.2), DH/DW (Figure 4.4) and delimiter MSK/INC (Figure 4.12). The current order was presumably chosen to match software running on real hardware, so it is left unchanged.
- Not implemented: DC2 (row 0 suppression), underlining (DC7/LR/UNL), grid (DC6), incrustation, MR in bitmap mode.

Next steps before submitting (as of 2026-09-24):
1. Build both branches with DEBUG=1, run `-validate`, test `s330` (and `exl100` if ROMs are available). The corrected masking commit has not been compiled yet.
2. Check the VDP chip marking on the S-330 board. If it is a Roland-specific variant, drop commit 6 (BT2) from this PR and add it later as a separate device type (compare tms9928a.h and saa5050.h).
3. Determine which attribute byte the S-330 firmware writes for masked delimiters (0x08 or 0x10) to settle the bit 3/4 question.
4. Optional separate PR: add the v3.00 firmware dump as a BIOS option for `s330` in roland_s50.cpp.
