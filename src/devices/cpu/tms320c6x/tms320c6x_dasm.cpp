// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Texas Instruments TMS320C6000 (C64x+/C674x) disassembler

    Instructions are decoded into a common form, which the CPU core also
    executes from. 16-bit compact instructions in header-based fetch
    packets decode to the same form as their 32-bit equivalents.

    The syntax follows TI's dis6x. Constants that dis6x shows in hex or
    decimal depending on the form are always shown one way here.

    Reference: SPRUFE8B, TMS320C674x DSP CPU and Instruction Set
    Reference Guide (chapter 3 and appendixes C-H).

***************************************************************************/

#include "emu.h"
#include "tms320c6x_dasm.h"


namespace {

constexpr char const *MNEMONICS[] =
{
#define TMS320C6X_OP_NAME(name, mnemonic) mnemonic,
	TMS320C6X_OPS(TMS320C6X_OP_NAME)
#undef TMS320C6X_OP_NAME
};

using insn = tms320c6x_disassembler::insn;
using dasm = tms320c6x_disassembler;

// condition register field to register number + 1
constexpr u8 CREG_REGS[8] = { 0, 32 + 1, 33 + 1, 34 + 1, 1 + 1, 2 + 1, 0 + 1, 0 };

// operand forms for the table-driven 1 or 2 source formats
enum form : u8
{
	RR,     // a = src1, b = xsrc2
	CR,     // a = scst5, b = xsrc2
	UR,     // a = ucst5, b = xsrc2
	XL,     // a = xsrc1, b = src2 pair
	CL,     // a = scst5, b = src2 pair
	UL,     // a = ucst5, b = src2 pair
	SW,     // a = xsrc1, b = src2
	SX,     // a = xsrc2, b = src1
	RU,     // unary: b = xsrc2
	LU,     // unary: b = src2 pair
	SR,     // shift: a = xsrc2, b = src1
	SC,     // shift: a = xsrc2, b = ucst5
	LR,     // shift: a = src2 pair, b = src1
	LC      // shift: a = src2 pair, b = ucst5
};

// pair flags in table entries
enum : u8
{
	PA = 1, PB = 2, PD = 4
};

struct table_entry
{
	u8 opfield;
	dasm::op_t op;
	form f;
	u8 pairs;
};

#define E(o, op, f, p) { o, dasm::OP_##op, f, p }

// .L unit, op in bits 11:5
const table_entry L_TABLE[] =
{
	E(0x03, ADD, RR, 0), E(0x23, ADD_IIL, RR, PD), E(0x21, ADD_IL, XL, PB | PD), E(0x02, ADD, CR, 0), E(0x20, ADD_IL, CL, PB | PD),
	E(0x2b, ADDU_IIL, RR, PD), E(0x29, ADDU_IL, XL, PB | PD),
	E(0x05, ADD2, RR, 0), E(0x65, ADD4, RR, 0),
	E(0x18, ADDDP, RR, PA | PB | PD), E(0x10, ADDSP, RR, 0),
	E(0x7b, AND, RR, 0), E(0x7a, AND, CR, 0), E(0x7c, ANDN, RR, 0),
	E(0x53, CMPEQ, RR, 0), E(0x52, CMPEQ, CR, 0), E(0x51, CMPEQ_L, XL, PB), E(0x50, CMPEQ_L, CL, PB),
	E(0x47, CMPGT, RR, 0), E(0x46, CMPGT, CR, 0), E(0x45, CMPGT_L, XL, PB), E(0x44, CMPGT_L, CL, PB),
	E(0x4f, CMPGTU, RR, 0), E(0x4e, CMPGTU, UR, 0), E(0x4d, CMPGTU_L, XL, PB), E(0x4c, CMPGTU_L, UL, PB),
	E(0x57, CMPLT, RR, 0), E(0x56, CMPLT, CR, 0), E(0x55, CMPLT_L, XL, PB), E(0x54, CMPLT_L, CL, PB),
	E(0x5f, CMPLTU, RR, 0), E(0x5e, CMPLTU, UR, 0), E(0x5d, CMPLTU_L, XL, PB), E(0x5c, CMPLTU_L, UL, PB),
	E(0x08, DPINT, LU, PB), E(0x09, DPSP, LU, PB), E(0x01, DPTRUNC, LU, PB),
	E(0x39, INTDP, RU, PD), E(0x3b, INTDPU, RU, PD), E(0x4a, INTSP, RU, 0), E(0x49, INTSPU, RU, 0),
	E(0x6b, LMBD, RR, 0), E(0x6a, LMBD, CR, 0),
	E(0x42, MAX2, RR, 0), E(0x43, MAXU4, RR, 0), E(0x41, MIN2, RR, 0), E(0x48, MINU4, RR, 0),
	E(0x63, NORM, RU, 0), E(0x60, NORM_L, LU, PB),
	E(0x7f, OR, RR, 0), E(0x7e, OR, CR, 0),
	E(0x00, PACK2, RR, 0), E(0x1e, PACKH2, RR, 0), E(0x69, PACKH4, RR, 0), E(0x1c, PACKHL2, RR, 0),
	E(0x1b, PACKLH2, RR, 0), E(0x68, PACKL4, RR, 0),
	E(0x13, SADD, RR, 0), E(0x31, SADD_L, XL, PB | PD), E(0x12, SADD, CR, 0), E(0x30, SADD_L, CL, PB | PD),
	E(0x40, SAT, LU, PB),
	E(0x61, SHLMB, RR, 0), E(0x62, SHRMB, RR, 0),
	E(0x0a, SPINT, RU, 0), E(0x0b, SPTRUNC, RU, 0),
	E(0x0f, SSUB, RR, 0), E(0x1f, SSUB, SW, 0), E(0x0e, SSUB, CR, 0), E(0x2c, SSUB_L, CL, PB | PD),
	E(0x64, SSUB2, RR, 0),
	E(0x07, SUB, RR, 0), E(0x17, SUB, SW, 0), E(0x27, SUB_IIL, RR, PD), E(0x37, SUB_IIL, SW, PD),
	E(0x06, SUB, CR, 0), E(0x24, SUB_IL, CL, PB | PD),
	E(0x5a, SUBABS4, RR, 0), E(0x4b, SUBC, RR, 0),
	E(0x19, SUBDP, RR, PA | PB | PD), E(0x1d, SUBDP, SW, PA | PB | PD),
	E(0x11, SUBSP, RR, 0), E(0x15, SUBSP, SW, 0),
	E(0x2f, SUBU_IIL, RR, PD), E(0x3f, SUBU_IIL, SW, PD),
	E(0x04, SUB2, RR, 0), E(0x66, SUB4, RR, 0),
	E(0x6f, XOR, RR, 0), E(0x6e, XOR, CR, 0),
	E(0x38, ABS_L, LU, PB | PD)
};

// .L unit nonconditional (creg/z = 0001), op in bits 11:5
const table_entry L_NC_TABLE[] =
{
	E(0x0c, ADDSUB, RR, PD), E(0x0d, ADDSUB2, RR, PD), E(0x34, DPACK2, RR, PD), E(0x33, DPACKX2, RR, PD),
	E(0x0e, SADDSUB, RR, PD), E(0x0f, SADDSUB2, RR, PD), E(0x36, SHFL3, RR, PD)
};

// .S unit floating-point add/subtract, op in bits 11:5
const table_entry S_FP_TABLE[] =
{
	E(0x70, ADDSP, RR, 0), E(0x72, ADDDP, RR, PA | PB | PD), E(0x71, SUBSP, RR, 0), E(0x75, SUBSP, SX, 0),
	E(0x73, SUBDP, RR, PA | PB | PD), E(0x77, SUBDP, SX, PA | PB | PD)
};

// .S unit, op in bits 11:6
const table_entry S_TABLE[] =
{
	E(0x07, ADD, RR, 0), E(0x06, ADD, CR, 0), E(0x01, ADD2, RR, 0),
	E(0x1f, AND, RR, 0), E(0x1e, AND, CR, 0),
	E(0x3f, CLR, SR, 0), E(0x2f, EXT, SR, 0), E(0x2b, EXTU, SR, 0), E(0x3b, SET, SR, 0),
	E(0x1d, CMPEQ2, RR, 0), E(0x1c, CMPEQ4, RR, 0), E(0x14, CMPGT2, RR, 0), E(0x15, CMPGTU4, RR, 0),
	E(0x28, CMPEQDP, RR, PA | PB), E(0x29, CMPGTDP, RR, PA | PB), E(0x2a, CMPLTDP, RR, PA | PB),
	E(0x38, CMPEQSP, RR, 0), E(0x39, CMPGTSP, RR, 0), E(0x3a, CMPLTSP, RR, 0),
	E(0x1b, OR, RR, 0), E(0x1a, OR, CR, 0),
	E(0x09, PACKH2, RR, 0), E(0x08, PACKHL2, RR, 0), E(0x10, PACKLH2, RR, 0),
	E(0x2d, RCPDP, LU, PB | PD), E(0x3d, RCPSP, RU, 0), E(0x2e, RSQRDP, LU, PB | PD), E(0x3e, RSQRSP, RU, 0),
	E(0x2c, ABSDP, LU, PB | PD),
	E(0x20, SADD, RR, 0),
	E(0x33, SHL, SR, 0), E(0x31, SHL_L, LR, PA | PD), E(0x13, SHL_UL, SR, PD),
	E(0x32, SHL, SC, 0), E(0x30, SHL_L, LC, PA | PD), E(0x12, SHL_UL, SC, PD),
	E(0x37, SHR, SR, 0), E(0x35, SHR_L, LR, PA | PD), E(0x36, SHR, SC, 0), E(0x34, SHR_L, LC, PA | PD),
	E(0x27, SHRU, SR, 0), E(0x25, SHRU_L, LR, PA | PD), E(0x26, SHRU, SC, 0), E(0x24, SHRU_L, LC, PA | PD),
	E(0x18, SHR2, SC, 0), E(0x19, SHRU2, SC, 0),
	E(0x02, SPDP, RU, PD),
	E(0x23, SSHL, SR, 0), E(0x22, SSHL, SC, 0),
	E(0x17, SUB, RR, 0), E(0x16, SUB, CR, 0), E(0x11, SUB2, RR, 0),
	E(0x0b, XOR, RR, 0), E(0x0a, XOR, CR, 0)
};

// .S unit extended, op in bits 9:6
const table_entry S_EXT_TABLE[] =
{
	E(0x0, SADD2, RR, 0), E(0x1, SADDUS2, RR, 0), E(0x2, SPACK2, RR, 0), E(0x3, SADDU4, RR, 0),
	E(0x4, SPACKU4, RR, 0), E(0x5, SUB, SX, 0), E(0x6, ANDN, RR, 0), E(0x7, SHR2, SR, 0), E(0x8, SHRU2, SR, 0),
	E(0x9, SHLMB, RR, 0), E(0xa, SHRMB, RR, 0), E(0xb, DMV, RR, PD), E(0xc, MIN2, RR, 0),
	E(0xd, MAX2, RR, 0), E(0xf, PACK2, RR, 0)
};

// .M unit, op in bits 11:7
const table_entry M_TABLE[] =
{
	E(0x19, MPY, RR, 0), E(0x18, MPY, CR, 0), E(0x01, MPYH, RR, 0), E(0x09, MPYHL, RR, 0),
	E(0x0f, MPYHLU, RR, 0), E(0x0b, MPYHSLU, RR, 0), E(0x03, MPYHSU, RR, 0), E(0x07, MPYHU, RR, 0),
	E(0x0d, MPYHULS, RR, 0), E(0x05, MPYHUS, RR, 0), E(0x11, MPYLH, RR, 0), E(0x17, MPYLHU, RR, 0),
	E(0x13, MPYLSHU, RR, 0), E(0x15, MPYLUHS, RR, 0), E(0x1b, MPYSU, RR, 0), E(0x1e, MPYSU, CR, 0),
	E(0x1f, MPYU, RR, 0), E(0x1d, MPYUS, RR, 0), E(0x1a, SMPY, RR, 0), E(0x02, SMPYH, RR, 0),
	E(0x0a, SMPYHL, RR, 0), E(0x12, SMPYLH, RR, 0), E(0x04, MPYI, RR, 0), E(0x06, MPYI, CR, 0),
	E(0x08, MPYID, RR, PD), E(0x0c, MPYID, CR, PD), E(0x1c, MPYSP, RR, 0), E(0x0e, MPYDP, RR, PA | PB | PD),
	E(0x10, MPY32, RR, 0), E(0x14, MPY32_64, RR, PD), E(0x16, MPY32SU, RR, PD)
};

// .M unit extended, op in bits 10:6
const table_entry M_EXT_TABLE[] =
{
	E(0x13, AVG2, RR, 0), E(0x12, AVGU4, RR, 0), E(0x0c, DOTP2, RR, 0), E(0x0b, DOTP2_LL, RR, PD),
	E(0x09, DOTPN2, RR, 0), E(0x07, DOTPNRSU2, RR, 0), E(0x0d, DOTPRSU2, RR, 0), E(0x02, DOTPSU4, RR, 0),
	E(0x06, DOTPU4, RR, 0), E(0x11, GMPY4, RR, 0), E(0x14, MPYHI, RR, PD), E(0x10, MPYHIR, RR, 0),
	E(0x15, MPYLI, RR, PD), E(0x0e, MPYLIR, RR, 0), E(0x16, MPYSPDP, RR, PB | PD), E(0x17, MPYSP2DP, RR, PD),
	E(0x05, MPYSU4, RR, PD), E(0x04, MPYU4, RR, PD), E(0x00, MPY2, RR, PD), E(0x18, MPY32U, RR, PD),
	E(0x19, MPY32US, RR, PD), E(0x01, SMPY2, RR, PD), E(0x1c, SSHVL, RR, 0), E(0x1a, SSHVR, RR, 0),
	E(0x1d, ROTL, SR, 0), E(0x1e, ROTL, SC, 0)
};

// .M unit extended nonconditional, op in bits 10:6
const table_entry M_NC_TABLE[] =
{
	E(0x0a, CMPY, RR, PD), E(0x0b, CMPYR, RR, 0), E(0x0c, CMPYR1, RR, 0), E(0x18, DDOTP4, RR, PA | PD),
	E(0x17, DDOTPH2, RR, PA | PD), E(0x15, DDOTPH2R, RR, PA), E(0x16, DDOTPL2, RR, PA | PD), E(0x14, DDOTPL2R, RR, PA),
	E(0x1f, GMPY, RR, 0), E(0x0f, MPY2IR, RR, PD), E(0x19, SMPY32, RR, 0), E(0x1b, XORMPY, RR, 0)
};

// .D unit extended, op in bits 9:6
const table_entry D_EXT_TABLE[] =
{
	E(0xa, ADD, RR, 0), E(0xb, ADD, CR, 0), E(0x4, ADD2, RR, 0), E(0x6, AND, RR, 0), E(0x7, AND, CR, 0),
	E(0x0, ANDN, RR, 0), E(0x2, OR, RR, 0), E(0x3, OR, CR, 0), E(0xc, SUB, RR, 0), E(0x5, SUB2, RR, 0),
	E(0xe, XOR, RR, 0), E(0xf, XOR, CR, 0)
};

#undef E

template <std::size_t N>
table_entry const *lookup(table_entry const (&table)[N], u32 opfield)
{
	for (auto const &e : table)
		if (e.opfield == opfield)
			return &e;
	return nullptr;
}

// fill operands of a table-driven 1 or 2 source instruction
void apply(insn &i, table_entry const &e, u32 o, u8 unit)
{
	u32 const side = BIT(o, 1) ? 32 : 0;
	u32 const xside = BIT(o, 12) ? (side ^ 32) : side;
	u32 const fd = BIT(o, 23, 5), f2 = BIT(o, 18, 5), f1 = BIT(o, 13, 5);

	i.op = e.op;
	i.unit = unit;
	i.dst = side + fd;
	if (BIT(o, 12))
		i.flags |= dasm::F_CROSS;

	switch (e.f)
	{
	case RR: i.a = side + f1; i.b = xside + f2; break;
	case CR: i.cst = util::sext(f1, 5); i.flags |= dasm::F_A_CST; i.b = xside + f2; break;
	case UR: i.cst = f1; i.flags |= dasm::F_A_CST; i.b = xside + f2; break;
	case XL: i.a = xside + f1; i.b = side + f2; break;
	case CL: i.cst = util::sext(f1, 5); i.flags |= dasm::F_A_CST; i.b = side + f2; break;
	case UL: i.cst = f1; i.flags |= dasm::F_A_CST; i.b = side + f2; break;
	case SW: i.a = xside + f1; i.b = side + f2; break;
	case SX: i.a = xside + f2; i.b = side + f1; break;
	case RU: i.b = xside + f2; i.flags |= dasm::F_NOA; break;
	case LU: i.b = side + f2; i.flags |= dasm::F_NOA; break;
	case SR: i.a = xside + f2; i.b = side + f1; break;
	case SC: i.a = xside + f2; i.cst = f1; i.flags |= dasm::F_B_CST; break;
	case LR: i.a = side + f2; i.b = side + f1; break;
	case LC: i.a = side + f2; i.cst = f1; i.flags |= dasm::F_B_CST; break;
	}

	// register pairs are named by the even register
	if (e.pairs & PA)
	{
		i.flags |= dasm::F_A_PAIR;
		i.a &= ~1;
	}
	if (e.pairs & PB)
	{
		i.flags |= dasm::F_B_PAIR;
		i.b &= ~1;
	}
	if (e.pairs & PD)
	{
		i.flags |= dasm::F_D_PAIR;
		i.dst &= ~1;
	}
}

// load/store access sizes
u32 access_shift(dasm::op_t op)
{
	switch (op)
	{
	case dasm::OP_LDB: case dasm::OP_LDBU: case dasm::OP_STB: return 0;
	case dasm::OP_LDH: case dasm::OP_LDHU: case dasm::OP_STH: return 1;
	case dasm::OP_LDDW: case dasm::OP_LDNDW: case dasm::OP_STDW: case dasm::OP_STNDW: return 3;
	default: return 2;
	}
}

bool is_load(dasm::op_t op)
{
	switch (op)
	{
	case dasm::OP_LDB: case dasm::OP_LDBU: case dasm::OP_LDH: case dasm::OP_LDHU:
	case dasm::OP_LDW: case dasm::OP_LDNW: case dasm::OP_LDDW: case dasm::OP_LDNDW:
		return true;
	default:
		return false;
	}
}

bool is_store(dasm::op_t op)
{
	switch (op)
	{
	case dasm::OP_STB: case dasm::OP_STH: case dasm::OP_STW: case dasm::OP_STNW:
	case dasm::OP_STDW: case dasm::OP_STNDW:
		return true;
	default:
		return false;
	}
}

bool is_pair_access(dasm::op_t op)
{
	return access_shift(op) == 3;
}

// p-bit of the last instruction in the fetch packet before fp
bool last_p(u32 fp, tms320c6x_disassembler::data_buffer const &opcodes)
{
	u32 const prev = fp - 32;
	u32 const header = opcodes.r32(prev + 28);
	if (!tms320c6x_disassembler::is_header(header))
		return BIT(header, 0);
	if (BIT(header, 21 + 6))
		return BIT(header, 13);
	return BIT(opcodes.r32(prev + 24), 0);
}

} // anonymous namespace


