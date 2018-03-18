/*****************************************************************************
 *
 *   nc4000.c
 *   NC4000 and derivates
 *
 *   Chips in the family:
 *   Novix Inc.: NC4000, NC4016
 *   Harris Semiconductor: RTX2000
 *   	RTX2000 is NC4000 with on-chip assets: 256 element return stack, 256 element data stack,
 *   	16 x 16 bit single cycle hardware multiplier, three counter/timers and a prioritized vectored interrupt controller
 *
 *****************************************************************************/

#include "emu.h"
#include "debugger.h"
#include "nc4000.h"

const device_type NC4000 = &device_creator<nc4000_device>;

nc4000_device::nc4000_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock)
	: cpu_device(mconfig, NC4000, "NC4000", tag, owner, clock),
	  m_program_config("program", ENDIANNESS_BIG, 16, 16, -1),
	  m_datastack_config("datastack", ENDIANNESS_BIG, 16, 8, -1),
	  m_retstack_config("retstack", ENDIANNESS_BIG, 16, 8, -1)
{

}

nc4000_device::~nc4000_device() {}

UINT32 nc4000_device::disasm_min_opcode_bytes() const
{
	return 2;
}

UINT32 nc4000_device::disasm_max_opcode_bytes() const
{
	return 4;
}


// device_t
void nc4000_device::device_start()
{
	m_program = space(AS_PROGRAM);
	m_datastack = space(AS_1);
	m_retstack = space(AS_2);

	m_icountptr = &m_icount;

	save_item(NAME(m_regs.pc));
	save_item(NAME(m_regs.t));
	save_item(NAME(m_regs.n));
	save_item(NAME(m_regs.i));
	save_item(NAME(m_regs.j));
	save_item(NAME(m_regs.k));
	save_item(NAME(m_regs.md));
	save_item(NAME(m_regs.sr));
	save_item(NAME(m_regs.bdata));
	save_item(NAME(m_regs.bdir));
	save_item(NAME(m_regs.bmask));
	save_item(NAME(m_regs.btri));
	save_item(NAME(m_regs.xdata));
	save_item(NAME(m_regs.xdir));
	save_item(NAME(m_regs.xmask));
	save_item(NAME(m_regs.xtri));
	save_item(NAME(carry));

	state_add(NC4000_PC,       "PC",        m_regs.pc);
	state_add(STATE_GENPC,     "GENPC",     m_regs.pc).noshow();
	state_add(STATE_GENPCBASE, "GENPCBASE", m_ppc).noshow();

	state_add(NC4000_T,  "T",  m_regs.t);
	state_add(NC4000_N,  "N",  m_regs.n);
	state_add(NC4000_I,  "I",  m_regs.i);
//	state_add(NC4000_JK, "JK", m_regs.jk.w);
	state_add(NC4000_J,  "J",  m_regs.j);
	state_add(NC4000_K,  "K",  m_regs.k);
	state_add(NC4000_MD, "MD", m_regs.md);
	state_add(NC4000_SR, "SR", m_regs.sr);
	state_add(NC4000_carry, "carry", carry).mask(1);

	state_add(NC4000_Bdata, "Bdata", m_regs.bdata);
	state_add(NC4000_Bdir,  "Bdir",  m_regs.bdir);
	state_add(NC4000_Bmask, "Bmask",  m_regs.bmask);
	state_add(NC4000_Btri,  "Btri",  m_regs.btri);
	state_add(NC4000_Xdata, "Xdata", m_regs.xdata).mask(0x1f);
	state_add(NC4000_Xdir,  "Xdir",  m_regs.xdir).mask(0x1f);
	state_add(NC4000_Xmask, "Xmask", m_regs.xmask).mask(0x1f);
	state_add(NC4000_Xtri,  "Xtri",  m_regs.xtri).mask(0x1f);

	// Resolve device callbacks
	m_in_portb_func.resolve(m_in_portb_cb, *this);
	m_out_portb_func.resolve(m_out_portb_cb, *this);
	m_in_portx_func.resolve(m_in_portx_cb, *this);
	m_out_portx_func.resolve(m_out_portx_cb, *this);
	m_in_irq_func.resolve(m_in_irq_cb, *this);
}

