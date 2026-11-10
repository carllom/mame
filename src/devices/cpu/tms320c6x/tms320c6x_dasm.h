// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6X_DASM_H
#define MAME_CPU_TMS320C6X_TMS320C6X_DASM_H

#pragma once

class tms320c6x_disassembler : public util::disasm_interface
{
public:
	// functional units
	enum : u8
	{
		UNIT_NONE,
		UNIT_L1, UNIT_L2,
		UNIT_S1, UNIT_S2,
		UNIT_M1, UNIT_M2,
		UNIT_D1, UNIT_D2
	};

	// operations; forms with different operand types have their own entry
#define TMS320C6X_OPS(X) \
	X(INVALID, "?") \
	X(ABS, "ABS") X(ABS_L, "ABS") X(ABS2, "ABS2") X(ABSDP, "ABSDP") X(ABSSP, "ABSSP") \
	X(ADD, "ADD") X(ADD_IIL, "ADD") X(ADD_IL, "ADD") X(ADDU_IIL, "ADDU") X(ADDU_IL, "ADDU") \
	X(ADDAB, "ADDAB") X(ADDAH, "ADDAH") X(ADDAW, "ADDAW") X(ADDAD, "ADDAD") \
	X(ADDDP, "ADDDP") X(ADDSP, "ADDSP") X(ADDK, "ADDK") X(ADDKPC, "ADDKPC") \
	X(ADDSUB, "ADDSUB") X(ADDSUB2, "ADDSUB2") X(ADD2, "ADD2") X(ADD4, "ADD4") \
	X(AND, "AND") X(ANDN, "ANDN") X(AVG2, "AVG2") X(AVGU4, "AVGU4") \
	X(B, "B") X(B_REG, "B") X(B_IRP, "B") X(B_NRP, "B") X(BNOP, "BNOP") X(BNOP_REG, "BNOP") \
	X(BDEC, "BDEC") X(BPOS, "BPOS") X(CALLP, "CALLP") \
	X(BITC4, "BITC4") X(BITR, "BITR") \
	X(CLR, "CLR") X(EXT, "EXT") X(EXTU, "EXTU") X(SET, "SET") \
	X(CMPEQ, "CMPEQ") X(CMPEQ_L, "CMPEQ") X(CMPGT, "CMPGT") X(CMPGT_L, "CMPGT") \
	X(CMPGTU, "CMPGTU") X(CMPGTU_L, "CMPGTU") X(CMPLT, "CMPLT") X(CMPLT_L, "CMPLT") \
	X(CMPLTU, "CMPLTU") X(CMPLTU_L, "CMPLTU") \
	X(CMPEQ2, "CMPEQ2") X(CMPEQ4, "CMPEQ4") X(CMPGT2, "CMPGT2") X(CMPGTU4, "CMPGTU4") \
	X(CMPEQDP, "CMPEQDP") X(CMPGTDP, "CMPGTDP") X(CMPLTDP, "CMPLTDP") \
	X(CMPEQSP, "CMPEQSP") X(CMPGTSP, "CMPGTSP") X(CMPLTSP, "CMPLTSP") \
	X(CMPY, "CMPY") X(CMPYR, "CMPYR") X(CMPYR1, "CMPYR1") \
	X(DDOTP4, "DDOTP4") X(DDOTPH2, "DDOTPH2") X(DDOTPH2R, "DDOTPH2R") X(DDOTPL2, "DDOTPL2") X(DDOTPL2R, "DDOTPL2R") \
	X(DEAL, "DEAL") X(DINT, "DINT") X(DMV, "DMV") X(DOTP2, "DOTP2") X(DOTP2_LL, "DOTP2") X(DOTPN2, "DOTPN2") \
	X(DOTPNRSU2, "DOTPNRSU2") X(DOTPRSU2, "DOTPRSU2") X(DOTPSU4, "DOTPSU4") X(DOTPU4, "DOTPU4") \
	X(DPACK2, "DPACK2") X(DPACKX2, "DPACKX2") X(DPINT, "DPINT") X(DPSP, "DPSP") X(DPTRUNC, "DPTRUNC") \
	X(GMPY, "GMPY") X(GMPY4, "GMPY4") X(IDLE, "IDLE") \
	X(INTDP, "INTDP") X(INTDPU, "INTDPU") X(INTSP, "INTSP") X(INTSPU, "INTSPU") \
	X(LDB, "LDB") X(LDBU, "LDBU") X(LDH, "LDH") X(LDHU, "LDHU") X(LDW, "LDW") X(LDNW, "LDNW") \
	X(LDDW, "LDDW") X(LDNDW, "LDNDW") \
	X(LMBD, "LMBD") X(MAX2, "MAX2") X(MAXU4, "MAXU4") X(MIN2, "MIN2") X(MINU4, "MINU4") \
	X(MPY, "MPY") X(MPYH, "MPYH") X(MPYHL, "MPYHL") X(MPYHLU, "MPYHLU") X(MPYHSLU, "MPYHSLU") \
	X(MPYHSU, "MPYHSU") X(MPYHU, "MPYHU") X(MPYHULS, "MPYHULS") X(MPYHUS, "MPYHUS") \
	X(MPYLH, "MPYLH") X(MPYLHU, "MPYLHU") X(MPYLSHU, "MPYLSHU") X(MPYLUHS, "MPYLUHS") \
	X(MPYSU, "MPYSU") X(MPYU, "MPYU") X(MPYUS, "MPYUS") \
	X(SMPY, "SMPY") X(SMPYH, "SMPYH") X(SMPYHL, "SMPYHL") X(SMPYLH, "SMPYLH") \
	X(MPYI, "MPYI") X(MPYID, "MPYID") X(MPYDP, "MPYDP") X(MPYSP, "MPYSP") X(MPYSPDP, "MPYSPDP") X(MPYSP2DP, "MPYSP2DP") \
	X(MPYHI, "MPYHI") X(MPYHIR, "MPYHIR") X(MPYLI, "MPYLI") X(MPYLIR, "MPYLIR") \
	X(MPY32, "MPY32") X(MPY32_64, "MPY32") X(MPY32SU, "MPY32SU") X(MPY32U, "MPY32U") X(MPY32US, "MPY32US") \
	X(MPYSU4, "MPYSU4") X(MPYU4, "MPYU4") X(MPY2, "MPY2") X(MPY2IR, "MPY2IR") X(SMPY2, "SMPY2") X(SMPY32, "SMPY32") \
	X(MV, "MV") X(MVC_TO, "MVC") X(MVC_FROM, "MVC") X(MVD, "MVD") X(MVK, "MVK") X(MVKH, "MVKH") \
	X(NOP, "NOP") X(NORM, "NORM") X(NORM_L, "NORM") X(OR, "OR") \
	X(PACK2, "PACK2") X(PACKH2, "PACKH2") X(PACKH4, "PACKH4") X(PACKHL2, "PACKHL2") X(PACKLH2, "PACKLH2") X(PACKL4, "PACKL4") \
	X(RCPDP, "RCPDP") X(RCPSP, "RCPSP") X(RINT, "RINT") X(ROTL, "ROTL") X(RPACK2, "RPACK2") X(RSQRDP, "RSQRDP") X(RSQRSP, "RSQRSP") \
	X(SADD, "SADD") X(SADD_L, "SADD") X(SADD2, "SADD2") X(SADDSUB, "SADDSUB") X(SADDSUB2, "SADDSUB2") \
	X(SADDUS2, "SADDUS2") X(SADDU4, "SADDU4") X(SAT, "SAT") \
	X(SHFL, "SHFL") X(SHFL3, "SHFL3") X(SHL, "SHL") X(SHL_L, "SHL") X(SHL_UL, "SHL") X(SHLMB, "SHLMB") \
	X(SHR, "SHR") X(SHR_L, "SHR") X(SHR2, "SHR2") X(SHRMB, "SHRMB") X(SHRU, "SHRU") X(SHRU_L, "SHRU") X(SHRU2, "SHRU2") \
	X(SPACK2, "SPACK2") X(SPACKU4, "SPACKU4") X(SPDP, "SPDP") X(SPINT, "SPINT") \
	X(SPKERNEL, "SPKERNEL") X(SPKERNELR, "SPKERNELR") X(SPLOOP, "SPLOOP") X(SPLOOPD, "SPLOOPD") X(SPLOOPW, "SPLOOPW") \
	X(SPMASK, "SPMASK") X(SPMASKR, "SPMASKR") X(SPTRUNC, "SPTRUNC") \
	X(SSHL, "SSHL") X(SSHVL, "SSHVL") X(SSHVR, "SSHVR") X(SSUB, "SSUB") X(SSUB_L, "SSUB") X(SSUB2, "SSUB2") \
	X(STB, "STB") X(STH, "STH") X(STW, "STW") X(STNW, "STNW") X(STDW, "STDW") X(STNDW, "STNDW") \
	X(SUB, "SUB") X(SUB_IIL, "SUB") X(SUB_IL, "SUB") X(SUBU_IIL, "SUBU") X(SUBAB, "SUBAB") X(SUBAH, "SUBAH") X(SUBAW, "SUBAW") \
	X(SUBC, "SUBC") X(SUBDP, "SUBDP") X(SUBSP, "SUBSP") X(SUB2, "SUB2") X(SUB4, "SUB4") X(SUBABS4, "SUBABS4") \
	X(SWAP4, "SWAP4") X(SWE, "SWE") X(SWENR, "SWENR") X(UNPKHU4, "UNPKHU4") X(UNPKLU4, "UNPKLU4") \
	X(XOR, "XOR") X(XORMPY, "XORMPY") X(XPND2, "XPND2") X(XPND4, "XPND4")

