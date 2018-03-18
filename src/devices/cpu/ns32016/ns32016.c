/*****************************************************************************
 *
 *   ns32016.c
 *   NS32016
 *
 *****************************************************************************/

#define DEBUG_MEM

#include "emu.h"
#include "debugger.h"
#include "ns32016.h"

#define __INS_R(off) (m_program->read_byte(m_regs.pc + off))
#define __INS_RS(off,sh) (m_program->read_byte(m_regs.pc + off) << sh)
#define INS_OP8(off)  __INS_R(off)
#define INS_OP16(off) (__INS_RS(off,8) | __INS_R(off+1))
#define INS_OP32(off) (__INS_RS(off,24) | __INS_RS(off+1,16) | __INS_RS(off+2,8) | __INS_R(off+3))

#define __MASK_WIDTH_BYTE  0x000000FF
#define __MASK_WIDTH_WORD  0x0000FFFF
#define __MASK_WIDTH_DWORD 0xFFFFFFFF
#define __MASK_WIDTH (width == NS32016_WIDTH_BYTE ? __MASK_WIDTH_BYTE : width == NS32016_WIDTH_WORD ? __MASK_WIDTH_WORD : __MASK_WIDTH_DWORD)
#define MASK_WIDTH_W(dest, value) dest = ((dest & ~__MASK_WIDTH) | (value & __MASK_WIDTH))
#define MASK_WIDTH_R(x) (x & (width == NS32016_WIDTH_BYTE ? 0x000000FF : width == NS32016_WIDTH_WORD ? 0x0000FFFF : 0xFFFFFFFF))
#define WIDTH_BYTES(width) (width == NS32016_WIDTH_BYTE ? 1 : width == NS32016_WIDTH_WORD ? 2 : 4)
#define WIDTH_BITS(width) (width == NS32016_WIDTH_BYTE ? 8 : width == NS32016_WIDTH_WORD ? 16 : 32)

#define __MASK_MSB_BYTE  0x00000080
#define __MASK_MSB_WORD  0x00008000
#define __MASK_MSB_DWORD 0x80000000
#define MASK_MSB(width) (width == NS32016_WIDTH_BYTE ? __MASK_MSB_BYTE : width == NS32016_WIDTH_WORD ? __MASK_MSB_WORD : __MASK_MSB_DWORD)

#define SIGN_X(v, width) (width == NS32016_WIDTH_BYTE ? (INT8)v : width == NS32016_WIDTH_WORD ? (INT16)v : (INT32)v)
#define WIDTH(v, width) (width == NS32016_WIDTH_BYTE ? (UINT8)v : width == NS32016_WIDTH_WORD ? (UINT16)v : (UINT32)v)
#define SIGNED(x,w) (w == NS32016_WIDTH_BYTE ? (INT8)x : w == NS32016_WIDTH_WORD ? (INT16)x : (INT32)x)
#define UNSIGNED(x,w) (w == NS32016_WIDTH_BYTE ? (UINT8)x : w == NS32016_WIDTH_WORD ? (UINT16)x : (UINT32)x)

#define FMT0_COND(x) ((x >> 4) & 0xF)

#define FMT1_OP      ((m_ins >> 4) & 0xF)

#define FMT2_I       (m_ins & 0x3)
#define FMT2_OP      ((m_ins >> 4) & 0x7)
#define FMT2_SHRT    ((m_ins >> 7) & 0xF)
#define FMT2_SHRT_SX (INT32)((m_ins & 0x0400 ? 0xFFFFFFF0 : 0) | FMT2_SHRT)
#define FMT2_GEN     ((m_ins >> 11) & 0x1F)

#define FMT3_I       (m_ins & 0x3)
#define FMT3_OP      ((m_ins >> 7) & 0xF)
#define FMT3_GEN     ((m_ins >> 11) & 0x1F)


#define FMT4_I       (m_ins & 0x3)
#define FMT4_OP      ((m_ins >> 2) & 0xF)
#define FMT4_GEN2    ((m_ins >> 6) & 0x1F)
#define FMT4_GEN1    ((m_ins >> 11) & 0x1F)

#define FMT5_I       ((m_ins >> 8) & 0x3)
#define FMT5_OP      ((m_ins >> 10) & 0xF)
#define FMT5_SHRT    ((m_ins >> 15) & 0xF)
#define FMT5_SHRT_MASK 0x00780000
#define FMT5_T       (m_ins & 0x8000)
#define FMT5_B       (m_ins & 0x10000)
#define FMT5_UW_NONE ((m_ins & 0x60000) == 0)
#define FMT5_UW_W    ((m_ins & 0x60000) == 0x20000)
#define FMT5_UW_U	 ((m_ins & 0x60000) == 0x60000)

#define FMT6_I       ((m_ins >> 8) & 0x3)
#define FMT6_OP      ((m_ins >> 10) & 0xF)
#define FMT6_GEN2    ((m_ins >> 14) & 0x1F)
#define FMT6_GEN1    ((m_ins >> 19) & 0x1F)

#define FMT7_I       ((m_ins >> 8) & 0x3)
#define FMT7_OP      ((m_ins >> 10) & 0xF)
#define FMT7_GEN2    ((m_ins >> 14) & 0x1F)
#define FMT7_GEN1    ((m_ins >> 19) & 0x1F)


#define PSR_C_MASK 0x0001
#define PSR_T_MASK 0x0002
#define PSR_L_MASK 0x0004
#define PSR_F_MASK 0x0020
#define PSR_Z_MASK 0x0040
#define PSR_N_MASK 0x0080
#define PSR_U_MASK 0x0100
#define PSR_S_MASK 0x0200
#define PSR_P_MASK 0x0400
#define PSR_I_MASK 0x0800

#define PSR_C (m_regs.psr & PSR_C_MASK)
#define PSR_T (m_regs.psr & PSR_T_MASK)
#define PSR_L (m_regs.psr & PSR_L_MASK)
#define PSR_F (m_regs.psr & PSR_F_MASK)
#define PSR_Z (m_regs.psr & PSR_Z_MASK)
#define PSR_N (m_regs.psr & PSR_N_MASK)
#define PSR_U (m_regs.psr & PSR_U_MASK)
#define PSR_S (m_regs.psr & PSR_S_MASK)
#define PSR_P (m_regs.psr & PSR_P_MASK)
#define PSR_I (m_regs.psr & PSR_I_MASK)

#define PSR_SET(msk) { m_regs.psr |= msk; }
#define PSR_CLR(msk) { m_regs.psr &= ~msk; }

#define CALC_C(mask,a,b,r) { if ((((a&b) | (a^b)) & ~r) & mask) PSR_SET(PSR_C_MASK) else PSR_CLR(PSR_C_MASK) }
#define CALC_F(mask,a,b,r) { if ((~(a^b) & (b^r)) & mask) PSR_SET(PSR_F_MASK) else PSR_CLR(PSR_F_MASK) }
#define CALC_Z(x) { if (x == 0) PSR_SET(PSR_Z_MASK) else PSR_CLR(PSR_Z_MASK) }
#define CALC_N(x) { if (x < 0) PSR_SET(PSR_N_MASK) else PSR_CLR(PSR_N_MASK) }
#define CALC_L(a,b) { if ((UINT32)a > (UINT32)b) PSR_SET(PSR_L_MASK) else PSR_CLR(PSR_L_MASK) }

#define __SP_ACTIVE ((m_regs.psr >> 9) & 1)
#define REG_SP m_regs.sp[__SP_ACTIVE]

#define IMM_CONS3(x) ((x >> 5) & 7)
#define IMM_CONS5(x) (x & 0x1F)