void nc4000_device::device_reset()
{
	m_regs.pc = 0x1000; // TODO: can't we have word offset?
	m_regs.t = m_regs.n = m_regs.i = m_regs.md = m_regs.sr = 0;
	m_regs.j = m_regs.k = 0;

	// TODO: Init port registers?
	m_regs.bdata = m_regs.bdir = m_regs.btri = m_regs.bmask = 0;
	m_regs.xdata = m_regs.xdir = m_regs.xtri = m_regs.xmask = 0;

	m_irq_state = m_irq_latch = CLEAR_LINE;
	m_icount = 0;

	//
	// Old impl
	//
	intreq = 0;
	carry = 0;
	timesmode = 0;
	binput = 0;
	xinput = 0;
}

void nc4000_device::device_config_complete()
{
	// inherit a copy of the static data
	const nc4000_interface *intf = reinterpret_cast<const nc4000_interface *>(static_config());
	if (intf != NULL)
	{
		*static_cast<nc4000_interface *>(this) = *intf;
	}
	// or initialize to defaults if none provided
	else
	{
		memset(&m_in_portb_func, 0, sizeof(m_in_portb_func));
		memset(&m_out_portb_func, 0, sizeof(m_out_portb_func));
		memset(&m_in_portx_func, 0, sizeof(m_in_portx_func));
		memset(&m_out_portx_func, 0, sizeof(m_out_portx_func));
		memset(&m_in_irq_func, 0, sizeof(m_in_irq_func));
	}
}

#define PUSH_R { m_retstack->write_word(m_regs.j << 1, m_regs.i); m_regs.j--; }
#define PUSH_TO_R(x) { PUSH_R; m_regs.i = x; }
#define POP_R { m_regs.j++; m_regs.i = m_retstack->read_word(m_regs.j << 1); }
#define POP_R_TO(x) { x = m_regs.i; POP_R }
#define PUSH_S { m_datastack->write_word(m_regs.k << 1, m_regs.n); m_regs.k--; }
#define POP_S { m_regs.k++; m_regs.n = m_datastack->read_word(m_regs.k << 1); }
#define READ_M(x) m_program->read_word(x << 1)
#define WRITE_M(x,d) m_program->write_word(x << 1, d)