char const *tms320c6x_disassembler::mnemonic(op_t op)
{
	return (op < OP_COUNT) ? MNEMONICS[op] : "?";
}

char const *tms320c6x_disassembler::control_register_name(u32 crlo, u32 crhi, bool write)
{
	switch (crlo)
	{
	case 0x00: return "AMR";
	case 0x01: return "CSR";
	case 0x02: return write ? "ISR" : "IFR";
	case 0x03: return "ICR";
	case 0x04: return "IER";
	case 0x05: return "ISTP";
	case 0x06: return "IRP";
	case 0x07: return "NRP";
	case 0x0a: return "TSCL";
	case 0x0b: return "TSCH";
	case 0x0d: return "ILC";
	case 0x0e: return "RILC";
	case 0x0f: return "REP";
	case 0x10: return "PCE1";
	case 0x11: return "DNUM";
	case 0x12: return "FADCR";
	case 0x13: return "FAUCR";
	case 0x14: return "FMCR";
	case 0x15: return "SSR";
	case 0x16: return "GPLYA";
	case 0x17: return "GPLYB";
	case 0x18: return "GFPGFR";
	case 0x19: return "DIER";
	case 0x1a: return "TSR";
	case 0x1b: return "ITSR";
	case 0x1c: return "NTSR";
	case 0x1d: return write ? "ECR" : "EFR";
	case 0x1f: return "IERR";
	default: return nullptr;
	}
}

