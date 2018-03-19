/*****************************************************************************
 *
 *   ns32016dasm.c
 *   NS32016
 *
 *****************************************************************************/

#include "emu.h"
#include "debugger.h"
#include "ns32016.h"

#define INS_BAS(x) (oprom[x+2]<<16 | oprom[x+1]<<8 | oprom[x])
#define INS_OP8(x) (oprom[x])
#define INS_OP16(x) (oprom[x]<<8 | oprom[x+1])
#define INS_OP32(x) (oprom[x]<<24 | oprom[x+1]<<16 | oprom[x+2]<<8 | oprom[x+3])

#define INSTYPE_MASK  0x0F
#define INSGRP16_MASK 0xF0
#define INSGRP24_MASK 0xFF

#define FMT0_COND(x) ((x >> 4) & 0xF)

#define FMT1_OP      ((ins >> 4) & 0xF)

#define FMT2_I       (ins & 0x3)
#define FMT2_OP      ((ins >> 4) & 0x7)
#define FMT2_SHRT    ((ins >> 7) & 0xF)
#define FMT2_SHRT_SX (INT32)((ins & 0x0400 ? 0xFFFFFFF0 : 0) | FMT2_SHRT)
#define FMT2_GEN     ((ins >> 11) & 0x1F)

#define FMT3_I       (ins & 0x3)
#define FMT3_OP      ((ins >> 7) & 0xF)
#define FMT3_GEN     ((ins >> 11) & 0x1F)

#define FMT4_I       (ins & 0x3)
#define FMT4_OP      ((ins >> 2) & 0xF)
#define FMT4_GEN2    ((ins >> 6) & 0x1F)
#define FMT4_GEN1    ((ins >> 11) & 0x1F)

#define FMT5_I       ((ins >> 8) & 0x3)
#define FMT5_OP      ((ins >> 10) & 0xF)
#define FMT5_SHRT    ((ins >> 15) & 0xF)
#define FMT5_T       (ins & 0x8000)
#define FMT5_B       (ins & 0x10000)
#define FMT5_UW_NONE ((ins & 0x60000) == 0)
#define FMT5_UW_W    ((ins & 0x60000) == 0x20000)
#define FMT5_UW_U	 ((ins & 0x60000) == 0x60000)

#define FMT6_I       ((ins >> 8) & 0x3)
#define FMT6_OP      ((ins >> 10) & 0xF)
#define FMT6_GEN2    ((ins >> 14) & 0x1F)
#define FMT6_GEN1    ((ins >> 19) & 0x1F)

#define FMT7_I       ((ins >> 8) & 0x3)
#define FMT7_OP      ((ins >> 10) & 0xF)
#define FMT7_GEN2    ((ins >> 14) & 0x1F)
#define FMT7_GEN1    ((ins >> 19) & 0x1F)

static const char * const op_areg[] = {
		"US", "[res]", "[res]", "[res]", "[res]", "[res]", "[res]", "[res]",
		"FP", "SP", "SB", "[res]", "[res]", "PSR", "INTBASE", "MOD"
		};

offs_t op_disp(INT32 &disp, offs_t pc, const UINT8 *oprom, offs_t off)
{
	switch((INS_OP8(off) >> 5) & 7) // Two MSB bits + sign bit
	{
	case 0: // 8-bit pos
	case 1:
		disp = INS_OP8(off);
		return 1;
	case 2: // 8-bit neg
	case 3:
		disp = (INT8)(INS_OP8(off) | 0x80); // Propagate sign bit
		return 1;
	case 4: // 16-bit pos
		disp = INS_OP16(off) & 0x3FFF;
		return 2;
	case 5: // 16-bit neg
		disp = (INT16)(INS_OP16(off) | 0xC000);
		return 2;
	case 6: // 32-bit pos
		disp = INS_OP32(off) & 0x3FFFFFFF;
		return 4;
	case 7: // 32-bit neg
		disp = INS_OP32(off);
		return 4;
	default:
		return 0;
	}

}

// Illegal/unknown instruction (generates TRAP)
offs_t ins_unk(UINT32 ins, char **buffer, offs_t pc, const UINT8 *oprom)
{
	return 1;
}