#ifdef DEBUG_MEM
	#define __maskaddr(a) (a < 0x4000)
	UINT32 ns32016_device::MEM_R_DW(UINT32 a) { UINT32 x = m_program->read_dword_unaligned(a); if(!__maskaddr(a)) logerror("@%06x MEMR @%06X => %08X\n", m_regs.pc, a, x); return x; }
	UINT16 ns32016_device::MEM_R_W(UINT32 a) { UINT16 x = m_program->read_word_unaligned(a); if(!__maskaddr(a)) logerror("@%06x MEMR @%06X => %04X\n", m_regs.pc, a, x); return x; }
	UINT8 ns32016_device::MEM_R_B(UINT32 a) { UINT8 x = m_program->read_byte(a); if(!__maskaddr(a)) logerror("@%06x MEMR @%06X => %02X\n", m_regs.pc, a, x); return x; }
	#define MEM_R(a,w) (w == NS32016_WIDTH_BYTE ? MEM_R_B(a) : w == NS32016_WIDTH_WORD ? MEM_R_W(a) : MEM_R_DW(a))
#else
	#define MEM_R_DW(x) m_program->read_dword_unaligned(x)
	#define MEM_R_W(x) m_program->read_word_unaligned(x)
	#define MEM_R_B(x) m_program->read_byte(x)
#endif

#define MEM_W_DW(a,x) {logerror("@%06x MEMW @%06X <= %08X\n", m_regs.pc, a, x); m_program->write_dword_unaligned(a, x);}
#define MEM_W_W(a,x) {logerror("@%06x MEMW @%06X <= %04X\n", m_regs.pc, a, x & 0x0000FFFF); m_program->write_word_unaligned(a, x & 0x0000FFFF);}
#define MEM_W_B(a,x) {logerror("@%06x MEMW @%06X <= %02X\n", m_regs.pc, a, x & 0x000000FF); m_program->write_byte(a, x & 0x000000FF);}
#define MEM_W(a,x,w) { if (w == NS32016_WIDTH_BYTE) MEM_W_B(a,x) else if (w == NS32016_WIDTH_WORD) MEM_W_W(a,x) else MEM_W_DW(a,x) }

// No logging for stack operations - use memory directly
#define PUSHB(X) { REG_SP-=1; m_program->write_byte(REG_SP, X); }
#define PUSHW(X) { REG_SP-=2; m_program->write_word_unaligned(REG_SP, X); }
#define PUSHD(X) { REG_SP-=4; m_program->write_dword_unaligned(REG_SP, X); }
#define POPB(X) { X = m_program->read_byte(REG_SP); REG_SP+=1; }
#define POPW(X) { X = m_program->read_word_unaligned(REG_SP); REG_SP+=2; }
#define POPD(X) { X = m_program->read_dword_unaligned(REG_SP); REG_SP+=4; }

#define Unimpl(msg) { logerror("@%06X: %s\n", m_regs.pc, msg); assert_always(false, msg); }

static const UINT32 field_len_mask[] = {
	0x00000001, 0x00000003, 0x00000007, 0x0000000F, 0x0000001F, 0x0000003F, 0x0000007F, 0x000000FF,
	0x000001FF, 0x000003FF, 0x000007FF, 0x00000FFF, 0x00001FFF, 0x00003FFF, 0x00007FFF, 0x0000FFFF,
	0x0001FFFF, 0x0003FFFF, 0x0007FFFF, 0x000FFFFF, 0x001FFFFF, 0x003FFFFF, 0x007FFFFF, 0x00FFFFFF,
	0x01FFFFFF, 0x03FFFFFF, 0x07FFFFFF, 0x0FFFFFFF, 0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF
};

#define ROL(x,n,w) ((x<<n) | ((x >> (WIDTH_BITS(w)-n)) & field_len_mask[n-1])
#define ROR(x,n,w) (((x>>n) & ~field_len_mask[WIDTH_BITS(w)-n-1]) | (x<<WIDTH_BITS(w)-n))

const device_type NS32016 = &device_creator<ns32016_device>;

ns32016_device::ns32016_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: cpu_device(mconfig, NS32016, "NS32016", tag, owner, clock),
	  m_program_config("program", ENDIANNESS_LITTLE, 8, 24, 0)
{
}

ns32016_device::~ns32016_device() {}

UINT32 ns32016_device::disasm_min_opcode_bytes() const
{
	return 1;
}

UINT32 ns32016_device::disasm_max_opcode_bytes() const
{
	return 10; //???
}

// device_t
void ns32016_device::device_start()
{
	m_program = space(AS_PROGRAM);
	m_icountptr = &m_icount;

	save_item(NAME(m_regs.pc));
	save_item(NAME(m_regs.sb));
	save_item(NAME(m_regs.fp));
	save_item(NAME(m_regs.sp[1]));
	save_item(NAME(m_regs.sp[0]));
	save_item(NAME(m_regs.intbase));
	save_item(NAME(m_regs.psr));
	save_item(NAME(m_regs.mod));
	save_item(NAME(m_regs.r0));
	save_item(NAME(m_regs.r1));
	save_item(NAME(m_regs.r2));
	save_item(NAME(m_regs.r3));
	save_item(NAME(m_regs.r4));
	save_item(NAME(m_regs.r5));
	save_item(NAME(m_regs.r6));
	save_item(NAME(m_regs.r7));
	save_item(NAME(m_regs.cfg));

	state_add(STATE_GENPC, "GENPC",     m_regs.pc).noshow();
	state_add(STATE_GENPCBASE, "GENPCBASE", m_ppc).noshow();
	state_add(STATE_GENFLAGS, "GENFLAGS", m_regs.psr).noshow();
	
	state_add(NS32016_PC,  "PC",        m_regs.pc);
	state_add(NS32016_SB,  "SB",  m_regs.sb).mask(0x00ffffff);
	state_add(NS32016_FP,  "FP",  m_regs.fp).mask(0x00ffffff);;
	state_add(NS32016_SP1, "SP1",  m_regs.sp[1]).mask(0x00ffffff);;
	state_add(NS32016_SP0, "SP0",  m_regs.sp[0]).mask(0x00ffffff);;
	state_add(NS32016_INTBASE, "INTBASE",  m_regs.intbase).mask(0x00ffffff);;
	state_add(NS32016_PSR, "PSR",  m_regs.psr);
	state_add(NS32016_MOD, "MOD",  m_regs.mod);
	state_add(NS32016_R0,  "R0",  m_regs.r0);
	state_add(NS32016_R1,  "R1",  m_regs.r1);
	state_add(NS32016_R2,  "R2",  m_regs.r2);
	state_add(NS32016_R3,  "R3",  m_regs.r3);
	state_add(NS32016_R4,  "R4",  m_regs.r4);
	state_add(NS32016_R5,  "R5",  m_regs.r5);
	state_add(NS32016_R6,  "R6",  m_regs.r6);
	state_add(NS32016_R7,  "R7",  m_regs.r7);
	state_add(NS32016_CFG, "CFG",  m_regs.cfg).mask(0xf);

	// Resolve device callbacks
//	m_in_portb_func.resolve(m_in_portb_cb, *this);
//	m_out_portb_func.resolve(m_out_portb_cb, *this);
//	m_in_portx_func.resolve(m_in_portx_cb, *this);
//	m_out_portx_func.resolve(m_out_portx_cb, *this);
//	m_in_irq_func.resolve(m_in_irq_cb, *this);
}



void ns32016_device::device_reset()
{
	m_regs.pc = 0;
	m_regs.psr = 0;

//	m_irq_state = m_irq_latch = CLEAR_LINE;
	m_icount = 0;
}

void ns32016_device::device_config_complete()
{
	// inherit a copy of the static data
	const ns32016_interface *intf = reinterpret_cast<const ns32016_interface *>(static_config());
	if (intf != NULL)
	{
		*static_cast<ns32016_interface *>(this) = *intf;
	}
	// or initialize to defaults if none provided
	else
	{
//		memset(&m_in_portb_func, 0, sizeof(m_in_portb_func));
//		memset(&m_out_portb_func, 0, sizeof(m_out_portb_func));
//		memset(&m_in_portx_func, 0, sizeof(m_in_portx_func));
//		memset(&m_out_portx_func, 0, sizeof(m_out_portx_func));
//		memset(&m_in_irq_func, 0, sizeof(m_in_irq_func));
	}
}