void tms320c6x_disassembler::decode32(u32 o, u32 pce1, bool header_fp, insn &i)
{
	i = insn();
	i.size = 4;
	i.p = BIT(o, 0);

	u32 const s = BIT(o, 1);
	u32 const side = s ? 32 : 0;
	u32 const fd = BIT(o, 23, 5), f2 = BIT(o, 18, 5), f1 = BIT(o, 13, 5);
	bool const nc = BIT(o, 28, 4) == 1;

	i.creg = CREG_REGS[BIT(o, 29, 3)];
	i.z = BIT(o, 28);
	if (nc)
	{
		i.creg = 0;
		i.z = false;
	}
	else if (BIT(o, 29, 3) == 7)
	{
		return; // reserved condition
	}

	u8 const lunit = s ? UNIT_L2 : UNIT_L1;
	u8 const sunit = s ? UNIT_S2 : UNIT_S1;
	u8 const munit = s ? UNIT_M2 : UNIT_M1;
	u8 const dunit = s ? UNIT_D2 : UNIT_D1;

	// no unit
	if (BIT(o, 2, 10) == 0)
	{
		u32 const op = BIT(o, 13, 4);
		if (BIT(o, 17))
		{
			switch (op)
			{
			case 0xc: i.op = OP_SPLOOP; i.cst = fd + 1; break;
			case 0xd: i.op = OP_SPLOOPD; i.cst = fd + 1; break;
			case 0xf: i.op = OP_SPLOOPW; i.cst = fd + 1; break;
			case 0xa: i.op = OP_SPKERNEL; i.cst = BIT(o, 22, 6); break;
			case 0xb: i.op = OP_SPKERNELR; break;
			case 0x8: i.op = OP_SPMASK; i.cst = BIT(o, 18, 8); break;
			case 0x9: i.op = OP_SPMASKR; i.cst = BIT(o, 18, 8); break;
			}
		}
		else if (nc)
		{
			switch (op)
			{
			case 0x0: i.op = OP_SWE; break;
			case 0x1: i.op = OP_SWENR; break;
			case 0x2: i.op = OP_DINT; break;
			case 0x3: i.op = OP_RINT; break;
			}
		}
		else
		{
			if (op == 0xf)
				i.op = OP_IDLE;
			else
			{
				i.op = OP_NOP;
				i.cst = op + 1;
			}
		}
		return;
	}

	switch (BIT(o, 2, 2))
	{
	case 1: // .D load/store
		{
			u32 const y = BIT(o, 7);
			u32 const op = BIT(o, 4, 3);
			u32 const mode = BIT(o, 9, 4);
			if (!BIT(o, 8))
			{
				static constexpr op_t OPS[8] = { OP_LDHU, OP_LDBU, OP_LDB, OP_STB, OP_LDH, OP_STH, OP_LDW, OP_STW };
				i.op = OPS[op];
				i.dst = side + fd;
			}
			else
			{
				static constexpr op_t OPS[8] = { OP_INVALID, OP_INVALID, OP_LDNDW, OP_LDNW, OP_STDW, OP_STNW, OP_LDDW, OP_STNDW };
				i.op = OPS[op];
				if ((i.op == OP_LDNDW) || (i.op == OP_STNDW))
				{
					i.dst = side + (BIT(o, 24, 4) << 1);
					i.scaled = BIT(o, 23);
				}
				else
					i.dst = side + fd;
				if (is_pair_access(i.op))
					i.flags |= F_D_PAIR;
			}
			i.unit = y ? UNIT_D2 : UNIT_D1;
			i.base = (y ? 32 : 0) + f2;
			i.offset_reg = BIT(mode, 2);
			if (i.offset_reg)
				i.a = (y ? 32 : 0) + f1;
			else
				i.cst = f1;
			static constexpr u8 MODES[16] =
			{
				AM_NEG_OFFSET, AM_POS_OFFSET, 0xff, 0xff, AM_NEG_OFFSET, AM_POS_OFFSET, 0xff, 0xff,
				AM_PREDEC, AM_PREINC, AM_POSTDEC, AM_POSTINC, AM_PREDEC, AM_PREINC, AM_POSTDEC, AM_POSTINC
			};
			i.mode = MODES[mode];
			if (i.mode == 0xff)
				i.op = OP_INVALID;
		}
		return;

	case 3: // .D load/store with 15-bit offset, or ADDAB/ADDAH/ADDAW long immediate
		{
			u32 const op = BIT(o, 4, 3);
			u32 const base = BIT(o, 7) ? (32 + 15) : (32 + 14);
			if (nc)
			{
				switch (op)
				{
				case 3: i.op = OP_ADDAB; break;
				case 5: i.op = OP_ADDAH; break;
				case 7: i.op = OP_ADDAW; break;
				default: return;
				}
				i.unit = dunit;
				i.a = base;
				i.cst = BIT(o, 8, 15);
				i.flags |= F_B_CST;
				i.dst = side + fd;
				if (!s)
					i.flags |= F_CROSS;
			}
			else
			{
				static constexpr op_t OPS[8] = { OP_LDHU, OP_LDBU, OP_LDB, OP_STB, OP_LDH, OP_STH, OP_LDW, OP_STW };
				i.op = OPS[op];
				i.unit = UNIT_D2;
				i.dst = side + fd;
				i.base = base;
				i.cst = BIT(o, 8, 15);
				i.mode = AM_POS_OFFSET;
			}
		}
		return;
	}

	// bits 3:2 are 10 or 00 from here on
	if (BIT(o, 3))
	{
		if (BIT(o, 4))
		{
			// .L unit, or .S floating-point add/subtract
			u32 const op = BIT(o, 5, 7);
			if (nc)
			{
				if (auto const e = lookup(L_NC_TABLE, op))
					apply(i, *e, o, lunit);
				return;
			}
			if (op == 0x1a)
			{
				// unary
				i.unit = lunit;
				i.dst = side + fd;
				i.b = (BIT(o, 12) ? (side ^ 32) : side) + f2;
				i.flags |= F_NOA;
				if (BIT(o, 12))
					i.flags |= F_CROSS;
				switch (f1)
				{
				case 0x00: i.op = OP_ABS; break;
				case 0x01: i.op = OP_SWAP4; break;
				case 0x02: i.op = OP_UNPKLU4; break;
				case 0x03: i.op = OP_UNPKHU4; break;
				case 0x04: i.op = OP_ABS2; break;
				case 0x05:
					i.op = OP_MVK;
					i.cst = util::sext(f2, 5);
					i.flags = (i.flags & ~(F_CROSS | F_NOA)) | F_A_CST;
					break;
				}
				return;
			}
			if ((op & 0x78) == 0x70)
			{
				if (auto const e = lookup(S_FP_TABLE, op))
					apply(i, *e, o, sunit);
				return;
			}
			if (auto const e = lookup(L_TABLE, op))
				apply(i, *e, o, lunit);
			return;
		}
		if (BIT(o, 5))
		{
			// MVK/MVKH
			i.unit = sunit;
			i.dst = side + fd;
			i.flags |= F_A_CST;
			if (BIT(o, 6))
			{
				i.op = OP_MVKH;
				i.cst = BIT(o, 7, 16) << 16;
			}
			else
			{
				i.op = OP_MVK;
				i.cst = util::sext(BIT(o, 7, 16), 16);
			}
			return;
		}
		// field operations
		{
			static constexpr op_t OPS[4] = { OP_EXTU, OP_EXT, OP_SET, OP_CLR };
			i.op = OPS[BIT(o, 6, 2)];
			i.unit = sunit;
			i.dst = side + fd;
			i.a = side + f2;
			i.cst = f1;
			i.cst2 = BIT(o, 8, 5);
			i.flags |= F_B_CST;
		}
		return;
	}

	switch (BIT(o, 4, 2))
	{
	case 2: // .S unit
		{
			u32 const op = BIT(o, 6, 6);
			u32 const xside = BIT(o, 12) ? (side ^ 32) : side;
			switch (op)
			{
			case 0x05: // ADDKPC
				i.op = OP_ADDKPC;
				i.unit = sunit;
				i.dst = side + fd;
				i.cst = pce1 + (util::sext(BIT(o, 16, 7), 7) << 2);
				i.cst2 = BIT(o, 13, 3);
				return;
			case 0x0d: // B/BNOP register
				i.unit = sunit;
				i.b = xside + f2;
				if (BIT(o, 12))
					i.flags |= F_CROSS;
				if (fd == 0)
					i.op = OP_B_REG;
				else if (fd == 1)
				{
					i.op = OP_BNOP_REG;
					i.cst2 = BIT(o, 13, 3);
				}
				return;
			case 0x03: // B IRP/NRP
				i.unit = sunit;
				if (f2 == 6)
					i.op = OP_B_IRP;
				else if (f2 == 7)
					i.op = OP_B_NRP;
				return;
			case 0x00: // BDEC/BPOS
				i.op = BIT(o, 12) ? OP_BDEC : OP_BPOS;
				i.unit = sunit;
				i.dst = side + fd;
				i.cst = pce1 + (util::sext(BIT(o, 13, 10), 10) << 2);
				return;
			case 0x04: // BNOP displacement
				i.op = OP_BNOP;
				i.unit = sunit;
				i.cst = pce1 + (util::sext(BIT(o, 16, 12), 12) << (header_fp ? 1 : 2));
				i.cst2 = BIT(o, 13, 3);
				return;
			case 0x0e: // MVC to control register
				i.op = OP_MVC_TO;
				i.unit = sunit;
				i.b = xside + f2;
				i.cst = fd;
				i.cst2 = f1;
				if (BIT(o, 12))
					i.flags |= F_CROSS;
				return;
			case 0x0f: // MVC from control register
				i.op = OP_MVC_FROM;
				i.unit = sunit;
				i.dst = side + fd;
				i.cst = f2;
				i.cst2 = f1;
				return;
			case 0x3c: // unary
				i.unit = sunit;
				i.dst = side + fd;
				i.b = xside + f2;
				i.flags |= F_NOA;
				if (BIT(o, 12))
					i.flags |= F_CROSS;
				switch (f1)
				{
				case 0x00: i.op = OP_ABSSP; break;
				case 0x02: i.op = OP_UNPKLU4; break;
				case 0x03: i.op = OP_UNPKHU4; break;
				}
				return;
			}
			if (auto const e = lookup(S_TABLE, op))
				apply(i, *e, o, sunit);
		}
		return;

	case 3: // extended .M/.D/.S
		{
			u32 const op = BIT(o, 6, 4);
			if (!BIT(o, 11))
			{
				u32 const mop = BIT(o, 6, 5);
				if (nc)
				{
					if (auto const e = lookup(M_NC_TABLE, mop))
						apply(i, *e, o, munit);
					return;
				}
				if (mop == 0x03)
				{
					// unary
					i.unit = munit;
					i.dst = side + fd;
					i.b = (BIT(o, 12) ? (side ^ 32) : side) + f2;
					i.flags |= F_NOA;
					if (BIT(o, 12))
						i.flags |= F_CROSS;
					switch (f1)
					{
					case 0x1e: i.op = OP_BITC4; break;
					case 0x1f: i.op = OP_BITR; break;
					case 0x1d: i.op = OP_DEAL; break;
					case 0x1c: i.op = OP_SHFL; break;
					case 0x19: i.op = OP_XPND2; break;
					case 0x18: i.op = OP_XPND4; break;
					case 0x1a: i.op = OP_MVD; break;
					}
					return;
				}
				if (auto const e = lookup(M_EXT_TABLE, mop))
					apply(i, *e, o, munit);
			}
			else if (!BIT(o, 10))
			{
				if (auto const e = lookup(D_EXT_TABLE, op))
					apply(i, *e, o, dunit);
			}
			else
			{
				if ((op == 0xb) && nc)
				{
					static constexpr table_entry RPACK2 = { 0xb, OP_RPACK2, RR, 0 };
					apply(i, RPACK2, o, sunit);
					return;
				}
				if (auto const e = lookup(S_EXT_TABLE, op))
					apply(i, *e, o, sunit);
			}
		}
		return;

	case 0: // .M or .D
		if (!BIT(o, 6))
		{
			if (auto const e = lookup(M_TABLE, BIT(o, 7, 5)))
				apply(i, *e, o, munit);
		}
		else
		{
			// .D 1 or 2 sources: src2, src1, dst
			u32 const op = BIT(o, 7, 6);
			i.unit = dunit;
			i.dst = side + fd;
			i.a = side + f2;
			if (BIT(op, 1) && (op != 0x3c))
			{
				i.cst = f1;
				i.flags |= F_B_CST;
			}
			else
				i.b = side + f1;
			switch (op)
			{
			case 0x10: case 0x12: i.op = OP_ADD; break;
			case 0x11: case 0x13: i.op = OP_SUB; break;
			case 0x30: case 0x32: i.op = OP_ADDAB; break;
			case 0x34: case 0x36: i.op = OP_ADDAH; break;
			case 0x38: case 0x3a: i.op = OP_ADDAW; break;
			case 0x3c: i.op = OP_ADDAD; break;
			case 0x3d: i.op = OP_ADDAD; i.cst = f1; i.flags |= F_B_CST; break;
			case 0x31: case 0x33: i.op = OP_SUBAB; break;
			case 0x35: case 0x37: i.op = OP_SUBAH; break;
			case 0x39: case 0x3b: i.op = OP_SUBAW; break;
			case 0x00:
				if (f2 == 0)
				{
					i.op = OP_MVK;
					i.cst = util::sext(f1, 5);
					i.flags = F_A_CST;
				}
				break;
			}
		}
		return;

	case 1: // B/CALLP or ADDK
		if (!BIT(o, 6))
		{
			i.op = nc ? OP_CALLP : OP_B;
			i.unit = sunit;
			i.cst = pce1 + (util::sext(BIT(o, 7, 21), 21) << 2);
			if (nc)
				i.dst = side + 3;
		}
		else
		{
			i.op = OP_ADDK;
			i.unit = sunit;
			i.dst = side + fd;
			i.cst = util::sext(BIT(o, 7, 16), 16);
			i.flags |= F_A_CST;
		}
		return;
	}
}