static const char * const mnem_cond[] = {
		"EQ", "NE", "CS", "CC", "H" , "LS", "GT", "LE",
		"FS", "FC", "LO", "HS", "LT", "GE", "R", "RN"
		};

static const char * const mnem_width[] = { "B", "W", "[error]", "D" };

offs_t ins_gen(int genCode, int width, char ** buffer, offs_t pc, const UINT8 *oprom, offs_t off)
{
	INT32 disp1 = 0, disp2 = 0;
	offs_t ic = 0;
	switch(genCode)
	{
	case 0:
		*buffer += sprintf(*buffer, "R0");
		break;
	case 1:
		*buffer += sprintf(*buffer, "R1");
		break;
	case 2:
		*buffer += sprintf(*buffer, "R2");
		break;
	case 3:
		*buffer += sprintf(*buffer, "R3");
		break;
	case 4:
		*buffer += sprintf(*buffer, "R4");
		break;
	case 5:
		*buffer += sprintf(*buffer, "R5");
		break;
	case 6:
		*buffer += sprintf(*buffer, "R6");
		break;
	case 7:
		*buffer += sprintf(*buffer, "R7");
		break;
	case 8:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R0)", disp1);
		break;
	case 9:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R1)", disp1);
		break;
	case 10:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R2)", disp1);
		break;
	case 11:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R3)", disp1);
		break;
	case 12:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R4)", disp1);
		break;
	case 13:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R5)", disp1);
		break;
	case 14:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R6)", disp1);
		break;
	case 15:
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(R7)", disp1);
		break;
	case 16: // Memory relative
		ic += op_disp(disp1, pc, oprom, off+ic);
		ic += op_disp(disp2, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(%i(FP))", disp2, disp1);
		break;
	case 17: // Memory relative
		ic += op_disp(disp1, pc, oprom, off+ic);
		ic += op_disp(disp2, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(%i(SP))", disp2, disp1);
		break;
	case 18: // Memory relative
		ic += op_disp(disp1, pc, oprom, off+ic);
		ic += op_disp(disp2, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(%i(SB))", disp2, disp1);
		break;
	case 19:
		*buffer += sprintf(*buffer, "[reserved]");
		break;
	case 20: // Immediate
		if (width == 0)
		{
			*buffer += sprintf(*buffer, "#%02X", INS_OP8(off+ic));
			ic += 1;
		}
		else if (width == 1)
		{
			*buffer += sprintf(*buffer, "#%04X", INS_OP16(off+ic));
			ic += 2;
		}
		else if (width == 3)
		{
			*buffer += sprintf(*buffer, "#%08X", INS_OP32(off+ic));
			ic += 4;
		}
		break;
	case 21: // Absolute
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "$%06X", disp1);
		break;
	case 22: // External
		ic += op_disp(disp1, pc, oprom, off+ic);
		ic += op_disp(disp2, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "EXT(%i) + %i", disp1, disp2);
		break;
	case 23: // Top of stack
		*buffer += sprintf(*buffer, "TOS");
		break;
	case 24: // Memory space
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(FP)", disp1);
		break;
	case 25: // Memory space
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(SP)", disp1);
		break;
	case 26: // Memory space
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "%i(SB)", disp1);
		break;
	case 27: // Memory space
		ic += op_disp(disp1, pc, oprom, off+ic);
		*buffer += sprintf(*buffer, "* + %i", disp1);
		break;
	case 28: // Scaled index
	case 29: // Scaled index
	case 30: // Scaled index
	case 31: // Scaled index
		*buffer += sprintf(*buffer, "Scaled index not implemented");
		break;
	}

	return ic;
}

// Branches
offs_t ins_format_0(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	INT32 disp;
	offs_t ic = 1; // 1-byte base instruction

	// Bx disp dest
	buffer += sprintf(buffer, "B%s ", mnem_cond[(ins & 0xF0)>>4]);
	ic += op_disp(disp, pc, oprom, ic);
	buffer += sprintf(buffer, "%i ($%06X)", disp, pc + disp);

	return ic;
}