// device_execute_interface

const int ns32016_device::op_fmt[] = 
{
 // -0 -1 -2 -3 -4 -5 -6 -7 -8 -9 -A -B -C -D -E -F
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 5, 2, // 0-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 2, 2,14, 2, // 1-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 8, 2, // 2-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 2, 2, 9, 2, // 3-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 6, 2, // 4-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 2, 2,16, 2, // 5-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 8, 2, // 6-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 3, 3,10, 3, // 7-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2,18, 2, // 8-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 2, 2,13, 2, // 9-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 8, 2, // A-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 2, 2,11, 2, // B-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 7, 2, // C-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4, 2, 2,17, 2, // D-
	 4, 4, 1, 4, 4, 4,19, 4, 4, 4, 0, 4, 2, 2, 8, 2, // E-
	 4, 4, 1, 4, 4, 4,15, 4, 4, 4, 0, 4,-1,-1,12,-1 // F-
};

const pFmtMethod ns32016_device::fnc_fmt[] = {
	&ns32016_device::ins_format_0,
	&ns32016_device::ins_format_1,
	&ns32016_device::ins_format_2,
	&ns32016_device::ins_format_3,
	&ns32016_device::ins_format_4,
	&ns32016_device::ins_format_5,
	&ns32016_device::ins_format_6,
	&ns32016_device::ins_format_7,
	NULL,
	NULL,
	NULL, // 10
	NULL,
	NULL,
	NULL,
	NULL,
	NULL, // 15
	NULL,
	NULL,
	NULL,
	NULL // 19
};

void ns32016_device::execute_run()
{
	bool check_debugger = ((device_t::machine().debug_flags & DEBUG_FLAG_ENABLED) != 0);

//	if (intreq && (m_regs.xtri & MASK_INTREQ)) { //Check for pending interrupt
//		interrupt();
//	}

	do {
		m_ppc = m_regs.pc;
		if (check_debugger)
			debugger_instruction_hook(this, m_regs.pc);

		m_ins = m_program->read_dword_unaligned(m_regs.pc);

		int fmt = op_fmt[m_ins & 0xFF];
		assert(fmt >= 0); // Do we get any -1 instructions?
		pFmtMethod fmtfnc = fnc_fmt[fmt];
		assert_always(fmtfnc != NULL,"unimplemented message format");
			
		//offs_t res = (this->*fmtfnc)();

		m_icount--;
	} while (m_icount > 0);
}

// device_memory_interface
const address_space_config *ns32016_device::memory_space_config(address_spacenum spacenum) const
{
	return	(spacenum == AS_PROGRAM) ? &m_program_config : NULL;
}

// device_disasm_interface
offs_t ns32016_device::disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options)
{
	//logerror("options=%i rom=%p ram=%p\n", options, oprom, opram);
	extern CPU_DISASSEMBLE( ns32016 );
	return CPU_DISASSEMBLE_NAME( ns32016 )(NULL, buffer, pc, oprom, opram, 0);
}


#define LINK_TABLE_ADDR (m_program->read_dword(m_regs.mod + 4))

bool ns32016_device::ins_cond(int cond)
{
	switch( cond & 0xF )
	{
	case 0:
		return PSR_Z;
	case 1:
		return !PSR_Z;
	case 2:
		return PSR_C;
	case 3:
		return !PSR_C;
	case 4:
		return PSR_L;
	case 5:
		return !PSR_L;
	case 6:
		return PSR_N;
	case 7:
		return !PSR_N;
	case 8:
		return PSR_F;
	case 9:
		return !PSR_F;
	case 10:
		return !(PSR_L || PSR_Z);
	case 11:
		return PSR_L || PSR_Z;
	case 12:
		return !(PSR_N || PSR_Z);
	case 13:
		return PSR_N || PSR_Z;
	case 14:
		return true;
	case 15:
		return false;
	default:
		assert(false); // We really shouldn't get here...
	}

	return false;
}

offs_t ns32016_device::disp_size(offs_t offset)
{
	UINT8 size = INS_OP8(offset) & 0xC0;
	if (size == 0x80)
		return 2;
	else if (size == 0xC0)
		return 4;
	else // 0x00 or 0x40
		return 1;
}

offs_t ns32016_device::op_disp(INT32 &disp, int off)
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
		assert(false); // We really shouldn't get here...
	}
}

// Branches (Conditional + Unconditional)
offs_t ns32016_device::ins_format_0()
{
	INT32 disp;
	offs_t ic = 1; // 1-byte base instruction
	
	// Syntax: B(cond) disp dest 
	ic += op_disp(disp, ic);
	if (ins_cond(FMT0_COND(m_ins)))
		m_regs.pc += disp;// Condition true - take branch
	else
		m_regs.pc += ic; // Condition false - next instruction
	return ic;
}