void tms320c6x_disassembler::decode16(u16 o, u32 header, u32 pce1, insn &i)
{
	i = insn();
	i.size = 2;
	i.flags = F_COMPACT;

	u32 const s = BIT(o, 0);
	u32 const side = s ? 32 : 0;
	u32 const roff = BIT(header, 19) ? 16 : 0;
	u32 const dsz = BIT(header, 16, 3);
	bool const br = BIT(header, 15);
	bool const sat = BIT(header, 14);
	u32 const x = BIT(o, 12);
	u32 const xside = x ? (side ^ 32) : side;

	auto const r3 = [roff] (u32 sd, u32 n) -> u8 { return sd + roff + n; };
	auto const cross = [&i, x] () { if (x) i.flags |= F_CROSS; };

	if (BIT(o, 1, 2) == 2)
	{
		// .D load/store
		u32 const t = BIT(o, 12) ? 32 : 0;
		u32 const ldst = BIT(o, 3);
		u32 const sz = BIT(o, 9);
		i.unit = s ? UNIT_D2 : UNIT_D1;
		i.base = side + 4 + BIT(o, 7, 2);

		bool dw = false;
		bool na = false;
		auto const pick = [&i, &dw, &na, sz, dsz, ldst, o, t, roff] (bool word_only)
		{
			if (word_only || !sz)
			{
				if (BIT(dsz, 2))
				{
					dw = true;
					na = BIT(o, 4);
					i.op = ldst ? (na ? OP_LDNDW : OP_LDDW) : (na ? OP_STNDW : OP_STDW);
				}
				else
					i.op = ldst ? OP_LDW : OP_STW;
			}
			else
			{
				static constexpr op_t LD[8] = { OP_LDBU, OP_LDB, OP_LDHU, OP_LDH, OP_LDW, OP_LDB, OP_LDNW, OP_LDH };
				static constexpr op_t ST[8] = { OP_STB, OP_STB, OP_STH, OP_STH, OP_STW, OP_STB, OP_STNW, OP_STH };
				i.op = ldst ? LD[dsz] : ST[dsz];
			}
			if (dw)
			{
				i.dst = t + roff + (BIT(o, 5, 2) << 1);
				i.flags |= F_D_PAIR;
			}
			else
				i.dst = t + roff + BIT(o, 4, 3);
		};

		if (!BIT(o, 10))
		{
			// Doff4: *ptr[ucst4]
			pick(false);
			i.mode = AM_POS_OFFSET;
			i.cst = (BIT(o, 11) << 3) | BIT(o, 13, 3);
			if (na)
				i.scaled = false;
		}
		else if (!BIT(o, 11))
		{
			// Dind: *ptr[src1]
			pick(false);
			i.mode = AM_POS_OFFSET;
			i.offset_reg = true;
			i.a = r3(side, BIT(o, 13, 3));
			if (na)
				i.scaled = false;
		}
		else if (BIT(o, 15))
		{
			// Dstk: *B15[ucst5]
			i.op = ldst ? OP_LDW : OP_STW;
			i.unit = UNIT_D2;
			i.base = 32 + 15;
			i.dst = t + roff + BIT(o, 4, 3);
			i.mode = AM_POS_OFFSET;
			i.cst = (BIT(o, 7, 3) << 2) | BIT(o, 13, 2);
		}
		else
		{
			// Dinc: *ptr[ucst2]++, Ddec: *--ptr[ucst2]
			pick(false);
			i.mode = BIT(o, 14) ? AM_PREDEC : AM_POSTINC;
			i.cst = BIT(o, 13) + 1;
		}
		if (BIT(header, 20) && is_load(i.op))
			i.flags |= F_PROT;
		return;
	}

	if (BIT(o, 1, 2) == 0)
	{
		if (!BIT(o, 3))
		{
			// L3, L3i
			i.unit = s ? UNIT_L2 : UNIT_L1;
			i.dst = r3(side, BIT(o, 4, 3));
			i.b = r3(xside, BIT(o, 7, 3));
			cross();
			if (!BIT(o, 10))
			{
				i.a = r3(side, BIT(o, 13, 3));
				if (!BIT(o, 11))
					i.op = sat ? OP_SADD : OP_ADD;
				else
					i.op = sat ? OP_SSUB : OP_SUB;
			}
			else
			{
				u32 const cst3 = BIT(o, 13, 3);
				i.op = OP_ADD;
				i.flags |= F_A_CST;
				if (!BIT(o, 11))
					i.cst = cst3 ? cst3 : 8;
				else
					i.cst = s32(cst3) - 8;
			}
		}
		else if (BIT(o, 10))
		{
			// L2c
			static constexpr op_t OPS[8] = { OP_AND, OP_OR, OP_XOR, OP_CMPEQ, OP_CMPLT, OP_CMPGT, OP_CMPLTU, OP_CMPGTU };
			i.op = OPS[(BIT(o, 11) << 2) | BIT(o, 5, 2)];
			i.unit = s ? UNIT_L2 : UNIT_L1;
			i.a = r3(side, BIT(o, 13, 3));
			i.b = r3(xside, BIT(o, 7, 3));
			i.dst = side + BIT(o, 4);
			cross();
		}
		return;
	}

	if (BIT(o, 1, 2) == 1)
	{
		i.unit = s ? UNIT_S2 : UNIT_S1;
		if (BIT(o, 3))
		{
			if (br)
			{
				if (!BIT(o, 5) && !BIT(o, 4))
				{
					// Sbs7, Sbu8
					i.op = OP_BNOP;
					if (BIT(o, 14, 2) == 3)
					{
						i.cst = pce1 + (BIT(o, 6, 8) << 1);
						i.cst2 = 5;
					}
					else
					{
						i.cst = pce1 + (util::sext(BIT(o, 6, 7), 7) << 1);
						i.cst2 = BIT(o, 13, 3);
					}
				}
				else if (!BIT(o, 5))
				{
					// Scs10
					i.op = OP_CALLP;
					i.cst = pce1 + (util::sext(BIT(o, 6, 10), 10) << 2);
					i.dst = side + 3;
				}
				else
				{
					// Sbs7c, Sbu8c
					i.op = OP_BNOP;
					i.creg = (s ? 32 : 0) + 1;
					i.z = BIT(o, 4);
					if (BIT(o, 14, 2) == 3)
					{
						i.cst = pce1 + (BIT(o, 6, 8) << 1);
						i.cst2 = 5;
					}
					else
					{
						i.cst = pce1 + (util::sext(BIT(o, 6, 7), 7) << 1);
						i.cst2 = BIT(o, 13, 3);
					}
				}
			}
			else
			{
				i.dst = r3(side, BIT(o, 4, 3));
				if (!BIT(o, 10))
				{
					// S3
					i.a = r3(side, BIT(o, 13, 3));
					i.b = r3(xside, BIT(o, 7, 3));
					cross();
					if (BIT(o, 11))
						i.op = OP_SUB;
					else
						i.op = sat ? OP_SADD : OP_ADD;
				}
				else
				{
					// S3i
					static constexpr u8 CST[8] = { 16, 1, 2, 3, 4, 5, 6, 8 };
					i.op = BIT(o, 11) ? OP_SHR : OP_SHL;
					i.a = r3(xside, BIT(o, 7, 3));
					cross();
					i.cst = CST[BIT(o, 13, 3)];
					i.flags |= F_B_CST;
				}
			}
			return;
		}
		if (BIT(o, 4))
		{
			// Smvk8
			i.op = OP_MVK;
			i.dst = r3(side, BIT(o, 7, 3));
			i.cst = (BIT(o, 10) << 7) | (BIT(o, 5, 2) << 5) | (BIT(o, 11, 2) << 3) | BIT(o, 13, 3);
			i.flags |= F_A_CST;
			return;
		}
		u32 const ucst5 = (BIT(o, 11, 2) << 3) | BIT(o, 13, 3);
		u32 const op = BIT(o, 5, 2);
		u32 const rd = r3(side, BIT(o, 7, 3));
		if (BIT(o, 10))
		{
			if (op != 3)
			{
				// Ssh5
				static constexpr op_t OPS[4] = { OP_SHL, OP_SHR, OP_SHRU, OP_INVALID };
				i.op = OPS[op];
				if ((op == 2) && sat)
					i.op = OP_SSHL;
				i.a = rd;
				i.dst = rd;
				i.cst = ucst5;
				i.flags |= F_B_CST;
			}
			else
			{
				// S2sh
				static constexpr op_t OPS[4] = { OP_SHL, OP_SHR, OP_SHRU, OP_SSHL };
				i.op = OPS[BIT(o, 11, 2)];
				i.a = rd;
				i.b = r3(side, BIT(o, 13, 3));
				i.dst = rd;
			}
		}
		else
		{
			if (op != 3)
			{
				// Sc5
				i.a = rd;
				i.cst = ucst5;
				i.flags |= F_B_CST;
				switch (op)
				{
				case 0:
					i.op = OP_EXTU;
					i.cst2 = 31;
					i.dst = side;
					break;
				case 1:
					i.op = OP_SET;
					i.cst2 = ucst5;
					i.dst = rd;
					break;
				case 2:
					i.op = OP_CLR;
					i.cst2 = ucst5;
					i.dst = rd;
					break;
				}
			}
			else
			{
				// S2ext
				static constexpr op_t OPS[4] = { OP_EXT, OP_EXT, OP_EXTU, OP_EXTU };
				i.op = OPS[BIT(o, 11, 2)];
				i.a = r3(side, BIT(o, 7, 3));
				i.dst = r3(side, BIT(o, 13, 3));
				i.cst = i.cst2 = BIT(o, 11) ? 24 : 16;
				i.flags |= F_B_CST;
			}
		}
		return;
	}

	// bits 2:1 = 11
	u32 const unit = BIT(o, 3, 2);
	if (unit == 3)
	{
		// M3
		static constexpr op_t OPS[8] = { OP_MPY, OP_MPYH, OP_MPYLH, OP_MPYHL, OP_SMPY, OP_SMPYH, OP_SMPYLH, OP_SMPYHL };
		i.op = OPS[(sat ? 4 : 0) | BIT(o, 5, 2)];
		i.unit = s ? UNIT_M2 : UNIT_M1;
		i.a = r3(side, BIT(o, 13, 3));
		i.b = r3(xside, BIT(o, 7, 3));
		i.dst = r3(side, BIT(o, 10, 2) << 1);
		cross();
		return;
	}

	static constexpr u8 UNITS[3][2] = { { UNIT_L1, UNIT_L2 }, { UNIT_S1, UNIT_S2 }, { UNIT_D1, UNIT_D2 } };
	u8 const u = UNITS[unit][s];

	switch (BIT(o, 5, 2))
	{
	case 0: // LSDmvto
		i.op = OP_MV;
		i.unit = u;
		i.dst = r3(side, BIT(o, 13, 3));
		i.b = xside + ((BIT(o, 10, 2) << 3) | BIT(o, 7, 3));
		i.flags |= F_NOA;
		cross();
		return;

	case 2: // LSDmvfr
		i.op = OP_MV;
		i.unit = u;
		i.dst = side + ((BIT(o, 10, 2) << 3) | BIT(o, 13, 3));
		i.b = r3(xside, BIT(o, 7, 3));
		i.flags |= F_NOA;
		cross();
		return;

	case 1:
		i.unit = u;
		switch (unit)
		{
		case 0:
			if (BIT(o, 10))
			{
				// Lx5
				i.op = OP_MVK;
				i.dst = r3(side, BIT(o, 7, 3));
				i.cst = util::sext((BIT(o, 11, 2) << 3) | BIT(o, 13, 3), 5);
				i.flags |= F_A_CST;
			}
			else if (!BIT(o, 12))
			{
				// Lx3c
				i.op = OP_CMPEQ;
				i.cst = BIT(o, 13, 3);
				i.flags |= F_A_CST;
				i.b = r3(side, BIT(o, 7, 3));
				i.dst = side + BIT(o, 11);
			}
			else
			{
				// Lx1c
				static constexpr op_t OPS[4] = { OP_CMPLT, OP_CMPGT, OP_CMPLTU, OP_CMPGTU };
				i.op = OPS[BIT(o, 14, 2)];
				i.cst = BIT(o, 13);
				i.flags |= F_A_CST;
				i.b = r3(side, BIT(o, 7, 3));
				i.dst = side + BIT(o, 11);
			}
			return;

		case 1:
			if (!BIT(o, 10))
			{
				// Sx2op
				i.op = BIT(o, 11) ? OP_SUB : OP_ADD;
				i.a = r3(side, BIT(o, 13, 3));
				i.b = r3(xside, BIT(o, 7, 3));
				i.dst = i.a;
				cross();
			}
			else
			{
				// Sx5
				i.op = OP_ADDK;
				i.cst = (BIT(o, 11, 2) << 3) | BIT(o, 13, 3);
				i.flags |= F_A_CST;
				i.dst = r3(side, BIT(o, 7, 3));
			}
			return;

		case 2:
			if (!BIT(o, 10))
			{
				// Dx2op
				i.op = BIT(o, 11) ? OP_SUB : OP_ADD;
				i.a = r3(side, BIT(o, 13, 3));
				i.b = r3(xside, BIT(o, 7, 3));
				i.dst = i.a;
				cross();
			}
			else
			{
				// Dx5
				i.op = OP_ADDAW;
				i.a = 32 + 15;
				i.cst = (BIT(o, 11, 2) << 3) | BIT(o, 13, 3);
				i.flags |= F_B_CST;
				i.dst = r3(side, BIT(o, 7, 3));
				if (!s)
					i.flags |= F_CROSS;
			}
			return;
		}
		return;

	case 3:
		break;
	}

	// bits 6:5 = 11
	u32 const op3 = BIT(o, 13, 3);
	u32 const rd = r3(side, BIT(o, 7, 3));
	u32 const sub = BIT(o, 10, 3);
	if (sub == 6)
	{
		// LSDx1 and the unit-specific Dx1/Lx1/Sx1
		i.unit = u;
		i.dst = rd;
		switch (op3)
		{
		case 0: i.op = OP_MVK; i.cst = 0; i.flags |= F_A_CST; return;
		case 1: i.op = OP_MVK; i.cst = 1; i.flags |= F_A_CST; return;
		case 5: i.op = OP_ADD; i.a = rd; i.cst = 1; i.flags |= F_B_CST; return;
		case 7: i.op = OP_XOR; i.a = rd; i.cst = 1; i.flags |= F_B_CST; return;
		}
		switch (unit)
		{
		case 0: // Lx1
		case 1: // Sx1
			if (op3 == 2)
			{
				i.op = OP_SUB;
				i.cst = 0;
				i.flags |= F_A_CST;
				i.b = rd;
			}
			else if (op3 == 3)
			{
				i.op = OP_ADD;
				i.cst = -1;
				i.flags |= F_A_CST;
				i.b = rd;
			}
			else if ((op3 == 6) && (unit == 1))
			{
				i.op = OP_MVC_TO;
				i.b = rd;
				i.cst = 0x0d; // ILC
				i.cst2 = 0;
			}
			return;
		case 2: // Dx1
			if (op3 == 3)
			{
				i.op = OP_SUB;
				i.a = rd;
				i.cst = 1;
				i.flags |= F_B_CST;
			}
			return;
		}
		return;
	}
	if (sub == 2)
	{
		// LSDx1c
		static constexpr u8 CREGS[4] = { 0 + 1, 0 + 1, 32 + 1, 32 + 1 };
		i.op = OP_MVK;
		i.unit = u;
		i.creg = CREGS[BIT(o, 14, 2)];
		i.z = BIT(o, 14);
		i.cst = BIT(o, 13);
		i.flags |= F_A_CST;
		i.dst = rd;
		return;
	}

	switch (unit)
	{
	case 0: // loop buffer instructions in the .L space
		{
			u32 const op4 = BIT(o, 10, 4);
			i.flags = F_COMPACT;
			switch (op4)
			{
			case 0x3: // Uspl/Uspldr
				i.cst = ((BIT(o, 14) << 3) | BIT(o, 7, 3)) + 1;
				if (!BIT(o, 15))
					i.op = BIT(o, 0) ? OP_SPLOOPD : OP_SPLOOP;
				else
				{
					i.op = OP_SPLOOPD;
					i.creg = BIT(o, 0) ? (32 + 1) : (0 + 1);
				}
				return;
			case 0x7: // Uspk
				i.op = OP_SPKERNEL;
				i.cst = (BIT(o, 0) << 5) | (BIT(o, 14, 2) << 3) | BIT(o, 7, 3);
				return;
			case 0xb: // SPMASK
			case 0xf: // SPMASKR
				i.op = (op4 == 0xb) ? OP_SPMASK : OP_SPMASKR;
				// mask bits as in the 32-bit form: L1 L2 S1 S2 D1 D2 M1 M2
				i.cst = BIT(o, 0) | (BIT(o, 7) << 1) | (BIT(o, 8) << 2) | (BIT(o, 9) << 3) | (BIT(o, 14) << 4) | (BIT(o, 15) << 5);
				return;
			}
		}
		return;

	case 1: // .S space
		if (BIT(o, 11, 2) == 0)
		{
			// Sx1b
			i.op = OP_BNOP_REG;
			i.unit = s ? UNIT_S2 : UNIT_S1;
			i.b = 32 + BIT(o, 7, 4);
			i.cst2 = BIT(o, 13, 3);
			if (!s)
				i.flags |= F_CROSS;
		}
		else if (BIT(o, 0, 13) == 0x0c6e)
		{
			i.op = OP_NOP;
			i.flags = F_COMPACT;
			i.cst = BIT(o, 13, 3) + 1;
		}
		return;

	case 2: // .D space
		if (!BIT(o, 11))
		{
			// Dpp
			u32 const r = (BIT(o, 12) ? 32 : 0) + BIT(o, 7, 4);
			bool const dw = BIT(o, 15);
			bool const ld = BIT(o, 14);
			i.unit = UNIT_D2;
			i.base = 32 + 15;
			i.cst = BIT(o, 13) + 1;
			i.dst = r;
			if (dw)
			{
				i.op = ld ? OP_LDDW : OP_STDW;
				i.flags |= F_D_PAIR;
			}
			else
				i.op = ld ? OP_LDW : OP_STW;
			i.mode = ld ? AM_PREINC : AM_POSTDEC;
			if (BIT(header, 20) && ld)
				i.flags |= F_PROT;
		}
		else if (BIT(o, 10, 3) == 3)
		{
			// Dx5p
			i.op = BIT(o, 7) ? OP_SUBAW : OP_ADDAW;
			i.unit = UNIT_D2;
			i.a = 32 + 15;
			i.dst = 32 + 15;
			i.cst = (BIT(o, 8, 2) << 3) | BIT(o, 13, 3);
			i.flags |= F_B_CST;
		}
		return;
	}
}