// device_execute_interface
void nc4000_device::execute_run()
{
	bool check_debugger = ((device_t::machine().debug_flags & DEBUG_FLAG_ENABLED) != 0);

	if (intreq && (m_regs.xtri & MASK_INTREQ)) { //Check for pending interrupt
		interrupt();
	}

	do {
		m_ppc = m_regs.pc;
		if (check_debugger)
			debugger_instruction_hook(this, m_regs.pc);

		UINT16 op;

		op = READ_M(m_regs.pc);

		if (timesmode) {
			m_regs.i--;
			if (m_regs.i == 0)
			{
				POP_R;
				timesmode = false;
			}
		}

		if (timesmode == false)
		{
			m_regs.pc++;
		}

//		if (timesmode) { // TIMES instruction handling
//			op = READ_M(m_regs.pc);
//			if (m_regs.i != 0xFFFF)
//				m_regs.i--;
//			else
//			{
//				POP_R;
//				m_regs.pc++;
//				timesmode = false;
//			}
//		}
//		if (!timesmode)
//		{
//			op = READ_M(m_regs.pc);// Instruction increment
//			m_regs.pc++;
//		}

		switch (op & 0xF000) { //Mask and shift instruction type field
			case 0x8000: 		// *** ALU instruction: 1000b ***
				ALU_Op(op);
				m_icount--;
				break;
			case 0x9000: 		// *** IF(conditional branch): 1001b ***

				if (m_regs.t == 0)
					m_regs.pc = BADDR(m_ppc); // P gets masked value from instruction

				m_regs.t = m_regs.n; //Pop value from data stack
				POP_S;
				// Observe: One cannot branch across 4K page boundary
				m_icount--;
				break;
			case 0xA000:		// *** LOOP: 1010b ***
				if (m_regs.i)
				{
					m_regs.pc = BADDR(m_ppc); // P gets masked value from instruction
					m_regs.i--;				// Decrement loop
				}
				else
					POP_R;
				// Observe: One cannot branch across 4K page boundary
				m_icount--;
				break;
			case 0xB000:		// *** ELSE(unconditional branch): 1011b ***
				m_regs.pc = BADDR(m_ppc);
				// Observe: One cannot branch across 4K page boundary
				m_icount--;
				break;
			case 0xC000:		// *** Literal/Local/Internal fetch: 1100b ***
				m_icount -= IO_LLI(op, 0);
				break;
			case 0xD000:		// *** Literal/Local/Internal store: 1101b ***
				m_icount -= IO_LLI(op, 1);
				break;
			case 0xE000:		// *** Memory fetch: 1110b ***
				IO_Mem(op, 0);
				m_icount -= 2; //# cycles for memory instruction execution
				break;
			case 0xF000:		// *** Memory store: 1111b ***
				IO_Mem(op, 1);
				m_icount -= 2; //# cycles for memory instruction execution
				break;
			default:		// *** Call instruction: 0111b or lower ***
				PUSH_TO_R(m_regs.pc | (carry<<15)); // Save carry and P
				m_regs.pc = op; //call address -> P
				m_icount--;
				break;
		}
	} while (m_icount > 0);
}

// device_memory_interface
const address_space_config *nc4000_device::memory_space_config(address_spacenum spacenum) const
{
	return	(spacenum == AS_PROGRAM) ? &m_program_config :
			(spacenum == AS_1) ? &m_datastack_config :
			(spacenum == AS_2) ? &m_retstack_config :
			NULL;
}

// device_disasm_interface
offs_t nc4000_device::disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options)
{
	//logerror("options=%i rom=%p ram=%p\n", options, oprom, opram);
	extern CPU_DISASSEMBLE( nc4000 );
	return CPU_DISASSEMBLE_NAME( nc4000 )(NULL, buffer, pc, oprom, opram, 0);
}