offs_t ns32016_device::ins_format_1()
{
	INT32 disp;
	UINT8 rmask;
	UINT32 lt_entry;
	offs_t ic = 1; // 1-byte base instruction

	switch (FMT1_OP)
	{
	case 0: // BSR - Branch to subroutine
		// Syntax: BSR disp dest
		ic += op_disp(disp, ic);
		PUSHD(m_regs.pc + ic); // return address

		m_regs.pc += disp;
		break;
	case 1: // RET - Return from subroutine
		// Syntax: RET disp constant
		ic+=op_disp(disp, ic);

		POPD(m_regs.pc);
		REG_SP += disp;
		break;
	case 2: // CXP - Call external procedure
		// Syntax: CXP disp constant
		ic+=op_disp(disp, ic);
		REG_SP -= 2;
		PUSHW(m_regs.mod);
		PUSHD(m_regs.pc + ic); // return address
		lt_entry = MEM_R_DW(m_regs.mod + 4) + 4 * disp; // link_table_entry = D(mod_descriptor + 4) + 4*disp
		m_regs.mod = MEM_R_W(lt_entry); // MOD = Wlink_table_entry + 0
		m_regs.sb = MEM_R_DW(m_regs.mod); // SB = D(mod_descriptor + 0)
		
		m_regs.pc = MEM_R_DW(m_regs.mod + 8) + MEM_R_W(lt_entry + 2); // PC = D(mod_descriptor + 8) + W(link_table_entry + 2)
		break;
	case 3: // RXP - Return from external procedure
		// Syntax: RXP disp constant
		ic+=op_disp(disp, ic);
		POPD(m_regs.pc);
		POPW(m_regs.mod);
		m_regs.sb = MEM_R_DW(m_regs.mod); // SB = D(mod_descriptor + 0)
		REG_SP += disp + 2;
		break;
	case 4: // RETT - Return from trap (priv)
		// Syntax: RETT disp constant
		ic+=op_disp(disp, ic);
		if (PSR_U)
		{
			trap(TRAP_ILL);
		}
		else
		{
			POPD(m_regs.pc);
			POPW(m_regs.mod);
			POPW(m_regs.psr);
		}
		m_regs.sb = MEM_R_DW(m_regs.mod); // SB = D(mod_descriptor + 0)
		REG_SP += disp;
		break;
	case 5: // RETI - Return from interrupt
		// Syntax: RETI
		if (PSR_U)
		{
			trap(TRAP_ILL);
		}
		else
		{
			POPD(m_regs.pc);
			POPW(m_regs.mod);
			POPW(m_regs.psr);
			m_regs.sb = MEM_R_DW(m_regs.mod); // SB = D(mod_descriptor + 0)
		}
		break;
	case 6: // SAVE - Save general purpose registers
		// Syntax: SAVE reglist imm
		rmask = INS_OP8(ic); ic++;
		if (rmask & 0x01) PUSHD(m_regs.r0);
		if (rmask & 0x02) PUSHD(m_regs.r1);
		if (rmask & 0x04) PUSHD(m_regs.r2);
		if (rmask & 0x08) PUSHD(m_regs.r3);
		if (rmask & 0x10) PUSHD(m_regs.r4);
		if (rmask & 0x20) PUSHD(m_regs.r5);
		if (rmask & 0x40) PUSHD(m_regs.r6);
		if (rmask & 0x80) PUSHD(m_regs.r7);

		m_regs.pc += ic;
		break;
	case 7: // RESTORE - Restore general purpose registers
		// Syntax: RESTORE reglist imm
		rmask = INS_OP8(ic); ic++;
		if (rmask & 0x01) POPD(m_regs.r7);
		if (rmask & 0x02) POPD(m_regs.r6);
		if (rmask & 0x04) POPD(m_regs.r5);
		if (rmask & 0x08) POPD(m_regs.r4);
		if (rmask & 0x10) POPD(m_regs.r3);
		if (rmask & 0x20) POPD(m_regs.r2);
		if (rmask & 0x40) POPD(m_regs.r1);
		if (rmask & 0x80) POPD(m_regs.r0);

		m_regs.pc += ic;
		break;
	case 8: // ENTER - Enter new context
		// ENTER reglist imm , constant disp
		rmask = INS_OP8(ic); ic++;
		ic += op_disp(disp, ic);
		PUSHD(m_regs.fp);
		m_regs.fp = REG_SP;
		REG_SP -= disp;
		if (rmask & 0x01) PUSHD(m_regs.r0);
		if (rmask & 0x02) PUSHD(m_regs.r1);
		if (rmask & 0x04) PUSHD(m_regs.r2);
		if (rmask & 0x08) PUSHD(m_regs.r3);
		if (rmask & 0x10) PUSHD(m_regs.r4);
		if (rmask & 0x20) PUSHD(m_regs.r5);
		if (rmask & 0x40) PUSHD(m_regs.r6);
		if (rmask & 0x80) PUSHD(m_regs.r7);
		
		m_regs.pc += ic;
		break;
	case 9: // EXIT - Exit context
		// EXIT reglist imm
		rmask = INS_OP8(ic); ic++;
		if (rmask & 0x01) POPD(m_regs.r7);
		if (rmask & 0x02) POPD(m_regs.r6);
		if (rmask & 0x04) POPD(m_regs.r5);
		if (rmask & 0x08) POPD(m_regs.r4);
		if (rmask & 0x10) POPD(m_regs.r3);
		if (rmask & 0x20) POPD(m_regs.r2);
		if (rmask & 0x40) POPD(m_regs.r1);
		if (rmask & 0x80) POPD(m_regs.r0);
		REG_SP = m_regs.fp;
		PUSHD(m_regs.fp);

		m_regs.pc += ic;
		break;
	case 10: // NOP - No operation
		// Syntax: NOP
		m_regs.pc += ic;
		break;
	case 11: // WAIT - Wait for interrupt
		// Syntax: WAIT
		m_regs.pc += ic;
		wait();
		break;
	case 12: // DIA - Hardware diagnose - branch to self - not used in programs
		break;
	case 13: // FLAG - Flag trap (FLG)
		// Syntax: FLAG
		if (PSR_F)
			trap(TRAP_FLG);
		else
			m_regs.pc += ic; // not in docs??
		break;
	case 14: // SVC - Supervisor call trap
		// Syntax: SVC
		trap(TRAP_SVC);
		break;
	case 15: // BPT - Breakpoint trap
		// Syntax: BPT
		trap(TRAP_BPT);
		break;
	default:
		assert(false); // We really shouldn't get here...
	}
	return ic;
}

#define I2W(i) (i == 0 ? NS32016_WIDTH_BYTE : i == 1 ? NS32016_WIDTH_WORD : NS32016_WIDTH_DWORD)

enum {
	AReg_US = 0,
	AReg_FP = 8,
	AReg_SP = 9,
	AReg_SB = 10,
	AReg_PSR = 13,
	AReg_INTBASE = 14,
	AReg_MOD = 15
};

offs_t ns32016_device::ins_format_2()
{
	INT32 disp;
	int d_size;
	offs_t d_off;
	UINT32 gen, sx, res;
	offs_t ic = 2; // 2-byte base instruction

	offs_t g_off = ic;
	offs_t g_size = gen_size(FMT2_GEN, g_off, FMT2_I);
	ic += g_size;


	switch (FMT2_OP)
	{
	case 0: // ADDQ - Add quick integer
		// Syntax: ADDQi quick src, rmw.i dest [CF]
		sx = FMT2_SHRT_SX;
		gen = gen_read(FMT2_GEN, g_off, FMT2_I);
		res = gen + sx;
		gen_write(FMT2_GEN, g_off, res, FMT2_I);

		CALC_C(MASK_MSB(FMT2_I), gen, sx, res);
		CALC_F(MASK_MSB(FMT2_I), gen, sx, res);

		m_regs.pc += ic;
		break;
	case 1: // CMPQ - Compare quick integer
		// Syntax: CMPQi quick src1, rd.i src2 [ZNL]
		sx = FMT2_SHRT_SX;
		gen = gen_read(FMT2_GEN, g_off, FMT2_I);
		disp = SIGN_X(gen - sx, FMT2_I);

		CALC_Z(WIDTH(disp, FMT2_I));
		CALC_N(disp);
		CALC_L(sx, gen);

		m_regs.pc += ic;
		break;
	case 2: // SPR - Store processor register (priv if INT/INTBASE)
		// Syntax: SPRi short progreg, wr.i dest
		if (PSR_U && (FMT2_SHRT == AReg_PSR || FMT2_SHRT == AReg_INTBASE))
		{
			trap(TRAP_ILL);
		}
		else
		{
			UINT32 areg;
			areg_r(FMT2_SHRT, areg);
			gen_write(FMT2_GEN, g_off, areg, FMT2_I);

			m_regs.pc += ic;
		}
		break;
	case 3: // S(cond) - Save condition code as boolean
		// Syntax: S(cond)i wr.i dest
		Unimpl("S[cond](2) unimpl");
		break;
	case 4: // ACB - Add, compare and branch
		// ACBi quick inc, rmw.i index, disp dest
		d_off = ic; // gen offset already included
		d_size = disp_size(d_off);
		
		gen = gen_read(FMT2_GEN, g_off, FMT2_I);
		gen += (INT8)FMT2_SHRT_SX;
		gen_write(FMT2_GEN, g_off, gen, FMT2_I);

		ic += d_size;

		if (gen == 0)
		{
			m_regs.pc += ic;
		}
		else
		{
			op_disp(disp, d_off);
			m_regs.pc += disp;
		}
		break;
	case 5: // MOVQ - Move quick integer
		// Syntax: MOVQi quick src, wr.i dest [all]
		sx = FMT2_SHRT_SX;
		gen_write(FMT2_GEN, g_off, sx, FMT2_I);

		m_regs.pc += ic;
		break;
	case 6: // LPR - Load processor register (priv if INT/INTBASE)
		// LPRi shrt procreg, rd.i src
		if (PSR_U && (FMT2_SHRT == AReg_PSR || FMT2_SHRT == AReg_INTBASE))
		{
			trap(TRAP_ILL);
		}
		else
		{
			UINT32 disp1 = gen_read(FMT2_GEN, g_off, FMT2_I);
			areg_w(FMT2_SHRT, disp1, FMT2_I);

			m_regs.pc += ic;
		}
		break;
	default:
		Unimpl("Unknown opcode fmt2");
		break;
	}
	return ic;
}