offs_t ins_format_1(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	INT32 disp;
	UINT8 rmask;
	offs_t ic = 1; // 1-byte base instruction

	switch (FMT1_OP)
	{
	case 0: // BSR disp dest
		ic+=op_disp(disp, pc, oprom, ic);
		buffer+=sprintf(buffer,"BSR %i ($%06x)", disp, pc + disp);
		ic+=DASMFLAG_STEP_OVER;
		break;
	case 1: // RET disp constant
		ic+=op_disp(disp, pc, oprom, ic);
		buffer+=sprintf(buffer,"RET %i", disp);
		break;
	case 2: // CXP disp constant
		ic+=op_disp(disp, pc, oprom, ic);
		buffer+=sprintf(buffer,"CXP %i", disp);
		ic+=DASMFLAG_STEP_OVER;
		break;
	case 3: // RXP disp constant
		ic+=op_disp(disp, pc, oprom, ic);
		buffer+=sprintf(buffer,"RXP %i", disp);
		break;
	case 4: // RETT disp constant [all]
		ic+=op_disp(disp, pc, oprom, ic);
		buffer+=sprintf(buffer,"RETT %i", disp);
		break;
	case 5: // RETI [all]
		buffer+=sprintf(buffer,"RETI");
		break;
	case 6: // SAVE imm reglist
		buffer+=sprintf(buffer,"SAVE [");
		rmask = INS_OP8(ic);
		ic++;
		for (int ri=0; ri<8; ri++)
		{
			if (rmask & 1)
				buffer+=sprintf(buffer,"R%i ", ri);
			rmask >>= 1;
		}
		buffer+= sprintf(buffer,"]");
		break;
	case 7: // RESTORE imm reglist
		buffer+=sprintf(buffer,"RESTORE [");
		rmask = INS_OP8(ic);
		ic++;
		for (int ri=7; ri>=0; ri--)
		{
			if (rmask & 1)
				buffer+=sprintf(buffer,"R%i ", ri);
			rmask >>= 1;
		}
		buffer+= sprintf(buffer,"]");
		break;
	case 8: // ENTER imm reglist, disp constant
		buffer+=sprintf(buffer,"ENTER [");
		rmask = INS_OP8(ic);
		ic++;
		ic += op_disp(disp, pc, oprom, ic); 
		for (int ri=0; ri<8; ri++)
		{
			if (rmask & 1)
				buffer+=sprintf(buffer,"R%i ", ri);
			rmask >>= 1;
		}
		buffer+= sprintf(buffer,"], %i", disp);

		break;
	case 9: // EXIT imm reglist
		buffer+=sprintf(buffer,"EXIT [");
		rmask = INS_OP8(ic);
		ic++;
		for (int ri=7; ri>=0; ri--)
		{
			if (rmask & 1)
				buffer+=sprintf(buffer,"R%i ", ri);
			rmask >>= 1;
		}
		buffer+= sprintf(buffer,"]");
		break;
	case 10: // NOP
		buffer+=sprintf(buffer,"NOP");
		break;
	case 11: // WAIT
		buffer+=sprintf(buffer,"WAIT");
		break;
	case 12: // DIA (Diagnostics instruction, not used in programming. Branch to self)
		buffer+=sprintf(buffer,"DIA");
		break;
	case 13: // FLAG
		buffer+=sprintf(buffer,"FLAG");
		break;
	case 14: // SVC
		buffer+=sprintf(buffer,"SVC");
		break;
	case 15: // BPT
		buffer+=sprintf(buffer,"BPT");
		break;
	}

	return ic;
}