void nc4000_device::ALU_Op(UINT16 op) {
	UINT16 op2, result, carry_in;

	//
	// 1. Get operand 2
	//

	switch (OP_YSRC) {		// get operand 2
	case YSRC_N:
	case YSRC_NC:
		op2 = m_regs.n;
		break;
	case YSRC_MD:
		op2= m_regs.md;
		break;
	case YSRC_SR:
		op2 = m_regs.sr;
		break;
	default:
		break;
	}

	// manage Tn & Sa bits
	if (OP_TN) {	// Tn bit set..
		if (OP_SA)	{// ..and Sa bit set
			PUSH_S;
		}
		m_regs.n = m_regs.t;
	} else {			// Tn bit not set..
		if (OP_SA)	{// ..but Sa bit set
			POP_S;
		}
	}

	//
	// 2. Do arithmetic operation
	//

	// check if N with carry. If so move carry to carry_in
	if (OP_YSRC == YSRC_NC)
		carry_in=carry;
	else
		carry_in=0;

	// Perform ALU function
	result=DoALU(op2, carry_in, OP_ALUOP);

	//
	// 3. Write results to T (if allowed by step instructions)
	//
//	if (!( (OP_DIV && !carry) || (OP_YSRC == YSRC_NC && (m_regs.n & 0x0001))))
//		m_regs.t = result; //also check extra div cond (/").

	if (!OP_STEP)
	{
		m_regs.t = result;
	}
	else
	{
		if (OP_DIV) // Division or Sqrt
		{
			if (OP_CFLAG) // Sqrt
			{
				logerror("Square root step - unimplemented\n");
				// TODO
			}
			else // Division
			{
				logerror("Division step: carry=%i, result=%04x, t=%04x\n", carry, result, m_regs.t);
				if (carry)
					m_regs.t = result;
			}
		}
		else if (!OP_CFLAG) // Multiplication
		{
			logerror("Multiplication step: carry=%i, result=%04x, t=%04x, n=%04x\n", carry, result, m_regs.t, m_regs.n);
			if (m_regs.n & 0x0001)
				m_regs.t = result;
		}
	}

//	if (OP_STEP && ((OP_DIV && !OP_CFLAG && !carry) || (!OP_DIV && !OP_CFLAG && !(m_regs.n & 0x0001))))
//	{
//
//	}
//	else
//	{
//		m_regs.t = result; //also check extra div cond (/").
//	}

	//
	// 4. Do shift operation
	//
	if (OP_SH32)
	{
		switch (OP_SHDIR)
		{
		case SHDIR_NONE:			// Special case: left shift N only
			m_regs.n = (m_regs.n<<1) | carry; // carry into N
			break;
		case SHDIR_LSR:			// 32-bit shift right (T as most sign. word)
			m_regs.n = (m_regs.n>>1) | ((m_regs.t<<15) & 0x8000);
			m_regs.t >>= 1;
			break;
		case SHDIR_SHL:			// 32-bit shift left (T as most sign. word)
			m_regs.t = (m_regs.t<<1) | ((m_regs.n>>15) & 0x0001); // 0x0001 is just to make sure unsigned
			m_regs.n = (m_regs.n<<1) | carry; // carry into N
			break;
		default:		// SHDIR_ASR & OP_SH32 not valid combination
			break;
		}
	}
	else
	{
		if (op==0x8C11)
		{
			logerror("wtf!!\n");
		}
		switch (OP_SHDIR)
		{
		case SHDIR_NONE:			// No shift
			break;
		case SHDIR_LSR:			// Shift result right
			logerror("lsr: t=%x, msbcar=%x, reslt=%x\n",m_regs.t, carry<<15, m_regs.t | carry << 15);
			m_regs.t >>= 1;
			m_regs.t |= carry << 15;
			break;
		case SHDIR_SHL:			// Shift result left
			m_regs.t <<= 1;
			m_regs.t |= carry;
			break;
		case SHDIR_ASR:			// Propagate msb (used as '0<')
			m_regs.t = (m_regs.t & 0x8000) ? 0xFFFF : 0;
			break;
		}
	}

	//
	// 5. Return bit (;) handling
	//
	if (IS_RET && !timesmode) {
		m_regs.pc = m_regs.i & 0x7FFF;
		carry = m_regs.i & 0x8000 ? 1 : 0; // restore P and carry
		POP_R;
	}
}