offs_t ns32016_device::ins_format_3()
{
	//INT32 disp;
	//int d_size;
	//offs_t d_off;
	INT32 gen;
	offs_t ic = 2; // 2-byte base instruction

	offs_t g_off = ic;
	offs_t g_size = gen_size(FMT3_GEN, g_off, FMT3_I);
	ic += g_size;

	switch (FMT3_OP)
	{
	case 0: // CXPD - Call external procedure with descriptor
		// Syntax: CXPD addr desc
		gen = gen_read(FMT3_GEN, g_off, FMT3_I);
		REG_SP -= 2;
		PUSHW(m_regs.mod);
		PUSHD(m_regs.pc + ic); // return address
		m_regs.mod = MEM_R_W(gen); // MOD = desc_addr
		m_regs.sb = MEM_R_DW(m_regs.mod); // SB = D(mod_descriptor + 0)

		m_regs.pc = MEM_R_DW(m_regs.mod + 8 + MEM_R_W(gen + 2)); // PC = D(mod_descriptor + 8) + W(desc_addr + 2)
		break;
	case 2: // BICPSR - Bit clear in PSR (priv if word length)
		// Syntax: BICPSR(B|W) rd.(B|W) src [all]
		gen = gen_read(FMT3_GEN, g_off, FMT3_I);
		if (FMT3_I == NS32016_WIDTH_BYTE)
		{
			m_regs.psr &= ~(gen & 0xFF);
		}
		else if (FMT3_I == NS32016_WIDTH_WORD)
		{
			if (PSR_U)
				trap(TRAP_ILL);
			else
				m_regs.psr &= ~(gen & 0xFFFF);
		}

		m_regs.pc += ic;
		break;
	case 4: // JUMP - Jump
		// Syntax: JUMP addr dest
		gen = gen_read(FMT3_GEN, g_off, FMT3_I);

		m_regs.pc = gen;
		break;
	case 6: // BISPSR - Bit set in PSR (priv if word length)
		// Syntax: BISPSR(B|W) rd.(B|W) src [all]
		gen = gen_read(FMT3_GEN, g_off, FMT3_I);

		if (FMT3_I == NS32016_WIDTH_BYTE)
		{
			m_regs.psr |= gen & 0xFF;
		}
		else if (FMT3_I == NS32016_WIDTH_WORD)
		{
			if (PSR_U)
				trap(TRAP_ILL);
			else
				m_regs.psr |= gen & 0xFFFF;
		}

		m_regs.pc += ic;				
		break;
	case 10: // ADJSP - Adjust stack pointer
		// Syntax: ADJSPi rd.i src
		gen = gen_read(FMT3_GEN, g_off, FMT3_I);
		REG_SP -= gen;

		m_regs.pc += ic;				
		break;
	case 12: // JSR - Jump to subroutine
		// Syntax: JSR addr dest
		gen = gen_read(FMT3_GEN, ic, FMT3_I);
		PUSHD(m_regs.pc + ic); // return address

		m_regs.pc = gen;
		break;
	case 14: // CASE - Case branch
		// Syntax: CASEi rd.i index
		gen = gen_read(FMT3_GEN, g_off, FMT3_I);

		m_regs.pc += gen;
		break;
	default:
		Unimpl("Unknown opcode fmt3");
		break;
	}
	return ic;
}

offs_t ns32016_device::ins_format_4()
{
	INT32 result;
	UINT32 src, dest;
	offs_t ic = 2; // 2-byte base instruction

	offs_t g1_off = ic;
	offs_t g1_size = gen_size(FMT4_GEN1, g1_off, FMT4_I);
	offs_t g2_off = g1_off + g1_size;
	offs_t g2_size = gen_size(FMT4_GEN2, g2_off, FMT4_I);
	ic += g1_size + g2_size;

	switch (FMT4_OP)
	{
	case 0: // ADD - Add
		// Syntax: ADDi rd.i src, rmw.i dest [CF]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		result = src + dest;
		gen_write(FMT4_GEN2, g2_off, result, FMT4_I); 

		CALC_C(MASK_MSB(FMT4_I), src, dest, result);
		CALC_F(MASK_MSB(FMT4_I), src, dest, result);

		m_regs.pc += ic;
		break;
	case 1: // CMP - Compare
		// Syntax: CMPi rd.i src1, rmw.i src2 [ZNL]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		result = SIGN_X(dest - src, FMT4_I);

		CALC_Z(WIDTH(result, FMT4_I));
		CALC_N(result);
		CALC_L(src, dest);

		m_regs.pc += ic;
		break;
	case 2: // BIC - Bit clear
		// Syntax: BICi r.i src, rmw.i dest
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		gen_write(FMT4_GEN2, g2_off, dest & ~src, FMT4_I);

		m_regs.pc += ic;
		break;
	case 4: // ADDC - Add with carry
		// Syntax: ADDCi rd.i src, rmw.i dest  [CF]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		result = src + dest + (PSR_C ? 1 : 0);
		gen_write(FMT4_GEN2, g2_off, result, FMT4_I); 

		CALC_C(MASK_MSB(FMT4_I), src, dest, result);
		CALC_F(MASK_MSB(FMT4_I), src, dest, result);

		m_regs.pc += ic;
		break;
	case 5: // MOV - Move
		// Syntax: MOVi rd.i src, wr.i dest
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		gen_write(FMT4_GEN2, g2_off, src, FMT4_I);

		m_regs.pc += ic;
		break;
	case 6: // OR - Logical OR
		// Syntax: ORi rd.i src, rmw.i dest 
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		dest |= src;
		gen_write(FMT4_GEN2, g2_off, dest, FMT4_I);

		m_regs.pc += ic;
		break;
	case 8: // SUB - Subtract
		// Syntax: SUBi rd.i src, rmw.i dest [CF]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		result = dest - src;
		gen_write(FMT4_GEN2, g2_off, result, FMT4_I); 

		CALC_C(MASK_MSB(FMT4_I), src, dest, result);
		CALC_F(MASK_MSB(FMT4_I), src, dest, result);

		m_regs.pc += ic;
		break;
	case 9: // ADDR - Compute effective address
		// Syntax: ADDR addr src, wr.D dest
		src = gen_addr(FMT4_GEN1, g1_off, FMT4_I);
		gen_write(FMT4_GEN2, g2_off, src, FMT4_I);

		m_regs.pc += ic;
		break;
	case 10: // AND - Logical AND
		// Syntax: ANDi rd.i src, rmw.i dest
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		dest &= src;
		gen_write(FMT4_GEN2, g2_off, dest, FMT4_I);

		m_regs.pc += ic;
		break;
	case 12: // SUBC - Subtract with borrow
		// Syntax: SUBCi rd.i src, rmw.i dest [CF]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		result = dest - (src + (PSR_C ? 1 : 0));
		gen_write(FMT4_GEN2, g2_off, result, FMT4_I); 

		CALC_C(MASK_MSB(FMT4_I), src, dest, result);
		CALC_F(MASK_MSB(FMT4_I), src, dest, result);

		m_regs.pc += ic;
		break;
	case 13: // TBIT - Test bit
		// TBITi rd.i offset, regaddr base [F]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		if (dest >> src & 1)
			PSR_SET(PSR_F_MASK)
		else
			PSR_CLR(PSR_F_MASK)

		m_regs.pc += ic;
		break;
	case 14: // XOR - Exclusive OR
		// Syntax: XORi rd.i src, rmw.i dest [CF]
		src = gen_read(FMT4_GEN1, g1_off, FMT4_I);
		dest = gen_read(FMT4_GEN2, g2_off, FMT4_I);
		dest ^= src;
		gen_write(FMT4_GEN2, g2_off, dest, FMT4_I);

		m_regs.pc += ic;
		break;
	default:
		Unimpl("Unknown opcode fmt4"); //TODO: Trap?
		break;
	}
	return ic;
}

