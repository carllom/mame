/*****************************************************************************
 *
 *   nc4000dasm.c
 *   NC4000 and derivates
 *
 *   Chips in the family:
 *   Novix Inc.: NC4000, NC4016
 *   Harris Semiconductor: RTX2000
 *
 *****************************************************************************/
#include "emu.h"
#include "debugger.h"
#include "nc4000.h"

#define FETCHW(x) (oprom[x*2]<<8 | oprom[(x*2)+1])

#define APPEND_RET if (IS_RET) { flags |= DASMFLAG_STEP_OUT; buffer += sprintf(buffer, " ;"); }

typedef struct
{
   UINT16	opcode;		// Opcode value
   UINT16	mask;		// Opcode mask. Applied before comparison to opcode
   UINT8	length;		// Opcode length
   const char name[25];	// Opcode name
   unsigned flags;		// Disassembly flags
} opcodeinfo;

#define DOP_L5	1
#define DOP_R	2
#define DOP_L16	3

#define M_RET 0xFFDF
#define M_MRW 0xFFC0

static const opcodeinfo nc4000_opcodes[] =
{
		// ALU operations                     // tttw alu yy nrs shft
		{ 0100000, M_RET, 1, "NOOP",     0 }, // 1000 000 00 000 0000 ALU T
		{ 0100020, M_RET, 1, "NIP",      0 }, // 1000 000 00 001 0000 ALU T        POP
		{ 0107020, M_RET, 1, "DROP",     0 }, // 1000 111 00 001 0000 ALU N        POP
		{ 0107000, M_RET, 1, "DROP DUP", 0 }, // 1000 111 00 000 0000 ALU N
		{ 0100120, M_RET, 1, "DUP",      0 }, // 1000 000 00 101 0000 ALU T    N=T PSH
		{ 0107120, M_RET, 1, "OVER",     0 }, // 1000 111 00 101 0000 ALU N    N=T PSH
		{ 0107100, M_RET, 1, "SWAP",     0 }, // 1000 111 00 100 0000 ALU N    N=T PSH
		{ 0104020, M_RET, 1, "+",        0 }, // 1000 100 00 001 0000 ALU T+N      POP
		{ 0104220, M_RET, 1, "+c",       0 }, // 1000 100 01 001 0000 ALU T+Nc     POP
		{ 0106020, M_RET, 1, "-",        0 }, // 1000 110 00 001 0000 ALU T-N      POP
		{ 0106220, M_RET, 1, "-c",       0 }, // 1000 110 01 001 0000 ALU T-Nc     POP
		{ 0102020, M_RET, 1, "SWAP -",   0 }, // 1000 010 00 001 0000 ALU N-T      POP
		{ 0102220, M_RET, 1, "SWAP -c ", 0 }, // 1000 010 01 001 0000 ALU Nc-T     POP
		{ 0103020, M_RET, 1, "OR",       0 }, // 1000 011 00 001 0000 ALU T|N      POP
		{ 0105020, M_RET, 1, "XOR",      0 }, // 1000 101 00 001 0000 ALU T^N      POP
		{ 0101020, M_RET, 1, "AND",      0 }, // 1000 001 00 001 0000 ALU T&N      POP
		{ 0100001, M_RET, 1, "2/",       0 }, // 1000 000 00 000 0001 ALU T            >>
		{ 0100002, M_RET, 1, "2*",       0 }, // 1000 000 00 000 0010 ALU T            <<
		{ 0100003, M_RET, 1, "0<",       0 }, // 1000 000 00 000 0011 ALU T			   SX
		{ 0100011, M_RET, 1, "D2/",      0 }, // 1000 000 00 000 1001 ALU T			   D>>
		{ 0100012, M_RET, 1, "D2*",      0 }, // 1000 000 00 000 1010 ALU T			   D<<
		{ 0104411, M_RET, 1, "*'",       0 }, // 1000 100 10 000 1001 ALU T+MD         D<<
		{ 0102411, M_RET, 1, "*-",       0 }, // 1000 010 10 000 1001 ALU T-MD         D>>
		{ 0102412, M_RET, 1, "*F",       0 }, // 1000 010 10 000 1010 ALU T-MD         D<<
		{ 0102416, M_RET, 1, "/'",       0 }, // 1000 010 10 000 1110 ALU T-MD         Dd<<
		{ 0102414, M_RET, 1, "/\"",      0 }, // 1000 010 10 000 1100 ALU T-MD         Dd
		{ 0102616, M_RET, 1, "S'",       0 }, // 1000 010 11 000 1110 ALU T-SR         Dd<<

		//Return stack operators
		//
		// when reading N(alu input) is register value
		// SA flag is retstack?
		{ 0147321, M_RET, 1, "R>",       0 }, // 1100 111 011 0 10001 RGr reg  N=T PSH POPR reg=I  (  - x) (R:x -  )
		{ 0147301, M_RET, 1, "R@ (#I)",  0 }, // 1100 111 011 0 00001 RGr reg  N=T PSH      reg=I
		{ 0140721, M_RET, 1, "R> DROP",  0 }, // 1100 000 111 0 10001 RGr T            POPR reg=I
		{ 0157201, M_RET, 1, ">R",       0 }, // 1101 111 010 0 00001 RGw ???                      (x -  ) (R:  - x)
		{ 0157701, M_RET, 1, "R> SWAP >R",0}, // 1101 111 111 0 00001 RGw

		//Memory operations
		{ 0167100, M_RET, 1, "@",        0 }, // 1110 111 001 0 00000 MEr N    N=
		{ 0164000, M_RET, 1, "@ +",      0 }, // 1110 100 000 0 00000 MEr         (N
		{ 0164200, M_RET, 1, "@ +c",     0 }, // 1110 100 010 0 00000 MEr
		{ 0162000, M_RET, 1, "@ -",      0 }, // 1110 010 000 0 00000 MEr
		{ 0162200, M_RET, 1, "@ -c",    0 }, // 1110 010 010 0 00000 MEr
		{ 0166000, M_RET, 1, "@ SWAP -", 0 }, // 1110 110 000 0 00000 MEr N-T
		{ 0166200, M_RET, 1, "@ SWAP -c",0 }, // 1110 110 010 0 00000 MEr Nc-T
		{ 0163000, M_RET, 1, "@ OR",     0 }, // 1110 011 000 0 00000 MEr T|
		{ 0165000, M_RET, 1, "@ XOR",    0 }, // 1110 101 000 0 00000 MEr T^
		{ 0161000, M_RET, 1, "@ AND",    0 }, // 1110 001 000 0 00000 MEr T&
		{ 0177000, M_RET, 1, "!",        0 }, // 1111 111 000 0 00000 MEw N        POP wr(N,T)
		{ 0177100, M_RET, 1, "DUP !",    0 }, // 1111 111 001 0 00000 MEw N            wr(N,T)

		//Full literal fetch
		{ 0147500, M_RET, 2, "%04x (FLF)",         DOP_L16 }, // 1100 111 101 0 00000 RGr
		{ 0144400, M_RET, 2, "%04x + (FLF)",       DOP_L16 }, // 1100 100 100 0 00000 RGr
		{ 0144600, M_RET, 2, "%04x +c (FLF)",      DOP_L16 }, // 1100 100 110 0 00000 RGr
		{ 0142400, M_RET, 2, "%04x - (FLF)",       DOP_L16 }, // 1100 010 100 0 00000 RGr
		{ 0142600, M_RET, 2, "%04x -c (FLF)",      DOP_L16 }, // 1100 010 110 0 00000 RGr
		{ 0146400, M_RET, 2, "%04x SWAP - (FLF)",  DOP_L16 }, // 1100 110 100 0 00000 RGr
		{ 0146600, M_RET, 2, "%04x SWAP -c (FLF)", DOP_L16 }, // 1100 110 110 0 00000 RGr
		{ 0143400, M_RET, 2, "%04x OR (FLF)",      DOP_L16 }, // 1100 011 100 0 00000 RGr
		{ 0145400, M_RET, 2, "%04x XOR (FLF)",     DOP_L16 }, // 1100 101 100 0 00000 RGr
		{ 0141400, M_RET, 2, "%04x AND (FLF)",     DOP_L16 }, // 1100 001 100 0 00000 RGr

		{ 0164700, M_MRW, 1, "DUP @ SWAP %02x +",  DOP_L5 }, // 1110 100 111 0 xxxxx MEr T+L5 N=Mt PSH (a b - a+l Ma b)
		{ 0162700, M_MRW, 1, "DUP @ SWAP %02x -",  DOP_L5 }, // 1110 010 111 0 xxxxx MEr T-L5 -"-
		{ 0174700, M_MRW, 1, "SWAP OVER ! %02x +", DOP_L5 }, // 1111 100 111 0 xxxxx MEw T+L5      POP wr(N,T)
		{ 0172700, M_MRW, 1, "SWAP OVER ! %02x -", DOP_L5 }, // 1111 010 111 0 xxxxx MEw T-L5 -"-

		{ 0147100, M_MRW, 1, "%02x @",             DOP_L5 }, // 1100 111 001 0 xxxxx RGr
		{ 0144000, M_MRW, 1, "%02x @ +",           DOP_L5 }, // 1100 100 000 0 xxxxx RGr
		{ 0144200, M_MRW, 1, "%02x @ +c",          DOP_L5 }, // 1100 100 010 0 xxxxx RGr
		{ 0142000, M_MRW, 1, "%02x @ -",           DOP_L5 }, // 1100 010 000 0 xxxxx RGr
		{ 0142200, M_MRW, 1, "%02x @ -c",          DOP_L5 }, // 1100 010 010 0 xxxxx RGr
		{ 0146000, M_MRW, 1, "%02x @ SWAP -",      DOP_L5 }, // 1100 110 000 0 xxxxx RGr
		{ 0146200, M_MRW, 1, "%02x @ SWAP -c",     DOP_L5 }, // 1100 110 010 0 xxxxx RGr
		{ 0143000, M_MRW, 1, "%02x @ OR",          DOP_L5 }, // 1100 011 000 0 xxxxx RGr
		{ 0145000, M_MRW, 1, "%02x @ XOR",         DOP_L5 }, // 1100 101 000 0 xxxxx RGr
		{ 0141000, M_MRW, 1, "%02x @ AND",         DOP_L5 }, // 1100 001 000 0 xxxxx RGr
		{ 0157000, M_MRW, 1, "%02x !",             DOP_L5 }, // 1101 111 000 0 xxxxx RGw
		{ 0151100, M_MRW, 1, "DUP %02x !",         DOP_L5 }, // 1101 001 001 0 xxxxx RGw
		{ 0154000, M_MRW, 1, "DUP %02x ! +",       DOP_L5 }, // 1101 100 000 0 xxxxx RGw
		{ 0156000, M_MRW, 1, "DUP %02x ! -",       DOP_L5 }, // 1101 110 000 0 xxxxx RGw
		{ 0152000, M_MRW, 1, "DUP %02x ! SWAP -",  DOP_L5 }, // 1101 010 000 0 xxxxx RGw
		{ 0153000, M_MRW, 1, "DUP %02x ! OR",      DOP_L5 }, // 1101 011 000 0 xxxxx RGw
		{ 0155000, M_MRW, 1, "DUP %02x ! XOR",     DOP_L5 }, // 1101 101 000 0 xxxxx RGw
		{ 0151000, M_MRW, 1, "DUP %02x ! AND",     DOP_L5 }, // 1101 001 000 0 xxxxx RGw

		// Internal data fetch and store
		{ 0147300, M_MRW, 1, "%02x I@ (%s)",       DOP_R }, // 1100 111 011 0 xxxxx RGr
		{ 0144700, M_MRW, 1, "%02x I@ + (%s)",     DOP_R }, // 1100 100 111 0 xxxxx RGr
		{ 0142700, M_MRW, 1, "%02x I@ - (%s)",     DOP_R }, // 1100 010 111 0 xxxxx RGr
		{ 0146700, M_MRW, 1, "%02x I@ SWAP - (%s)",DOP_R }, // 1100 110 111 0 xxxxx RGr
		{ 0143700, M_MRW, 1, "%02x I@ OR (%s)",    DOP_R }, // 1100 011 111 0 xxxxx RGr
		{ 0145700, M_MRW, 1, "%02x I@ XOR (%s)",   DOP_R }, // 1100 101 111 0 xxxxx RGr
		{ 0141700, M_MRW, 1, "%02x I@ AND (%s)",   DOP_R }, // 1100 001 111 0 xxxxx RGr
		{ 0157200, M_MRW, 1, "%02x I! (%s)",       DOP_R }, // 1101 111 010 0 xxxxx RGw
		{ 0150300, M_MRW, 1, "%02x I! (%s)",       DOP_R }, // 1101 000 011 0 xxxxx RGw
		{ 0154200, M_MRW, 1, "DUP %02x I! + (%s)", DOP_R }, // 1101 100 010 0 xxxxx RGw
		{ 0156200, M_MRW, 1, "DUP %02x I! - (%s)", DOP_R }, // 1101 110 010 0 xxxxx RGw
		{ 0152200, M_MRW, 1, "DUP %02x I! SWAP - (%s)",DOP_R}, // 1101 010 010 0 xxxxx RGw
		{ 0153200, M_MRW, 1, "DUP %02x I! OR (%s)",DOP_R }, // 1101 011 010 0 xxxxx RGw
		{ 0155200, M_MRW, 1, "DUP %02x I! XOR (%s)",DOP_R}, // 1101 101 010 0 xxxxx RGw
		{ 0151200, M_MRW, 1, "DUP %02x I! AND (%s)",DOP_R}, // 1101 001 010 0 xxxxx RGw
		{ 0157700, M_MRW, 1, "%02x I@! (%s)",      DOP_R }, // 1101 111 111 0 xxxxx RGw
		{ 0157500, M_MRW, 1, "%02x (SLF)",         DOP_L5 }, // 1101 111 101 0 xxxxx RGw
		{ 0154400, M_MRW, 1, "%02x + (SLF)",       DOP_L5 }, // 1101 100 100 0 xxxxx RGw
		{ 0154600, M_MRW, 1, "%02x +c (SLF)",      DOP_L5 }, // 1101 100 110 0 xxxxx RGw
		{ 0152400, M_MRW, 1, "%02x - (SLF)",       DOP_L5 }, // 1101 010 100 0 xxxxx RGw
		{ 0152600, M_MRW, 1, "%02x -c (SLF)",      DOP_L5 }, // 1101 010 110 0 xxxxx RGw
		{ 0156400, M_MRW, 1, "%02x SWAP - (SLF)",  DOP_L5 }, // 1101 110 100 0 xxxxx RGw
		{ 0156600, M_MRW, 1, "%02x SWAP -c (SLF)", DOP_L5 }, // 1101 110 110 0 xxxxx RGw
		{ 0153400, M_MRW, 1, "%02x OR (SLF)",      DOP_L5 }, // 1101 011 100 0 xxxxx RGw
		{ 0155400, M_MRW, 1, "%02x XOR (SLF)",     DOP_L5 }, // 1101 101 100 0 xxxxx RGw
		{ 0151400, M_MRW, 1, "%02x AND (SLF)",     DOP_L5 },  // 1101 001 100 0 xxxxx RGw

		{ 0, 0, 0, "", 0 }
};