UINT16 nc4000_device::DoALU(UINT16 Op_Y, UINT16 carry_in, UINT16 ALU_func) {
	UINT16 result;
	UINT32 c_check;

	result=carry_in;	// Get carry in (if any)
	carry=0;	//Reset carry now and correct it in ALU operation

	// Perform ALU function
	switch (ALU_func) {
	case ALUOP_T:		//Pass T
		result = m_regs.t; // D-uh!
		break;
	case ALUOP_AND:		//Bitwise AND
		result = m_regs.t & Op_Y;
		break;
	case ALUOP_SUB:		//T-Op_Y
		c_check = (UINT32)m_regs.t + (~Op_Y) + 1 + result;
//		c_check = m_regs.t + ((-Op_Y)&0xFFFF)  + result;
		result = c_check & 0xFFFF;	//mask out unit size
		logerror("alu sub(%x): cin=%x, c_check=%x, t=%x, y=%x(%x), result = %x\n", m_regs.pc, carry_in, c_check, m_regs.t, Op_Y, ~Op_Y, result);
		if (c_check & 0x10000)
		{
			logerror("alu sub generated carry c_check=%x, car=%x\n",c_check, c_check& 0x10000);
			carry=1;	//overflow/carry?
		}
		else
		{
			carry=0;
		}
		break;
	case ALUOP_OR:		//Bitwise OR
		result = m_regs.t | Op_Y;
		break;
	case ALUOP_ADD:		//T+Op_Y
		c_check = (UINT32)m_regs.t + Op_Y + result;
		result = c_check & 0xFFFF;	//mask out unit size
		if (c_check & 0x10000)
			carry=1;	//overflow/carry?
		else
			carry=0;
		break;
	case ALUOP_XOR:		//Bitwise XOR
		result = m_regs.t ^ Op_Y;
		break;
	case ALUOP_SUBY:		//Op_Y-T	(Real stack subtraction)
		c_check = (UINT32)Op_Y + (~m_regs.t) + 1 + result;
//		c_check = (UINT32)Op_Y + ((-m_regs.t)&0xFFFF) + result;
		result=c_check & 0xFFFF;	//mask out unit size
		if (c_check & 0x10000)
			carry=1;	//overflow/carry?
		else
			carry=0;
		break;
	case ALUOP_Y:		//Pass Opy
		result = Op_Y;
		break;
	default:
		break;
	}
	return result;
}

// C    B   C   6
// 14   5  7  0  6
// 1100 101111000110
// LLIr
// ALU  101--------- XOR
// IOS  ---111------ Internal fetch (reg is op2)
// CIN  ----1------- use carry in
// TN   -----1------ copy t to n
// ;    ------0----- no ret
// SA   -------0---- stack not active
// 4BIT --------0110 SR

// 14   7  3  0  0
// 1100 111011000000
// LLIr
// ALU  111--------- Y
// IOS  ---011------ Internal fetch (reg is op2)
// CIN  ----1------- use carry in
// TN   -----1------ copy t to n
// ;    ------0----- no ret
// SA   -------0---- stack not active
// 4BIT --------0000 SR


// 1100 111 111 001100