std::string tms320c6x_disassembler::reg_name(u32 r, bool pair)
{
	char const side = (r & 32) ? 'B' : 'A';
	u32 const n = r & 31;
	if (pair)
		return util::string_format("%c%u:%c%u", side, n + 1, side, n);
	else
		return util::string_format("%c%u", side, n);
}

void tms320c6x_disassembler::format(std::ostream &stream, insn const &i)
{
	static constexpr char const *UNITS[] = { "", ".L1", ".L2", ".S1", ".S2", ".M1", ".M2", ".D1", ".D2" };

	auto const cst = [] (s32 v) -> std::string
	{
		if ((v > -16) && (v < 16))
			return util::string_format("%d", v);
		return util::string_format("0x%x", u32(v));
	};
	auto const a = [&i, &cst] () -> std::string
	{
		if (i.flags & F_A_CST)
			return cst(i.cst);
		return reg_name(i.a, i.flags & F_A_PAIR);
	};
	auto const b = [&i, &cst] () -> std::string
	{
		if (i.flags & F_B_CST)
			return cst(i.cst);
		return reg_name(i.b, i.flags & F_B_PAIR);
	};
	auto const d = [&i] () -> std::string { return reg_name(i.dst, i.flags & F_D_PAIR); };

	std::string mnem = mnemonic(i.op);
	std::string ops;
	op_t op = i.op;

	// aliases
	bool const a_zero = (i.flags & F_A_CST) && (i.cst == 0);
	bool const b_zero = (i.flags & F_B_CST) && (i.cst == 0);
	bool const a_m1 = (i.flags & F_A_CST) && (i.cst == -1);
	if (((op == OP_ADD) || (op == OP_OR) || (op == OP_ADD_IL)) && (a_zero || b_zero) && !(i.flags & F_COMPACT))
	{
		mnem = "MV";
		ops = (a_zero ? b() : a()) + "," + d();
		op = OP_MV;
	}
	else if ((op == OP_ADD) && (i.flags & F_A_CST) && !(i.flags & F_B_CST))
	{
		// dis6x shows a negative constant as SUB, except in compact .L
		// instructions, and puts the constant second in compact instructions
		bool const compact = i.flags & F_COMPACT;
		bool const lunit = (i.unit == UNIT_L1) || (i.unit == UNIT_L2);
		if ((i.cst < 0) && (!compact || !lunit))
		{
			mnem = "SUB";
			ops = b() + "," + cst(-i.cst) + "," + d();
		}
		else if (compact)
			ops = b() + "," + a() + "," + d();
		else
			ops = a() + "," + b() + "," + d();
	}
	else if (((op == OP_SUB) || (op == OP_SUB_IIL)) && !(i.flags & (F_A_CST | F_B_CST)) && (i.a == i.b))
	{
		mnem = "ZERO";
		ops = d();
		op = OP_MV;
	}
	else if (((op == OP_SUB) || (op == OP_SUB_IL)) && a_zero)
	{
		mnem = "NEG";
		ops = b() + "," + d();
		op = OP_MV;
	}
	else if ((op == OP_XOR) && a_m1)
	{
		mnem = "NOT";
		ops = b() + "," + d();
		op = OP_MV;
	}

	std::string unit = UNITS[i.unit];
	if (is_load(op) || is_store(op))
		unit += util::string_format("T%u", (i.dst & 32) ? 2 : 1);
	else if (i.flags & F_CROSS)
		unit += "X";

	if (ops.empty())
	{
		switch (op)
		{
		case OP_INVALID:
			break;

		case OP_NOP:
			if ((i.cst > 1) || (i.flags & F_COMPACT))
				ops = util::string_format("%d", i.cst);
			break;

		case OP_IDLE: case OP_DINT: case OP_RINT: case OP_SWE: case OP_SWENR: case OP_SPKERNELR:
		case OP_B_IRP: case OP_B_NRP:
			if (op == OP_B_IRP)
				ops = "IRP";
			else if (op == OP_B_NRP)
				ops = "NRP";
			break;

		case OP_SPLOOP: case OP_SPLOOPD: case OP_SPLOOPW:
			ops = util::string_format("%d", i.cst);
			break;

		case OP_SPKERNEL:
			ops = util::string_format("%d", i.cst);
			break;

		case OP_SPMASK: case OP_SPMASKR:
			{
				static constexpr char const *NAMES[8] = { "L1", "L2", "S1", "S2", "D1", "D2", "M1", "M2" };
				for (u32 n = 0; n < 8; n++)
				{
					if (BIT(i.cst, n))
					{
						if (!ops.empty())
							ops += ",";
						ops += NAMES[n];
					}
				}
			}
			break;

		case OP_B: case OP_CALLP:
			ops = util::string_format("0x%08x", u32(i.cst));
			if (op == OP_CALLP)
				ops += "," + d();
			break;

		case OP_BNOP:
			ops = util::string_format("0x%08x,%d", u32(i.cst), i.cst2);
			break;

		case OP_B_REG:
			ops = b();
			break;

		case OP_BNOP_REG:
			ops = b() + util::string_format(",%d", i.cst2);
			break;

		case OP_BDEC: case OP_BPOS:
			ops = util::string_format("0x%08x,", u32(i.cst)) + d();
			break;

		case OP_ADDKPC:
			ops = util::string_format("0x%08x,", u32(i.cst)) + d() + util::string_format(",%d", i.cst2);
			break;

		case OP_MVC_TO:
			{
				char const *const name = control_register_name(i.cst, i.cst2, true);
				ops = b() + "," + (name ? name : util::string_format("CR%u", i.cst));
			}
			break;

		case OP_MVC_FROM:
			{
				char const *const name = control_register_name(i.cst, i.cst2, false);
				ops = std::string(name ? name : util::string_format("CR%u", i.cst)) + "," + d();
			}
			break;

		case OP_MVK: case OP_MVKH: case OP_ADDK:
			ops = cst(i.cst) + "," + d();
			break;

		case OP_EXT: case OP_EXTU: case OP_SET: case OP_CLR:
			if (i.flags & F_B_CST)
				ops = reg_name(i.a, false) + util::string_format(",%d,%d,", i.cst, i.cst2) + d();
			else
				ops = a() + "," + b() + "," + d();
			break;

		case OP_LDB: case OP_LDBU: case OP_LDH: case OP_LDHU: case OP_LDW: case OP_LDNW: case OP_LDDW: case OP_LDNDW:
		case OP_STB: case OP_STH: case OP_STW: case OP_STNW: case OP_STDW: case OP_STNDW:
			{
				std::string const base = reg_name(i.base, false);
				std::string offset = i.offset_reg ? reg_name(i.a, false) : util::string_format("%d", i.cst);
				offset = i.scaled ? ("[" + offset + "]") : ("(" + offset + ")");
				std::string addr;
				switch (i.mode)
				{
				case AM_NEG_OFFSET: addr = "*-" + base + offset; break;
				case AM_POS_OFFSET: addr = "*+" + base + offset; break;
				case AM_PREDEC: addr = "*--" + base + offset; break;
				case AM_PREINC: addr = "*++" + base + offset; break;
				case AM_POSTDEC: addr = "*" + base + "--" + offset; break;
				case AM_POSTINC: addr = "*" + base + "++" + offset; break;
				}
				if (is_load(op))
					ops = addr + "," + d();
				else
					ops = d() + "," + addr;
			}
			break;

		default:
			if (i.flags & F_NOA)
				ops = b() + "," + d();
			else if ((i.flags & (F_A_CST | F_B_CST)) == F_B_CST)
				ops = a() + "," + b() + "," + d();
			else if (i.flags & F_A_CST)
				ops = a() + "," + reg_name(i.b, i.flags & F_B_PAIR) + "," + d();
			else
				ops = a() + "," + b() + "," + d();
			break;
		}
	}

	if (i.creg)
		util::stream_format(stream, "[%c%c%u] ", i.z ? '!' : ' ', ((i.creg - 1) & 32) ? 'B' : 'A', (i.creg - 1) & 31);
	if (ops.empty())
		stream << mnem + unit;
	else
		util::stream_format(stream, "%-13s %s", mnem + unit, ops);
}