//
// String ops:
// R0 - max number of items to copy
// R1 - source address
// R2 - destination address
// R3 - (optional) address of translation table (only for byte width)
// R4 - end of string condition
//
// U/W is whether copy should continue or break on R4 condition. U is continue until.
// B flag (Direction) indicates whether pointers should be decreased or increased. 1 is decrease
// After a copy the pointers point to the byte 'after' the string
//
offs_t ns32016_device::ins_format_5()
{
	offs_t ic = 3; // 3-byte base instruction
	UINT32 val, width, wsize;//, uw;
	bool trans;//, cond;

	switch(FMT5_OP)
	{
	case 0: // MOVS(T) - Move string (with translation)
		// MOVS(T)i [B[,]][U|W] [F]
		width = FMT5_I;
		wsize = WIDTH_BYTES(width);
		trans = width == NS32016_WIDTH_BYTE && FMT5_T; // Translate

		if (m_regs.r0 > 0) // Only copy when counter > 0
		{
			// Move
			val = MEM_R(m_regs.r1, width);
			if (trans) 
				val = MEM_R(m_regs.r3 + val, width);
			MEM_W(m_regs.r2, val, width);
			
			// Update counters
			m_regs.r0--;
			if (FMT5_B)
			{
				m_regs.r1 -= wsize;
				m_regs.r2 -= wsize;
			}
			else
			{
				m_regs.r1 += wsize;
				m_regs.r2 += wsize;
			}

			// Check condition
			if ((FMT5_UW_U && val == m_regs.r4) || (FMT5_UW_W && val != m_regs.r4))
				PSR_SET(PSR_F_MASK)
			else
				PSR_CLR(PSR_F_MASK)
		}

		// Only continue to next instruction if condition is true or counter is 0
		if (PSR_F || m_regs.r0 == 0)
			m_regs.pc += ic;
		break;
	case 1: // CMPS(T) - Compare string (with translation)
		// CMPS(T)i [B[,]][U|W] [ZNLF]
		Unimpl("CMPS(5) unimpl");
		break;
	case 2: // SETCFG - Set configuration register (priv)
		// SETCFG short cfglist
		m_regs.cfg = FMT5_SHRT;
		m_regs.pc += ic;
		break;
	case 3: // SKPS(T) -  Skip string (with translation)
		// SKPS(T)i [B[,]][U,W] [F]
		Unimpl("SKPS(5) unimpl");
		break;
	default:
		trap(TRAP_UND);
		break;
	}
	return ic;
}

offs_t ns32016_device::ins_format_6()
{
	UINT32 src, dest;
	INT32 /*disp,*/ result;
	//UINT8 imm;
	offs_t ic = 3; // 3-byte base instruction

	offs_t g1_off = ic;
	offs_t g1_size = gen_size(FMT6_GEN1, g1_off, FMT6_I);
	offs_t g2_off = g1_off + g1_size;
	offs_t g2_size = gen_size(FMT6_GEN2, g2_off, FMT6_I);
	ic += g1_size + g2_size;

	switch (FMT6_OP)
	{
	case 0: // ROTi rd.B count, rmw.i dest
		Unimpl("ROTi");
		break;
	case 1: // ASH - Arithmetic shift (left or right)
		// ASHi rd.B count, rmw.i dest
		src = gen_read(FMT6_GEN1, g1_off, NS32016_WIDTH_BYTE);
		result = gen_read(FMT6_GEN2, g2_off, FMT6_I);
		if (SIGNED(src,NS32016_WIDTH_BYTE) < 0) // Shift right
		{
			result >>= -src; // Shift of signed type is arithmetic in C/C++
		}
		else // Shift left
		{
			result <<= src;
		}
		gen_write(FMT6_GEN2, g2_off, result, FMT6_I);

		m_regs.pc += ic;
		break;
	case 2: // CBITi rd.i offset, regaddr base [F]
		src = gen_read(FMT6_GEN1, g1_off, FMT6_I);
		dest = gen_read(FMT6_GEN2, g2_off, FMT6_I);

		if ((dest>>src) & 1)
			PSR_SET(PSR_F_MASK)
		else
			PSR_CLR(PSR_F_MASK)

		dest &= ~(1<<src);
		gen_write(FMT7_GEN2, g2_off, dest, FMT6_I);

		m_regs.pc += ic;
		break;
	case 3: // CBITIi rd.i offset, regaddr base [F]
		Unimpl("CBITIi");
		break;
	case 4: // Undefined
		trap(TRAP_UND);
		break;
	case 5: // LSH - Logical shift (left or right)
		// LSHi rd.B count, rmw.i dest
		src = gen_read(FMT6_GEN1, g1_off, NS32016_WIDTH_BYTE);
		dest = gen_read(FMT6_GEN2, g2_off, FMT6_I);
		if (SIGNED(src,NS32016_WIDTH_BYTE) < 0) // Shift right
		{
			dest >>= -src;
		}
		else // Shift left
		{
			dest <<= src;
		}
		gen_write(FMT6_GEN2, g2_off, dest, FMT6_I);

		m_regs.pc += ic;
		break;
	case 6: // SBITi rd.i offset, regaddr base [F]
		src = gen_read(FMT6_GEN1, g1_off, FMT6_I);
		dest = gen_read(FMT6_GEN2, g2_off, FMT6_I);

		if ((dest>>src) & 1)
			PSR_SET(PSR_F_MASK)
		else
			PSR_CLR(PSR_F_MASK)

		dest |= (1<<src);
		gen_write(FMT6_GEN2, g2_off, dest, FMT6_I);

		m_regs.pc += ic;
		break;
	case 7: // SBITIi rd.i offset, regaddr base [F]
		Unimpl("SBITIi");
		break;
	case 8: // NEGi rd.i src, w.i dest [CF]
		Unimpl("NEGi");
		break;
	case 9: // NOTi rd.i src, w.i dest
		Unimpl("NOTi");
		break;
	case 10: // Undefined
		trap(TRAP_UND);
		break;
	case 11: // SUBPi rd.i src, rmw.i dest [CF]
		Unimpl("SUBPi");
		break;
	case 12: // ABSi rd.i src, rmw.i dest [F]
		Unimpl("ABSi");
		break;
	case 13: // COMi rd.i src, rmw.i dest
		Unimpl("COMi");
		break;
	case 14: // IBITi  rd.i offset, regaddr base [F]
		src = gen_read(FMT6_GEN1, g1_off, FMT6_I);
		dest = gen_read(FMT6_GEN2, g2_off, FMT6_I);

		if ((dest>>src) & 1)
			PSR_SET(PSR_F_MASK)
		else
			PSR_CLR(PSR_F_MASK)

		dest ^= (1<<src);
		gen_write(FMT7_GEN2, g2_off, dest, FMT6_I);

		m_regs.pc += ic;
		break;
	case 15: // ADDPi rd.i src, rmw.i dest [CF]
		Unimpl("ADDPi");
		break;
	default:
		Unimpl("Fmt6 Illegal");
		break;
	}
	return ic;
}