	enum op_t : u16
	{
#define TMS320C6X_OP_ENUM(name, mnemonic) OP_##name,
		TMS320C6X_OPS(TMS320C6X_OP_ENUM)
#undef TMS320C6X_OP_ENUM
		OP_COUNT
	};

	// operand flags
	static constexpr u16 F_A_CST = 1 << 0;   // a is a constant
	static constexpr u16 F_B_CST = 1 << 1;   // b is a constant
	static constexpr u16 F_A_PAIR = 1 << 2;  // a is a register pair
	static constexpr u16 F_B_PAIR = 1 << 3;  // b is a register pair
	static constexpr u16 F_D_PAIR = 1 << 4;  // dst is a register pair
	static constexpr u16 F_CROSS = 1 << 5;   // a source uses the cross path
	static constexpr u16 F_NOA = 1 << 6;     // no a operand (unary)
	static constexpr u16 F_NOD = 1 << 7;     // no destination
	static constexpr u16 F_PROT = 1 << 8;    // load in a protected fetch packet (implied NOP 4)
	static constexpr u16 F_COMPACT = 1 << 9; // 16-bit compact instruction

	// load/store addressing
	enum : u8
	{
		AM_NEG_OFFSET,      // *-R[o]
		AM_POS_OFFSET,      // *+R[o]
		AM_PREDEC,          // *--R[o]
		AM_PREINC,          // *++R[o]
		AM_POSTDEC,         // *R--[o]
		AM_POSTINC          // *R++[o]
	};