int nc4000_device::IO_LLI(UINT16 op, int store)
{
	int cycles =1;
	UINT16 swap, M;

	if (store==0) {				// Fetch type
		switch (OP_IOS>>6) { // Y,C,SA
			case 0:
			case 1:
			case 2:	// Local fetch
				if (OP_TN) { PUSH_S; m_regs.n = m_regs.t; }	// Tn bit active
				M = READ_M(OP_5BIT);
				m_regs.t = DoALU(M, CIN, OP_ALUOP);
				cycles=2;
				break;
			case 3:
			case 7:	// Internal fetch
				if (!(op & 0x0100)) { PUSH_S; m_regs.n = m_regs.t; }	// Tn bit active
//				logerror("%04x @ %04x SA=%i, m=%i\n", op, m_regs.pc, OP_SA>0, OP_IOS>>6);
				m_regs.t = DoALU(int_read_reg(OP_4BIT), 0, OP_ALUOP);
				if (OP_SA)
					POP_R;
				break;
			default: // 4,5,6.  16-bit literal fetch
				if (OP_TN) { PUSH_S; m_regs.n = m_regs.t; }	// Tn bit active
				M = READ_M(m_regs.pc); m_regs.pc++;
				m_regs.t = DoALU(M, CIN, OP_ALUOP);
				cycles=2;
				break;
		}
	} else {					// Store type
		switch (OP_IOS>>6) {
			case 0:
			case 1: // Local store
				M = OP_5BIT;
				WRITE_M(M, m_regs.t);
				m_regs.t = DoALU(m_regs.n, CIN, OP_ALUOP);
				if (!OP_TN)
					POP_S; // Tn bit for stores
				cycles=2;
				break;
			case 2:
			case 3:	// Internal store
				if (OP_SA)
				{
					PUSH_R;
					timesmode=true;
				}
				else if (OP_4BIT == 1)
					PUSH_R;

				int_write_reg(OP_4BIT, m_regs.t);
				m_regs.t = DoALU(m_regs.n, CIN, OP_ALUOP);
				if (!OP_TN)
					POP_S; // Tn bit for stores
				break;
			case 7: // Internal swap
				if (OP_SA)
				{
					PUSH_R;
					timesmode=true;
				}
				swap = int_read_reg(OP_4BIT);
				int_write_reg(OP_4BIT, m_regs.t);
				m_regs.t = DoALU(swap, CIN, OP_ALUOP);
				break;
			default: // Short literal fetch
				if (OP_TN) { PUSH_S; m_regs.n = m_regs.t;}	// Tn bit active
				m_regs.t = DoALU(OP_5BIT, CIN, OP_ALUOP);
				break;
		}

	}
	// Handle return bit (Illegal results in some cases)
	// OBS!!! a times instruction cannot return as well in this implementation!
	// On the other hand this would be improbable since the return stack would be disturbed
	if (IS_RET && !timesmode) {
		m_regs.pc = m_regs.i & 0x7FFF;
		carry = m_regs.i & 0x8000 ? 1 : 0; // restore P and carry
		POP_R;
	}
	return cycles;
}
void nc4000_device::IO_Mem(UINT16 op, int store)
{
	if (store==0) {
    	if (OP_IOS == OPMASK_IOS) { // All bits set
			PUSH_S;
            m_regs.n = READ_M(m_regs.t);
            m_regs.t = DoALU(OP_5BIT, 0, OP_ALUOP);
        } else {
			// Extended memory read is ignored
	        m_regs.t = READ_M(m_regs.t); // Cheating...
			if (!OP_TN) { m_regs.t = DoALU(m_regs.n, 0, OP_ALUOP); POP_S; }	// Tn bit active
        }

        // DUP @ SWAP nn + (1647nn) ignored
	} else {
       	WRITE_M(m_regs.t, m_regs.n); POP_S;
    	if  (OP_IOS == OPMASK_IOS) { // All bits set
            m_regs.t = DoALU(OP_5BIT, 0, OP_ALUOP);
        } else {
	        // Extended memory write is ignored
            if (!OP_TN) {m_regs.t = DoALU(m_regs.n, 0, OP_ALUOP); POP_S; }
        }
    }
}

void nc4000_device::interrupt() {
	if (m_regs.xtri & MASK_INTREQ) {	// Bit 8 in Xtri set? (interrupt enable)
		PUSH_TO_R(m_regs.pc);
		m_regs.pc = 0x0020;	//0x0020 = Interrupt vector
		intreq = false;
	} else {
		intreq = true;
	}
}

#define XINPUT (m_in_portx_func.isnull() ? xinput : m_in_portx_func(0))
#define BINPUT (m_in_portb_func.isnull() ? binput : m_in_portb_func(0))

UINT16 nc4000_device::int_read_reg(UINT16 reg)
{ //Kind of self-explanatory really...
	switch (reg)
	{
	case 0:	// JK register
		return m_regs.k<<8 | m_regs.j;
	case 1:
		return m_regs.i;
	case 2:
		return m_regs.pc;
	case 3:
		return 0xFFFF; // Logical true (-1)
	case 4:
	case 5:
		return m_regs.md;
	case 6:
	case 7:
		return m_regs.sr;
	case 8:
		if (!m_in_portb_func.isnull())
			binput = m_in_portb_func(0, 0xffff);
		else
			logerror("No b reader handler defined\n");
		return ((binput ^ m_regs.bdata) & ~m_regs.bdir) | (m_regs.bdata & m_regs.bdir); // (XOR'ed) Xdata
	case 9:
		return m_regs.bmask;
	case 10:
		return m_regs.bdir;
	case 11:
		return m_regs.btri;
	case 12:
		if (!m_in_portx_func.isnull())
			xinput = m_in_portx_func(0);
		else
			logerror("No x reader handler defined\n");
		return (((xinput ^ m_regs.xdata) & ~m_regs.xdir) | (m_regs.xdata & m_regs.xdir)) & 0x1F; // (XOR'ed) Xdata
	case 13:
		return m_regs.xmask & 0x1F;
	case 14:
		return m_regs.xdir & 0x1F;
	case 15:
		return m_regs.xtri & 0x1F;
	default:
		return 0;
	}
}