offs_t ns32016_device::ins_format_7()
{
	UINT32 src, dest, fldmask;
	//INT32 disp, result;
	UINT8 imm;
	offs_t ic = 3; // 3-byte base instruction

	offs_t g1_off = ic;
	offs_t g1_size = gen_size(FMT7_GEN1, g1_off, FMT7_I);
	offs_t g2_off = g1_off + g1_size;
	offs_t g2_size = gen_size(FMT7_GEN2, g2_off, FMT7_I);
	ic += g1_size + g2_size;

	switch (FMT7_OP)
	{
	case 0: // MOVMi addr block1, addr block2, disp length (1264)
		Unimpl("MOVM(7) unimpl");
		break;
	case 1: // CMPMi addr block1, addr block2, disp length (ZNL) (1264)
		Unimpl("CMPM(7) unimpl");
		break;
	case 2: // INSS - Insert field short
		// Syntax: INSSi rd.i src, regaddr base, imm(cons3,cons5) offset,length (1265)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		dest = gen_read(FMT7_GEN2, g2_off, NS32016_WIDTH_DWORD); // base is regaddr, so read dword
		imm = INS_OP8(ic); ic += 1;
		if (IMM_CONS3(imm) + IMM_CONS5(imm) > 31) // Do we need to mask in the next byte at MSB
		{
			Unimpl("EXTS/INSS with 5th byte");
		}
		fldmask = field_len_mask[IMM_CONS5(imm)] << IMM_CONS3(imm); // Calculate offset field mask
		src <<= IMM_CONS3(imm); // shift source bit upp to offset position
		dest &= ~fldmask; // Clear field bits in destination
		dest |= src & fldmask; // Mask bits into destination
		gen_write(FMT7_GEN2, g2_off, dest, NS32016_WIDTH_DWORD);

		m_regs.pc += ic;
		break;
	case 3: // EXTS - Extract field short
		// EXTSi regaddr base, wr.i dest, imm(cons3,cons5) offset,length (1265)
		src = gen_read(FMT7_GEN1, g1_off, NS32016_WIDTH_DWORD); //base is regaddr, so read dword
		imm = INS_OP8(ic); ic += 1;
		if (IMM_CONS3(imm) + IMM_CONS5(imm) > 31) // Do we need to mask in the next byte at MSB
		{
			Unimpl("EXTS/INSS with 5th byte");
		}
		src >>= IMM_CONS3(imm); // shift down offset
		src &= field_len_mask[IMM_CONS5(imm)]; // mask to length
		gen_write(FMT7_GEN2, g2_off, src, FMT7_I);

		m_regs.pc += ic;
		break;
	case 4: // MOVXBW rd.B src, rmw.W dest (1270)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		gen_write(FMT7_GEN2, g2_off, (INT16)SIGNED(src,FMT7_I), NS32016_WIDTH_WORD);

		m_regs.pc += ic;
		break;
	case 5: // MOVZBW rd.B src, rmw.W dest (1271)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		gen_write(FMT7_GEN2, g2_off, UNSIGNED(src, FMT7_I), NS32016_WIDTH_WORD);

		m_regs.pc += ic;
		break;
	case 6: // MOVZiD rd.i src, rmw.D dest (1271)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		gen_write(FMT7_GEN2, g2_off, UNSIGNED(src, FMT7_I), NS32016_WIDTH_DWORD);

		m_regs.pc += ic;
		break;
	case 7: // MOVXiD rd.i src, rmw.D dest (1271)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		gen_write(FMT7_GEN2, g2_off, (INT32)SIGNED(src,FMT7_I), NS32016_WIDTH_DWORD);

		m_regs.pc += ic;
		break;
	case 8: // MULi rd.i src, rmw.i dest (1270)
		Unimpl("MUL(7) unimpl");
		break;
	case 9: // MEIi rd.i src, rmw.i dest (1263)
		Unimpl("MEI(7) unimpl");
		break;
	case 10: // Undefined
		trap(TRAP_UND);
		break;
	case 11: // DEIi rd.i src, rmw.i dest (trap DVZ) (1263)
		Unimpl("MOVM(7) unimpl");
		break;
	case 12: // QUOi rd.i src, rmw.i dest (trap DVZ) (1270)
		Unimpl("QUO(7) unimpl");
		break;
	case 13: // REMi rd.i src, rmw.i dest (trap DVZ) (1270)
		Unimpl("REM(7) unimpl");
		break;
	case 14: // MOD - Modulo
		// MODi rd.i src, rmw.i dest (trap DVZ) (1270)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		if (src == 0)
			trap(TRAP_DVZ);
		dest = gen_read(FMT7_GEN2, g2_off, FMT7_I);
		gen_write(FMT7_GEN2, g2_off, SIGNED(dest,FMT7_I)%SIGNED(src,FMT7_I), FMT7_I);

		m_regs.pc += ic;
		break;
	case 15: // DIV - Division
		// DIVi rd.i src, rmw.i dest (trap DVZ) (1270)
		src = gen_read(FMT7_GEN1, g1_off, FMT7_I);
		if (src == 0)
			trap(TRAP_DVZ);
		dest = gen_read(FMT7_GEN2, g1_off, FMT7_I);
		gen_write(FMT7_GEN2, g2_off, SIGNED(dest,FMT7_I)/SIGNED(src,FMT7_I), FMT7_I);

		m_regs.pc += ic;
		break;
	default:
		Unimpl("Unimplemented opcode fmt7");
		break;
	}

	return ic;
}
	

void ns32016_device::areg_r(int areg, UINT32 &value)
{
	switch (areg)
	{
	case AReg_US: //TODO: Are we sure it is user stack? Makes sense giving supervisor access to user stack.
		value = m_regs.sp[1];
		break;
	case AReg_FP:
		value = m_regs.fp;
		break;
	case AReg_SP:
		value = REG_SP;
		break;
	case AReg_SB:
		value = m_regs.sb;
		break;
	case AReg_PSR:
		value = m_regs.psr;
		break;
	case AReg_INTBASE:
		value = m_regs.intbase;
		break;
	case AReg_MOD:
		value = m_regs.mod;
		break;
	}
}

void ns32016_device::areg_w(int areg, UINT32 value, int width)
{
	switch (areg)
	{
	case AReg_US: //TODO: Are we sure it is user stack? Makes sense giving supervisor access to user stack.
		MASK_WIDTH_W(m_regs.sp[1], value);
		break;
	case AReg_FP:
		MASK_WIDTH_W(m_regs.fp, value);
		break;
	case AReg_SP:
		MASK_WIDTH_W(REG_SP, value);
		break;
	case AReg_SB:
		MASK_WIDTH_W(m_regs.sb, value);
		break;
	case AReg_PSR:
		MASK_WIDTH_W(m_regs.psr, value);
		break;
	case AReg_INTBASE:
		MASK_WIDTH_W(m_regs.intbase, value);
		break;
	case AReg_MOD:
		MASK_WIDTH_W(m_regs.mod, value);
		break;
	}
}

offs_t ns32016_device::gen_size(int gen, offs_t ic, int width)
{
	offs_t size;
	switch(gen)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
	case 23: // Top of stack (POP)
		size = 0;
		break;
	case 8:
	case 9:
	case 10:
	case 11:
	case 12:
	case 13:
	case 14:
	case 15:
	case 21: // Absolute
	case 24: // Memory space
	case 25: // Memory space
	case 26: // Memory space
	case 27: // Memory space (pc-relative?)
		size = disp_size(ic);
		break;
	case 16: // Memory relative
	case 17: // Memory relative
	case 18: // Memory relative
	case 22: // External
		size = disp_size(ic);
		size += disp_size(ic + size);
		break;
	case 19: // RESERVED
		size = -1;
		break;
	case 20: // Immediate
		if (width == NS32016_WIDTH_BYTE)
			size = 1;
		else if (width == NS32016_WIDTH_WORD)
			size = 2;
		else if (width == NS32016_WIDTH_DWORD)
			size = 4;
		break;
	case 28: // Scaled index
	case 29: // Scaled index
	case 30: // Scaled index
	case 31: // Scaled index
		size = -2;
		break;
	}

	return size;
}

// ic must be base-gen
UINT32 ns32016_device::gen_addr(int gen, offs_t ic, int width)
{
	INT32 disp1 = 0, disp2 = 0, result;

	switch(gen)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		Unimpl("gen_addr with register");
		break;
	case 8:
		ic += op_disp(disp1, ic);
		result = m_regs.r0 + disp1;
		break;
	case 9:
		ic += op_disp(disp1, ic);
		result = m_regs.r1 + disp1;
		break;
	case 10:
		ic += op_disp(disp1, ic);
		result = m_regs.r2 + disp1;
		break;
	case 11:
		ic += op_disp(disp1, ic);
		result = m_regs.r3 + disp1;
		break;
	case 12:
		ic += op_disp(disp1, ic);
		result = m_regs.r4 + disp1;
		break;
	case 13:
		ic += op_disp(disp1, ic);
		result = m_regs.r5 + disp1;
		break;
	case 14:
		ic += op_disp(disp1, ic);
		result = m_regs.r6 + disp1;
		break;
	case 15:
		ic += op_disp(disp1, ic);
		result = m_regs.r7 + disp1;
		break;
	case 16: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = m_program->read_dword(m_regs.fp + disp1) + disp2;
		break;
	case 17: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = m_program->read_dword(REG_SP + disp1) + disp2;
		break;
	case 18: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = m_program->read_dword(m_regs.sb + disp1) + disp2;
		break;
	case 19:
		// RESERVED
		break;
	case 20: // Immediate
		//if (width == NS32016_WIDTH_BYTE)
		//{
		//	result = INS_OP8(ic);
		//	ic += 1;
		//}
		//else if (width == NS32016_WIDTH_WORD)
		//{
		//	result = INS_OP16(ic);
		//	ic += 2;
		//}
		//else if (width == NS32016_WIDTH_DWORD)
		//{
		//	result = INS_OP32(ic);
		//	ic += 4;
		//}
		Unimpl("gen_addr with immediate");
		break;
	case 21: // Absolute
		ic += op_disp(disp1, ic);
		result = disp1;
		break;
	case 22: // External
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = m_program->read_dword(LINK_TABLE_ADDR + disp1 * 4) + disp2;
		break;
	case 23: // Top of stack (POP)
		//if (width == NS32016_WIDTH_BYTE)
		//{
		//	POPB(result);
		//}
		//else if (width == NS32016_WIDTH_WORD)
		//{
		//	POPW(result);
		//}
		//else if (width == NS32016_WIDTH_DWORD)
		//{
		//	POPD(result);
		//}
		Unimpl("gen_addr with pop");
		break;
	case 24: // Memory space
		ic += op_disp(disp1, ic);
		result = m_regs.fp + disp1;
		break;
	case 25: // Memory space
		ic += op_disp(disp1, ic);
		result = REG_SP + disp1;
		break;
	case 26: // Memory space
		ic += op_disp(disp1, ic);
		result = m_regs.sb + disp1;
		break;
	case 27: // Memory space (pc-relative?)
		ic += op_disp(disp1, ic);
		result = m_regs.pc + disp1;
		break;
	case 28: // Scaled index
	case 29: // Scaled index
	case 30: // Scaled index
	case 31: // Scaled index
		Unimpl("gen_addr Scaled index");
		break;
	}

	return result;
}