	// a decoded instruction; registers are 0-31 for A0-A31 and 32-63 for B0-B31
	struct insn
	{
		op_t op = OP_INVALID;
		u8 unit = UNIT_NONE;
		u8 creg = 0;        // condition register + 1, or 0 for unconditional
		bool z = false;     // condition is "register is zero"
		bool p = false;     // next instruction executes in parallel
		u8 size = 4;        // 2 or 4 bytes
		u16 flags = 0;
		u8 dst = 0;         // destination (data register for loads and stores)
		u8 a = 0;           // first source in assembly order
		u8 b = 0;           // second source in assembly order
		s32 cst = 0;        // constant source, branch target, field constants
		s32 cst2 = 0;       // second field constant, NOP count after branches
		u8 mode = 0;        // addressing mode
		u8 base = 0;        // base register
		bool offset_reg = false;
		bool scaled = true; // offset scaled by the access size
	};

	tms320c6x_disassembler() = default;

	virtual u32 opcode_alignment() const override;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, data_buffer const &opcodes, data_buffer const &params) override;

	static bool is_header(u32 word) { return (word >> 28) == 0xe; }
	static void decode32(u32 opcode, u32 pce1, bool header_fp, insn &i);
	static void decode16(u16 opcode, u32 header, u32 pce1, insn &i);
	static char const *mnemonic(op_t op);
	static char const *control_register_name(u32 crlo, u32 crhi, bool write);

private:
	static std::string reg_name(u32 r, bool pair);
	static void format(std::ostream &stream, insn const &i);
};

#endif // MAME_CPU_TMS320C6X_TMS320C6X_DASM_H