u32 tms320c6x_disassembler::opcode_alignment() const
{
	return 2;
}

offs_t tms320c6x_disassembler::disassemble(std::ostream &stream, offs_t pc, data_buffer const &opcodes, data_buffer const &params)
{
	u32 const fp = pc & ~u32(31);
	u32 const header = opcodes.r32(fp + 28);
	u32 const offset = pc & 31;

	if (!is_header(header))
	{
		insn i;
		decode32(opcodes.r32(pc & ~u32(3)), fp, false, i);

		// previous instruction's p-bit
		bool const prev_p = offset ? BIT(opcodes.r32(pc - 4), 0) : last_p(fp, opcodes);

		if (prev_p)
			stream << "|| ";
		if (i.op == OP_INVALID)
			util::stream_format(stream, ".word 0x%08x", opcodes.r32(pc & ~u32(3)));
		else
			format(stream, i);
		return 4 | SUPPORTED;
	}

	if (offset == 28)
	{
		static constexpr char const *SECONDARY[8] = { "BU", "B", "HU", "H", "W", "B", "NW", "H" };
		std::string layout;
		for (int n = 6; n >= 0; n--)
			layout += BIT(header, 21 + n) ? '1' : '0';
		util::stream_format(stream, ".fphead %s, %s, %s, %s, %s, %s, %sb",
				BIT(header, 20) ? "p" : "n",
				BIT(header, 19) ? "h" : "l",
				BIT(header, 18) ? "DW/NDW" : "W",
				SECONDARY[BIT(header, 16, 3)],
				BIT(header, 15) ? "br" : "nobr",
				BIT(header, 14) ? "sat" : "nosat",
				layout);
		return 4 | SUPPORTED;
	}

	u32 const word = offset >> 2;
	bool const compact = BIT(header, 21 + word);

	// p-bit of the previous instruction in this fetch packet
	bool prev_p = false;
	if (!offset)
		prev_p = last_p(fp, opcodes);
	else if (compact && BIT(pc, 1))
		prev_p = BIT(header, word * 2);
	else if (word)
	{
		if (BIT(header, 21 + word - 1))
			prev_p = BIT(header, (word - 1) * 2 + 1);
		else
			prev_p = BIT(opcodes.r32(fp + (word - 1) * 4), 0);
	}
	if (prev_p)
		stream << "|| ";

	insn i;
	if (compact)
		decode16(opcodes.r16(pc & ~u32(1)), header, fp, i);
	else
	{
		decode32(opcodes.r32(pc & ~u32(3)), fp, true, i);
		if (BIT(header, 20) && is_load(i.op))
			i.flags |= F_PROT;
	}

	if (i.op == OP_INVALID)
	{
		if (compact)
			util::stream_format(stream, ".half 0x%04x", opcodes.r16(pc & ~u32(1)));
		else
			util::stream_format(stream, ".word 0x%08x", opcodes.r32(pc & ~u32(3)));
	}
	else
		format(stream, i);

	return (compact ? 2 : 4) | SUPPORTED;
}