static const char * const reg_trf[] = { "JK", "I", "PC", "true", "MD", "MD",
		"SR", "SR", "Bdata", "Bmask", "Bdir", "Btri", "Xdata", "Xmask", "Xdir",
		"Xtri","illegal","illegal","illegal","illegal","illegal","illegal","illegal","illegal"
		,"illegal","illegal","illegal","illegal","illegal","illegal","illegal","illegal"};

CPU_DISASSEMBLE( nc4000 ) {
	UINT16 op = FETCHW(0), op2 = FETCHW(1);
	offs_t p = 1, flags = DASMFLAG_SUPPORTED;
	int opidx;
	const opcodeinfo *opinfo;

	if (op & 0x8000) {
		switch (op & 0xF000) {
		case 0x9000: // Branch on T != 0
			buffer += sprintf(buffer, "0BRANCH %04x", BADDR(pc));
			break;
		case 0xA000: // Loop on I > 0
			buffer += sprintf(buffer, "LOOP %04x", BADDR(pc));
			break;
		case 0xB000: // Unconditional branch
			buffer += sprintf(buffer, "BRANCH %04x", BADDR(pc));
			break;
		default:
			opidx = 0;
			while (nc4000_opcodes[opidx].opcode != 0)
			{
				opinfo = &nc4000_opcodes[opidx];
				if ((op & opinfo->mask) == opinfo->opcode)
				{
					if (opinfo->flags == DOP_L5)
					{
						buffer += sprintf(buffer, opinfo->name, OP_5BIT);
					}
					else if (opinfo->flags == DOP_R)
					{
						buffer += sprintf(buffer, opinfo->name, OP_4BIT, reg_trf[OP_4BIT]);
					}
					else if (opinfo->flags == DOP_L16)
					{
						buffer += sprintf(buffer, opinfo->name, op2);
					}
					else
					{
						buffer += sprintf(buffer, "%s", opinfo->name);
					}
					p = opinfo->length;
					APPEND_RET;
					break;
				}
				opidx++;
			};
			if (nc4000_opcodes[opidx].opcode == 0)
			{
				buffer += sprintf(buffer, "*** RAW: %04x ***", op);
			}
			break;
		}
	} else // Subroutine call
	{
		sprintf(buffer, "CALL %04x", op);
		flags |= DASMFLAG_STEP_OVER;
	}
	return p | flags;
}