// ic must be base-gen and is increased with gen-size
UINT32 ns32016_device::gen_read(int gen, offs_t ic, int width)
{
	INT32 disp1 = 0, disp2 = 0, result;

	switch(gen)
	{
	case 0:
		result = m_regs.r0;
		break;
	case 1:
		result = m_regs.r1;
		break;
	case 2:
		result = m_regs.r2;
		break;
	case 3:
		result = m_regs.r3;
		break;
	case 4:
		result = m_regs.r4;
		break;
	case 5:
		result = m_regs.r5;
		break;
	case 6:
		result = m_regs.r6;
		break;
	case 7:
		result = m_regs.r7;
		break;
	case 8:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r0 + disp1);
		break;
	case 9:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r1 + disp1);
		break;
	case 10:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r2 + disp1);
		break;
	case 11:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r3 + disp1);
		break;
	case 12:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r4 + disp1);
		break;
	case 13:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r5 + disp1);
		break;
	case 14:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r6 + disp1);
		break;
	case 15:
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.r7 + disp1);
		break;
	case 16: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = MEM_R_DW(m_program->read_dword(m_regs.fp + disp1) + disp2);
		break;
	case 17: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = MEM_R_DW(m_program->read_dword(REG_SP + disp1) + disp2);
		break;
	case 18: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = MEM_R_DW(m_program->read_dword(m_regs.sb + disp1) + disp2);
		break;
	case 19:
		// RESERVED
		break;
	case 20: // Immediate
		if (width == NS32016_WIDTH_BYTE)
		{
			result = INS_OP8(ic);
			ic += 1;
		}
		else if (width == NS32016_WIDTH_WORD)
		{
			result = INS_OP16(ic);
			ic += 2;
		}
		else if (width == NS32016_WIDTH_DWORD)
		{
			result = INS_OP32(ic);
			ic += 4;
		}
		break;
	case 21: // Absolute
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(disp1);
		break;
	case 22: // External
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		result = MEM_R_DW(m_program->read_dword(LINK_TABLE_ADDR + disp1 * 4) + disp2);
		break;
	case 23: // Top of stack (POP)
		if (width == NS32016_WIDTH_BYTE)
		{
			POPB(result);
		}
		else if (width == NS32016_WIDTH_WORD)
		{
			POPW(result);
		}
		else if (width == NS32016_WIDTH_DWORD)
		{
			POPD(result);
		}
		break;
	case 24: // Memory space
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.fp + disp1);
		break;
	case 25: // Memory space
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(REG_SP + disp1);
		break;
	case 26: // Memory space
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.sb + disp1);
		break;
	case 27: // Memory space (pc-relative?)
		ic += op_disp(disp1, ic);
		result = MEM_R_DW(m_regs.pc + disp1);
		break;
	case 28: // Scaled index
	case 29: // Scaled index
	case 30: // Scaled index
	case 31: // Scaled index
		//*buffer += sprintf(*buffer, "Scaled index not implemented");
		Unimpl("Scaled index");
		break;
	}

	return MASK_WIDTH_R(result);
}

// ic must be base-gen
void ns32016_device::gen_write(int gen, offs_t ic, UINT32 value, int width)
{
	INT32 disp1 = 0, disp2 = 0;

	switch(gen)
	{
	case 0:
		MASK_WIDTH_W(m_regs.r0, value);
		break;
	case 1:
		MASK_WIDTH_W(m_regs.r1, value);
		break;
	case 2:
		MASK_WIDTH_W(m_regs.r2, value);
		break;
	case 3:
		MASK_WIDTH_W(m_regs.r3, value);
		break;
	case 4:
		MASK_WIDTH_W(m_regs.r4, value);
		break;
	case 5:
		MASK_WIDTH_W(m_regs.r5, value);
		break;
	case 6:
		MASK_WIDTH_W(m_regs.r6, value);
		break;
	case 7:
		MASK_WIDTH_W(m_regs.r7, value);
		break;
	case 8:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r0 + disp1, value, width);
		break;
	case 9:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r1 + disp1, value, width);
		break;
	case 10:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r2 + disp1, value, width);
		break;
	case 11:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r3 + disp1, value, width);
		break;
	case 12:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r4 + disp1, value, width);
		break;
	case 13:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r5 + disp1, value, width);
		break;
	case 14:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r6 + disp1, value, width);
		break;
	case 15:
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.r7 + disp1, value, width);
		break;
	case 16: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		MEM_W(m_program->read_dword(m_regs.fp + disp1) + disp2, value, width);
		break;
	case 17: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		MEM_W(m_program->read_dword(REG_SP + disp1) + disp2, value, width);
		break;
	case 18: // Memory relative
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		MEM_W(m_program->read_dword(m_regs.sb + disp1) + disp2, value, width);
		break;
	case 19:
		// RESERVED
		break;
	case 20: // Immediate
		assert(false); //TODO: TRAP
		break;
	case 21: // Absolute
		ic += op_disp(disp1, ic);
		MEM_W(disp1, value, width);
		break;
	case 22: // External
		ic += op_disp(disp1, ic);
		ic += op_disp(disp2, ic);
		MEM_W(m_program->read_dword(LINK_TABLE_ADDR + disp1 * 4) + disp2, value, width);
		break;
	case 23: // Top of stack (PUSH)
		if (width == NS32016_WIDTH_BYTE)
		{
			PUSHB(value);
		}
		else if (width == NS32016_WIDTH_WORD)
		{
			PUSHW(value);
		}
		else if (width == NS32016_WIDTH_DWORD)
		{
			PUSHD(value);
		}
		else
			assert(false); // Illegal width
		break;
	case 24: // Memory space
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.fp + disp1, value, width);
		break;
	case 25: // Memory space
		ic += op_disp(disp1, ic);
		MEM_W(REG_SP + disp1, value, width);
		break;
	case 26: // Memory space
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.sb + disp1, value, width);
		break;
	case 27: // Memory space (pc-relative?)
		ic += op_disp(disp1, ic);
		MEM_W(m_regs.pc + disp1, value, width);
		break;
	case 28: // Scaled index
	case 29: // Scaled index
	case 30: // Scaled index
	case 31: // Scaled index
		Unimpl("Scaled index gen not implemented");
		break;
	}
}

void ns32016_device::trap(int trap)
{
	Unimpl("Trap not implemented");
}

void ns32016_device::wait()
{
	Unimpl("Wait not implemented");
}