void nc4000_device::int_write_reg(UINT16 reg, UINT16 data)
{
	switch (reg)
	{
	case 0: // JK register
		m_regs.k = 0xFF & data>>8;
		m_regs.j = 0xFF & data;
		break;
	case 1:
		m_regs.i = data;
		break;
	case 2:
		m_regs.pc = data;
		break;
	case 4:
	case 5:
		m_regs.md = data;
		break;
	case 6:
	case 7:
		m_regs.sr = data;
		break;
	case 8:
		m_regs.bdata = data & ~m_regs.bmask;
		if (!m_out_portb_func.isnull())
			m_out_portb_func(0, m_regs.bdata, 0xffff);
		else
			logerror("No b writer handler defined\n");
		break;
	case 9:
		m_regs.bmask = data;
		break;
	case 10:
		m_regs.bdir = data;
		break;
	case 11:
		m_regs.btri = data;
		break;
	case 12:
//		logerror("write x %02x, %i\n", data, m_out_portx_func.isnull());
		m_regs.xdata = data & ~ m_regs.xmask & 0x1F;
		if (!m_out_portx_func.isnull())
			m_out_portx_func(0, m_regs.xdata);
		else
			logerror("No x writer handler defined\n");
		break;
	case 13:
		m_regs.xmask = data & 0x1F;
		break;
	case 14:
		m_regs.xdir = data & 0x1F;
		break;
	case 15:
		m_regs.xtri = data & 0x1F;
		break;
	default:
		break;
	};
	return;
}


/* =====================
 * TODO Verify old code:
 * =====================
 *
 * TIMES instruction handling had a probable if-else bug
 * *** Bug
 *
 * Conditional branch(IF) branches on T!=0 despite Koopman saying T==0?
 * *** Ting p24 says instruction is DROP if T=0, implying branch on T!=0
 *
 * LOOP is code A and unconditional branch(ELSE) is code B. Koopman says other way around (LOOP=A,ELSE=B)
 * *** Newimpl had error. Ting p22 says LOOP=A, ELSE=B
 *
 * When CALLing, carry is preserved in top bit. Where does it say this? It is restored/masked on ret...
 *
 * When fetching Y we do not take N with carry into account. Verify that we do that when performing ALU work
 * *** OK. Passed through carry_in (always 0 if no carry)
 *
 * Read/Write registers has J as high byte and K as low byte. Koopman says that JK has K as high byte.
 * *** Error, Ting p12 says datastack is highbyte.
 *
 * On IO/Mem instructions, CIN (bit7) is *not* the actual carry in, it indicates usage of carry
 * *** Oldimpl bug. Ting p31 explains this. Fixed through change in CIN macro
 *
 */

READ8_MEMBER( nc4000_device::portx_r )
{
	//logerror("read port x = %02x (data=%02x, dir=%02x, tri=%02x)", m_regs.xdata & m_regs.xdir & ~m_regs.xtri & 0x1F, m_regs.xdata, m_regs.xdir, m_regs.xtri);
	return m_regs.xdata & m_regs.xdir & ~m_regs.xtri & 0x1F;
}
WRITE8_MEMBER( nc4000_device::portx_w )
{
	xinput = data & 0x1F;
}

READ16_MEMBER( nc4000_device::portb_r )
{
	//logerror("read port b = %02x (data=%02x, dir=%02x, tri=%02x)", m_regs.bdata & m_regs.bdir & ~m_regs.btri, m_regs.bdata, m_regs.bdir, m_regs.btri);
	return m_regs.bdata & m_regs.bdir & ~m_regs.btri;
}
WRITE16_MEMBER( nc4000_device::portb_w )
{
	binput = data;
}