offs_t ins_format_2(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	INT32 disp;
	offs_t ic = 2; // 2-byte base instruction
	const char* width = mnem_width[FMT2_I];

	switch (FMT2_OP)
	{
	case 0: // ADDQ quick src, wr.i dest [C]
		buffer += sprintf(buffer, "ADDQ%s %i, ", width, FMT2_SHRT_SX);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		break;
	case 1: // CMPQ quick src1, rd.i src2 [ZL]
		buffer += sprintf(buffer, "CMPQ%s %i, ", width, FMT2_SHRT_SX);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		break;
	case 2: // SPRi short procreg, rd.i src [all]
		buffer += sprintf(buffer, "SPR%s %s, ", width, op_areg[FMT2_SHRT]);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		break;
	case 3: // S<cond> wr.i dest
		buffer += sprintf(buffer, "S%s%s ", mnem_cond[FMT2_SHRT], width);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		break;
	case 4: // ACBi quick inc, rmw.i index, disp dest
		buffer += sprintf(buffer, "ACB%s %i, ", width, FMT2_SHRT_SX);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		ic += op_disp(disp, pc, oprom, ic);
		buffer += sprintf(buffer, ", %i", disp);
		break;
	case 5: // MOVQ quick src, wr.i dest
		buffer += sprintf(buffer, "MOVQ%s %i, ", width, FMT2_SHRT_SX);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		break;
	case 6: // LPRi short procreg, rd.i src [all]
		buffer += sprintf(buffer, "LPR%s %s, ", width, op_areg[FMT2_SHRT]);
		ic += ins_gen(FMT2_GEN, FMT2_I, &buffer, pc, oprom, ic);
		break;
	}
	return ic;
}

offs_t ins_format_3(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
//	INT32 disp;
	offs_t ic = 2; // 2-byte base instruction
	const char* width = mnem_width[FMT3_I];

	switch (FMT3_OP)
	{
	case 0: // CXPD addr descriptor
		buffer += sprintf(buffer, "CXPD ");
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		ic += DASMFLAG_STEP_OVER;
		break;
	case 2: // BICPSRi rd.i src [all]
		buffer += sprintf(buffer, "BICPSR%s ", width);
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		break;
	case 4: // JUMP addr dest
		buffer += sprintf(buffer, "JUMP ");
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		break;
	case 6: // BISPSRi rd.i src [all]
		buffer += sprintf(buffer, "BISPSR%s ", width);
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		break;
	case 10: // ADJSPi rd.i src
		buffer += sprintf(buffer, "ADJSP%s ", width);
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		break;
	case 12: // JSR addr dest
		buffer += sprintf(buffer, "JSR ");
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		ic += DASMFLAG_STEP_OVER;
		break;
	case 14: // CASEi rd.i index
		buffer += sprintf(buffer, "CASE%s ", width);
		ic += ins_gen(FMT3_GEN, FMT3_I, &buffer, pc, oprom, ic);
		break;
	}
	return ic;
}

static const char * const fmt4_op[] = {
		"ADD", "CMP", "BIC", "[ill]", "ADDC" , "MOV", "OR", "[ill]",
		"SUB", "ADDR", "AND", "[ill]", "SUBC", "TBIT", "XOR", "[ill]"
		};

offs_t ins_format_4(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
//	INT32 disp;
	offs_t ic = 2; // 2-byte base instruction
	const char* width = mnem_width[ins & 3];
//	INT8 shrt = (ins >> 7) & 0xF;
//	UINT8 gen = (ins >> 11) & 0x1F;

	// (F4OP)
	// TBITi rd.i offset, regaddr base [F]
	// ADDi ADDCi SUBi SUBCi rd.i src, rmw.i dest [CF]
	// CMPi rd.i src1, rd.i src2 [ZNL]
	// MOVi ANDi ORi BICi XORi rd.i src, rmw.i dest
	// ADDR addr src, wr.d dest
	// LXPD addr EXT(n), wr.d dest
	buffer += sprintf(buffer, "%s%s ", fmt4_op[FMT4_OP], width);
	ic += ins_gen(FMT4_GEN1, FMT4_I, &buffer, pc, oprom, ic);
	buffer += sprintf(buffer, ", ");
	ic += ins_gen(FMT4_GEN2, FMT4_I, &buffer, pc, oprom, ic);
	return ic;
}

static const char * const fmt5_setcfg[] = {
		"", "I", "F", "FI", "M", "MI", "MF", "MFI", 
		"C", "CI", "CF", "CFI", "CM", "CMI", "CMF", "CMFI"
		};

// UW B T ~10
static const char * const fmt5_stropt[] = {
		"", "", "B", "B", "U", "W", "B, W", "B, W",
		"[ill]", "[ill]", "[ill]", "[ill]", "U", "U", "B, U", "B, U"
		};


offs_t ins_format_5(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	const char* width = mnem_width[FMT5_I];
	offs_t ic = 3; // 3-byte base instruction

	switch (FMT5_OP)
	{
		case 0: // MOVS[T|i] [B[,]][U|W] [F]
			buffer += sprintf(buffer, "MOVS%s %s", ((*width == 'B') & FMT5_T) ? "T" : width, fmt5_stropt[FMT5_SHRT]);
			break;
		case 1: // CMPS[T|i] [B[,]][U|W] [ZNLF]
			buffer += sprintf(buffer, "CMPS%s %s", ((*width == 'B') & FMT5_T) ? "T" : width, fmt5_stropt[FMT5_SHRT]);
			break;
		case 2: // SETCFG short cfglist
			buffer += sprintf(buffer, "SETCFG [%s]", fmt5_setcfg[FMT5_SHRT]);
			break;
		case 3: // SKPS[T|i] [B[,]][U|W] [F]
			buffer += sprintf(buffer, "SKPS%s %s", ((*width == 'B') & FMT5_T) ? "T" : width, fmt5_stropt[FMT5_SHRT]);
			break;
		default:
			buffer += sprintf(buffer, "[Illegal]");
			break;
	}

	return ic;
}

offs_t ins_format_6(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	const char* width = mnem_width[FMT6_I];
	offs_t ic = 3; // 3-byte base instruction

	switch (FMT6_OP)
	{
		case 0: // ROTi rd.B count, rmw.i dest
			buffer += sprintf(buffer, "ROT%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 1: // ASHi rd.B count, rmw.i dest
			buffer += sprintf(buffer, "ASH%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 2: // CBITi rd.i offset, regaddr base [F]
			buffer += sprintf(buffer, "CBIT%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 3: // CBITIi rd.i offset, regaddr base [F]
			buffer += sprintf(buffer, "CBIT%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 4: // Undefined
			buffer += sprintf(buffer, "[Undefined fmt6 (trap)]");
			break;
		case 5: // LSHi rd.B count, rmw.i dest
			buffer += sprintf(buffer, "LSH%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 6: // SBITi rd.i offset, regaddr base [F]
			buffer += sprintf(buffer, "SBIT%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 7: // SBITIi rd.i offset, regaddr base [F]
			buffer += sprintf(buffer, "SBITI%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 8: // NEGi rd.i src, w.i dest [CF]
			buffer += sprintf(buffer, "NEG%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 9: // NOTi rd.i src, w.i dest
			buffer += sprintf(buffer, "NOT%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 10: // Undefined
			buffer += sprintf(buffer, "[Undefined fmt6 (trap)]");
			break;
		case 11: // SUBPi rd.i src, rmw.i dest [CF]
			buffer += sprintf(buffer, "SUBP%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 12: // ABSi rd.i src, rmw.i dest [F]
			buffer += sprintf(buffer, "ABS%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 13: // COMi rd.i src, rmw.i dest
			buffer += sprintf(buffer, "COM%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 14: // IBITi  rd.i offset, regaddr base [F]
			buffer += sprintf(buffer, "IBIT%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		case 15: // ADDPi rd.i src, rmw.i dest [CF]
			buffer += sprintf(buffer, "ADDP%s ", width);
			ic += ins_gen(FMT6_GEN1, FMT6_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT6_GEN2, FMT6_I, &buffer, pc, oprom, ic);
			break;
		default:
			buffer += sprintf(buffer, "[Illegal]");
			break;
	}
	return ic;
}

offs_t ins_format_7(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	const char* width = mnem_width[FMT7_I];
	INT32 disp;
	UINT8 imm;
	offs_t ic = 3; // 3-byte base instruction

	switch (FMT7_OP)
	{
		case 0: // MOVMi addr block1, addr block2, disp length (1264)
			buffer += sprintf(buffer, "MOVM%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			ic += op_disp(disp, pc, oprom, ic);
			buffer += sprintf(buffer, ", %i", disp);
			break;
		case 1: // CMPMi addr block1, addr block2, disp length [ZNL] (1264)
			buffer += sprintf(buffer, "CMPM%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			ic += op_disp(disp, pc, oprom, ic);
			buffer += sprintf(buffer, ", %i", disp);
			break;
		case 2: // INSSi rd.i src, regaddr base, imm(cons3,cons5) offset,length (1265)
			buffer += sprintf(buffer, "INSS%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			imm = INS_OP8(ic); ic++;
			buffer += sprintf(buffer, ", %i, %i", (imm >> 5 & 7), (imm & 0x1F) + 1);
			break;
		case 3: // EXTSi regaddr base, wr.i dest, imm(cons3,cons5) offset,length (1265)
			buffer += sprintf(buffer, "EXTS%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			imm = INS_OP8(ic); ic++;
			buffer += sprintf(buffer, ", %i, %i", (imm >> 5 & 7), (imm & 0x1F) + 1);
			break;
		case 4: // MOVXBW rd.B src, rmw.W dest (1270)
			buffer += sprintf(buffer, "MOVXBW ");
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 5: // MOVZBW rd.B src, rmw.W dest (1271)
			buffer += sprintf(buffer, "MOVZBW ");
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 6: // MOVZiD rd.i src, rmw.D dest (1271)
			buffer += sprintf(buffer, "MOVZ%sD ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 7: // MOVXiD rd.i src, rmw.D dest (1271)
			buffer += sprintf(buffer, "MOVX%sD ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 8: // MULi rd.i src, rmw.i dest (1270)
			buffer += sprintf(buffer, "MUL%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 9: // MEIi rd.i src, rmw.i dest (1263)
			buffer += sprintf(buffer, "MEI%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 10: // Undefined
			buffer += sprintf(buffer, "[Undefined]");
			break;
		case 11: // DEIi rd.i src, rmw.i dest (trap DVZ) (1263)
			buffer += sprintf(buffer, "DEI%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 12: // QUOi rd.i src, rmw.i dest (trap DVZ) (1270)
			buffer += sprintf(buffer, "QUO%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 13: // REMi rd.i src, rmw.i dest (trap DVZ) (1270)
			buffer += sprintf(buffer, "REM%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 14: // MODi rd.i src, rmw.i dest (trap DVZ) (1270)
			buffer += sprintf(buffer, "MOD%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		case 15: // DIVi rd.i src, rmw.i dest (trap DVZ) (1270)
			buffer += sprintf(buffer, "DIV%s ", width);
			ic += ins_gen(FMT7_GEN1, FMT7_I, &buffer, pc, oprom, ic);
			buffer += sprintf(buffer, ", ");
			ic += ins_gen(FMT7_GEN2, FMT7_I, &buffer, pc, oprom, ic);
			break;
		default:
			buffer += sprintf(buffer, "[Illegal]");
			break;
	}

	return ic;
}


offs_t ins_format_unk(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	buffer += sprintf(buffer, "Unknown (%02X)", ins & 0xFF);
	return 1;
}

offs_t ins_format_unimpl(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom)
{
	int fmt = ns32016_device::op_fmt[ins & 0xFF];
	buffer += sprintf(buffer, "Unimplemented (%02X) (fmt %i)", ins & 0xFF, fmt);
	return 1;
}

typedef offs_t (*pFmtFunc)(UINT32 ins, char *buffer, offs_t pc, const UINT8 *oprom);

static const pFmtFunc fmt_func[] = {
	&ins_format_0,
	&ins_format_1,
	&ins_format_2,
	&ins_format_3,
	&ins_format_4,
	&ins_format_5, // 5
	&ins_format_6,
	&ins_format_7,
	&ins_format_unimpl,
	&ins_format_unimpl,
	&ins_format_unimpl, // 10
	&ins_format_unimpl,
	&ins_format_unimpl,
	&ins_format_unimpl,
	&ins_format_unimpl,
	&ins_format_unimpl, // 10
	&ins_format_unimpl,
	&ins_format_unimpl,
	&ins_format_unimpl,
	&ins_format_unimpl // 19
};

CPU_DISASSEMBLE( ns32016 ) {
	UINT32 ins = INS_BAS(0);
	offs_t nbytes = 0, flags = DASMFLAG_SUPPORTED;

	int fmt = ns32016_device::op_fmt[ins & 0xFF];
	if (fmt < 0 )
		nbytes += ins_format_unk(ins, buffer, pc, oprom);
	else
	{
		pFmtFunc pFFunc = fmt_func[fmt];
		nbytes += pFFunc(ins, buffer, pc, oprom);
	}

	return nbytes | flags;
}
