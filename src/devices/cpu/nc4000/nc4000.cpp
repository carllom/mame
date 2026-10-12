// license:BSD-3-Clause
// copyright-holders:Carl Lom
/*****************************************************************************

    Novix NC4000 / NC4016

    Chips in the family:
    Novix Inc.: NC4000, NC4016
    Harris Semiconductor: RTX2000
        RTX2000 is NC4000 with on-chip assets: 256 element return stack,
        256 element data stack, 16 x 16 bit single cycle hardware multiplier,
        three counter/timers and a prioritized vectored interrupt controller

    Reference: C. H. Ting, "Footsteps in an Empty Valley" (3rd ed.), which
    also contains the cmFORTH source listing used to verify the details below.

    Instruction classes (bits 15-12):
    0xxx  call to the 15-bit address (P pushed on the return stack, carry in bit 15)
    1000  ALU: | ALU(3) | Y(2) | TN | ; | SA | D? | % | SL | SR |
    1001  IF: drop T, branch within the 4K page if T was zero
    1010  NEXT: branch and decrement I if I is non-zero, else pop the return stack
    1011  ELSE: unconditional branch within the 4K page
    1100  fetch from local memory, literal or internal register
    1101  store to local memory or internal register, short literal
    1110  fetch from memory addressed by T
    1111  store to memory addressed by T
    The I/O and memory classes are | ALU(3) | m(3) | ; | literal(5) |, where m
    selects the variant (Table 3.6 in the book).

    Notes:
    - The carry after a subtraction is a borrow. 2/ and D2/ combined with
      an arithmetic operation shift its carry into bit 15, which is how
      cmFORTH's U< works ( - 2/ 0< ); otherwise they keep the sign, which is
      why the 1987 cmFORTH clears the carry with 0 + before 2/ in 2/MOD.
    - Logic and pass operations leave the carry alone.
    - TIMES repeats the following instruction for I+2 cycles: I+2 times for a
      single cycle instruction, I+1 times for a memory instruction since its
      first pass takes two cycles (cmFORTH relies on both).
    - The return address on the return stack holds the carry in bit 15;
      a return restores it.

    TODO:
    - X@ and X! (extended memory) do not work on the prototype chip and
      are executed as their plain memory counterparts
    - tristate registers do not affect the port outputs

*****************************************************************************/

#include "emu.h"
#include "nc4000.h"
#include "nc4000d.h"

DEFINE_DEVICE_TYPE(NC4000, nc4000_device, "nc4000", "Novix NC4000")

nc4000_device::nc4000_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock)
	: cpu_device(mconfig, NC4000, tag, owner, clock)
	, m_program_config("program", ENDIANNESS_BIG, 16, 16, -1)
	, m_datastack_config("datastack", ENDIANNESS_BIG, 16, 8, -1)
	, m_retstack_config("retstack", ENDIANNESS_BIG, 16, 8, -1)
	, m_in_portb_cb(*this, 0xffff)
	, m_out_portb_cb(*this)
	, m_in_portx_cb(*this, 0x1f)
	, m_out_portx_cb(*this)
	, m_multiply_bug(true)
	, m_icount(0)
{
}

device_memory_interface::space_config_vector nc4000_device::memory_space_config() const
{
	return space_config_vector{
		std::make_pair(AS_PROGRAM, &m_program_config),
		std::make_pair(AS_DATA, &m_datastack_config),
		std::make_pair(AS_IO, &m_retstack_config)
	};
}

std::unique_ptr<util::disasm_interface> nc4000_device::create_disassembler()
{
	return std::make_unique<nc4000_disassembler>();
}

void nc4000_device::device_start()
{
	space(AS_PROGRAM).cache(m_cache);
	space(AS_PROGRAM).specific(m_program);
	space(AS_DATA).specific(m_datastack);
	space(AS_IO).specific(m_retstack);

	set_icountptr(m_icount);

	m_pc = m_ppc = 0;
	m_t = m_n = m_i = 0;
	m_j = m_k = 0;
	m_md = m_sr = 0;
	m_carry = 0;
	m_bdata = m_bmask = m_bdir = m_btri = 0;
	m_xdata = m_xmask = m_xdir = 0;
	m_xtri = 0;
	m_times_op = 0;
	m_times = m_times_first = m_times_last = false;
	m_int_line = m_int_request = m_int_active = false;
	m_int_j = 0;

	state_add(NC4000_PC,    "PC",    m_pc);
	state_add(STATE_GENPC,     "GENPC",     m_pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC",     m_ppc).noshow();
	state_add(NC4000_T,     "T",     m_t);
	state_add(NC4000_N,     "N",     m_n);
	state_add(NC4000_I,     "I",     m_i);
	state_add(NC4000_J,     "J",     m_j);
	state_add(NC4000_K,     "K",     m_k);
	state_add(NC4000_MD,    "MD",    m_md);
	state_add(NC4000_SR,    "SR",    m_sr);
	state_add(NC4000_CARRY, "carry", m_carry).mask(1);
	state_add(NC4000_BDATA, "Bdata", m_bdata);
	state_add(NC4000_BMASK, "Bmask", m_bmask);
	state_add(NC4000_BDIR,  "Bdir",  m_bdir);
	state_add(NC4000_BTRI,  "Btri",  m_btri);
	state_add(NC4000_XDATA, "Xdata", m_xdata).mask(0x1f);
	state_add(NC4000_XMASK, "Xmask", m_xmask).mask(0x1f);
	state_add(NC4000_XDIR,  "Xdir",  m_xdir).mask(0x1f);
	state_add(NC4000_XTRI,  "Xtri",  m_xtri).mask(0x11f);

	save_item(NAME(m_pc));
	save_item(NAME(m_ppc));
	save_item(NAME(m_t));
	save_item(NAME(m_n));
	save_item(NAME(m_i));
	save_item(NAME(m_j));
	save_item(NAME(m_k));
	save_item(NAME(m_md));
	save_item(NAME(m_sr));
	save_item(NAME(m_carry));
	save_item(NAME(m_bdata));
	save_item(NAME(m_bmask));
	save_item(NAME(m_bdir));
	save_item(NAME(m_btri));
	save_item(NAME(m_xdata));
	save_item(NAME(m_xmask));
	save_item(NAME(m_xdir));
	save_item(NAME(m_xtri));
	save_item(NAME(m_times_op));
	save_item(NAME(m_times));
	save_item(NAME(m_times_first));
	save_item(NAME(m_times_last));
	save_item(NAME(m_int_line));
	save_item(NAME(m_int_request));
	save_item(NAME(m_int_active));
	save_item(NAME(m_int_j));
}

void nc4000_device::device_reset()
{
	m_pc = 0x1000;
	m_carry = 0;

	// ports come up as inputs with interrupts disabled
	m_bdata = m_bmask = m_bdir = m_btri = 0;
	m_xdata = m_xmask = m_xdir = 0;
	m_xtri = 0;

	m_times = m_times_first = m_times_last = false;
	m_int_request = m_int_active = false;
}

void nc4000_device::execute_set_input(int inputnum, int state)
{
	if (inputnum != INT_LINE)
		return;

	// a high to low edge on INT sets the request flip-flop, even with interrupts disabled
	bool const line = state == ASSERT_LINE;
	if (line && !m_int_line)
		m_int_request = true;
	m_int_line = line;
}


//**************************************************************************
//  STACKS
//**************************************************************************

// the stack pointers are pre-decremented on push and post-incremented on pop

void nc4000_device::push_s()
{
	m_k--;
	m_datastack.write_word(m_k, m_n);
}

void nc4000_device::pop_s()
{
	m_n = m_datastack.read_word(m_k);
	m_k++;
}

void nc4000_device::push_r(uint16_t data)
{
	m_j--;
	m_retstack.write_word(m_j, m_i);
	m_i = data;
}

uint16_t nc4000_device::pop_r()
{
	uint16_t const data = m_i;
	m_i = m_retstack.read_word(m_j);
	m_j++;
	return data;
}

void nc4000_device::do_return()
{
	uint16_t const ret = pop_r();
	m_pc = ret & 0x7fff;
	m_carry = BIT(ret, 15);

	// returning past the interrupt call resets the request flip-flop
	if (m_int_active && int8_t(m_j - m_int_j) > 0)
	{
		m_int_active = false;
		m_int_request = false;
	}
}


//**************************************************************************
//  EXECUTION
//**************************************************************************

void nc4000_device::execute_run()
{
	do
	{
		uint16_t op;
		bool latched = false;
		bool repeat = false;

		if (m_times)
		{
			// the latched instruction repeats without being fetched; its return only
			// happens on the last pass, after the count has been popped
			m_ppc = m_pc;
			debugger_instruction_hook(m_pc);
			op = m_times_op;
			latched = true;
			if (m_i == 0 && m_times_last)
			{
				m_times = false;
				pop_r();
				m_pc++;
			}
			else
			{
				repeat = true;
				op &= ~0x0020;
			}
		}
		else if (m_int_request && !m_int_active && BIT(m_xtri, 8))
		{
			m_int_active = true;
			push_r(m_pc | (m_carry << 15));
			m_int_j = m_j;
			m_pc = 0x0020;
			m_icount--;
			continue;
		}
		else
		{
			m_ppc = m_pc;
			debugger_instruction_hook(m_pc);
			op = m_cache.read_word(m_pc++);
		}

		int cycles = 1;
		if (!BIT(op, 15))
		{
			// call: P (with the carry in bit 15) is pushed on the return stack
			push_r(m_pc | (m_carry << 15));
			m_pc = op;
		}
		else
		{
			switch (BIT(op, 12, 3))
			{
			case 0: // ALU
				alu_op(op);
				break;

			case 1: // IF: branch if T is zero, drop T either way
			{
				bool const zero = m_t == 0;
				m_t = m_n;
				pop_s();
				if (zero)
					m_pc = (m_pc & 0xf000) | (op & 0x0fff);
				break;
			}

			case 2: // NEXT
				if (m_i != 0)
				{
					m_i--;
					m_pc = (m_pc & 0xf000) | (op & 0x0fff);
				}
				else
				{
					pop_r();
				}
				break;

			case 3: // ELSE
				m_pc = (m_pc & 0xf000) | (op & 0x0fff);
				break;

			case 4:
				cycles = fetch_op(op);
				break;

			case 5:
				cycles = store_op(op);
				break;

			case 6:
				cycles = memory_fetch_op(op);
				break;

			case 7:
				cycles = memory_store_op(op);
				break;
			}
		}

		if (latched)
		{
			// TIMES counts cycles: the first pass of a two cycle instruction uses up two,
			// after that the latched instruction needs no fetch and takes one cycle
			if (!m_times_first)
				cycles = 1;
			m_times_first = false;
			if (repeat)
			{
				for (int i = 0; i < cycles && m_times; i++)
				{
					if (m_i != 0)
						m_i--;
					else if (!m_times_last)
						m_times_last = true;
					else
					{
						m_times = false;
						pop_r();
						m_pc++;
					}
				}
			}
		}
		m_icount -= cycles;
	}
	while (m_icount > 0);
}

uint16_t nc4000_device::alu(int func, uint16_t t, uint16_t y, int cin, bool update_carry)
{
	uint32_t result;

	switch (func)
	{
	case 0: return t;
	case 1: return t & y;
	case 3: return t | y;
	case 5: return t ^ y;
	case 7: return y;
	case 2: result = uint32_t(t) - y - cin; break;
	case 4: result = uint32_t(t) + y + cin; break;
	case 6: default: result = uint32_t(y) - t - cin; break;
	}

	// carry out of an addition, borrow out of a subtraction
	if (update_carry)
		m_carry = BIT(result, 16);
	return uint16_t(result);
}

void nc4000_device::alu_op(uint16_t op)
{
	int const func = BIT(op, 9, 3);
	int const ysel = BIT(op, 7, 2);
	bool const tn = BIT(op, 6);
	bool const sa = BIT(op, 4);
	bool const dbl = BIT(op, 3);
	bool const div = BIT(op, 2);
	int const shift = BIT(op, 0, 2);

	uint16_t const y = (ysel < 2) ? m_n : (ysel == 2) ? m_md : m_sr;
	uint16_t t = m_t;

	if (div)
	{
		// conditional subtraction: /' and /" (Y=MD), S' (Y=MD|SR) and MOD' (no shift)
		uint16_t const test = (ysel == 3) ? (m_md | m_sr) : y;
		uint16_t const diff = alu(func, t, test, 0, true);
		int const q = m_carry ^ 1;
		if (q)
		{
			t = diff;
			if (ysel == 3)
				m_md |= m_sr << 1;
		}
		if (ysel == 3)
			m_sr >>= 1;

		if (dbl && shift == 2)
		{
			m_t = (t << 1) | BIT(m_n, 15);
			m_n = (m_n << 1) | q;
		}
		else
		{
			m_t = t;
			if (dbl)
				m_n = (m_n << 1) | q;
		}
	}
	else if (ysel == 2 && dbl && (shift == 1 || shift == 2) && func != 0 && func != 7)
	{
		// multiply steps *' *- (shift right) and *F (shift left): MD is added or
		// subtracted if bit 0 of N is set, and the carry or borrow is shifted into T
		uint16_t const md = m_multiply_bug ? (m_md & ~1) : m_md;
		if (BIT(m_n, 0))
			t = alu(func, t, md, 0, true);
		else
			m_carry = 0;

		if (shift == 1)
		{
			m_n = (m_n >> 1) | (t << 15);
			m_t = (t >> 1) | (m_carry << 15);
		}
		else
		{
			m_t = (t << 1) | BIT(m_n, 15);
			m_n <<= 1;
		}
	}
	else
	{
		uint16_t result = alu(func, t, y, (ysel == 1) ? m_carry : 0, true);

		// right shifts take the carry of an arithmetic operation, otherwise they keep the sign
		bool const arith = func == 2 || func == 4 || func == 6;
		int const shift_in = arith ? m_carry : BIT(result, 15);

		if (tn)
		{
			if (sa)
				push_s();
			m_n = t;
		}
		else if (sa)
		{
			pop_s();
		}

		if (!dbl)
		{
			switch (shift)
			{
			case 1: result = (result >> 1) | (shift_in << 15); break; // 2/
			case 2: result <<= 1; break; // 2*
			case 3: result = BIT(result, 15) ? 0xffff : 0; break; // 0<
			}
		}
		else
		{
			switch (shift)
			{
			case 0: // designed to shift N left; unreliable on the prototype
				m_n <<= 1;
				break;
			case 1: // D2/
			{
				int const lsb = BIT(result, 0);
				result = (result >> 1) | (shift_in << 15);
				m_n = (m_n >> 1) | (lsb << 15);
				break;
			}
			case 2: // D2*
				result = (result << 1) | BIT(m_n, 15);
				m_n <<= 1;
				break;
			case 3: // designed to shift N right into T; unreliable on the prototype
				break;
			}
		}
		m_t = result;
	}

	if (BIT(op, 5))
		do_return();
}

// 1100: local memory fetch (m=0-2), internal register fetch (m=3, 7) and long literal (m=4-6)
int nc4000_device::fetch_op(uint16_t op)
{
	int const func = BIT(op, 9, 3);
	int const m = BIT(op, 6, 3);
	int const lit = BIT(op, 0, 5);
	int cycles = 1;
	uint16_t data = 0;
	bool push = false;
	int cin = 0;

	switch (m)
	{
	case 0: case 1: case 2:
		data = m_program.read_word(lit);
		push = m == 1;
		cin = (m == 2) ? m_carry : 0;
		cycles = 2;
		break;

	case 3: case 7:
		data = read_reg(lit);
		if (lit == 17)
			pop_r(); // R> and R> DROP
		push = m == 3;
		break;

	default:
		data = m_cache.read_word(m_pc++);
		push = m == 5;
		cin = (m == 6) ? m_carry : 0;
		cycles = 2;
		break;
	}

	if (push)
	{
		push_s();
		m_n = m_t;
	}
	m_t = alu(func, m_t, data, cin, true);

	if (BIT(op, 5))
		do_return();
	return cycles;
}

// 1101: local memory store (m=0-1), internal register store (m=2-3), short literal (m=4-6)
// and register exchange (m=7)
int nc4000_device::store_op(uint16_t op)
{
	int const func = BIT(op, 9, 3);
	int const m = BIT(op, 6, 3);
	int const lit = BIT(op, 0, 5);
	int cycles = 1;

	switch (m)
	{
	case 0: case 1:
		m_program.write_word(lit, m_t);
		m_t = alu(func, m_t, m_n, 0, true);
		pop_s();
		cycles = 2;
		break;

	case 2: case 3:
	{
		uint16_t const data = m_t;
		m_t = alu(func, m_t, m_n, (m == 2) ? m_carry : 0, true);
		if (m == 2)
			pop_s();

		if (lit == 1 || lit == 17)
		{
			// >R pushes the return stack; TIMES also latches the next instruction
			push_r(data);
			if (lit == 17)
			{
				m_times = true;
				m_times_first = true;
				m_times_last = false;
				m_times_op = m_cache.read_word(m_pc);
			}
		}
		else
		{
			write_reg(lit, data);
		}
		break;
	}

	case 4: case 5: case 6:
		if (m == 5)
		{
			push_s();
			m_n = m_t;
		}
		m_t = alu(func, m_t, lit, (m == 6) ? m_carry : 0, true);
		break;

	case 7:
	{
		int const reg = (lit == 17) ? 1 : lit;
		uint16_t const data = read_reg(reg);
		write_reg(reg, m_t);
		m_t = alu(func, m_t, data, 0, true);
		break;
	}
	}

	if (BIT(op, 5))
		do_return();
	return cycles;
}

// 1110: fetch from the address in T
int nc4000_device::memory_fetch_op(uint16_t op)
{
	int const func = BIT(op, 9, 3);
	int const m = BIT(op, 6, 3);
	int const lit = BIT(op, 0, 5);

	if (m == 4 || m == 5 || m == 6)
		logerror("%04x: X@ executed as @\n", m_ppc);

	uint16_t const data = m_program.read_word(m_t);
	switch (m)
	{
	case 7: // @+ @-: ( a - n a+lit )
		push_s();
		m_n = data;
		m_t = alu(func, m_t, lit, 0, true);
		break;

	case 1: case 3: case 5: // @: ( a - n )
		m_t = alu(func, m_t, data, 0, true);
		break;

	default: // @ op: ( n a - n op data )
		m_t = alu(func, m_n, data, BIT(m, 1) ? m_carry : 0, true);
		pop_s();
		break;
	}

	if (BIT(op, 5))
		do_return();
	return 2;
}

// 1111: store N to the address in T
int nc4000_device::memory_store_op(uint16_t op)
{
	int const func = BIT(op, 9, 3);
	int const m = BIT(op, 6, 3);
	int const lit = BIT(op, 0, 5);

	if (m == 4 || m == 5)
		logerror("%04x: X! executed as !\n", m_ppc);

	m_program.write_word(m_t, m_n);
	switch (m)
	{
	case 7: // !+ !-: ( n a - a+lit )
		m_t = alu(func, m_t, lit, 0, true);
		pop_s();
		break;

	case 3: case 5: // N!: ( n a - n )
		m_t = m_n;
		pop_s();
		break;

	default: // !: ( n a - )
		pop_s();
		m_t = m_n;
		pop_s();
		break;
	}

	if (BIT(op, 5))
		do_return();
	return 2;
}


//**************************************************************************
//  INTERNAL REGISTERS AND PORTS
//**************************************************************************

uint16_t nc4000_device::read_reg(int reg)
{
	switch (reg)
	{
	case 0: return (m_k << 8) | m_j;
	case 1: case 17: return m_i;
	case 2: return m_pc;
	case 3: return 0xffff;
	case 4: case 5: return m_md;
	case 6: case 7: return m_sr;
	case 8: return portb_r();
	case 9: return m_bmask;
	case 10: return m_bdir;
	case 11: return m_btri;
	case 12: return portx_r();
	case 13: return m_xmask;
	case 14: return m_xdir;
	case 15: return m_xtri;
	default: return read_reg(reg & 0x0f);
	}
}

void nc4000_device::write_reg(int reg, uint16_t data)
{
	switch (reg)
	{
	case 0: m_j = data & 0xff; m_k = data >> 8; break;
	case 1: case 17: m_i = data; break;
	case 2: m_pc = data; break;
	case 3: break;
	case 4: case 5: m_md = data; break;
	case 6: case 7: m_sr = data; break;
	case 8: portb_w(data); break;
	case 9: m_bmask = data; break;
	case 10: m_bdir = data; m_out_portb_cb(0, m_bdata, m_bdir); break;
	case 11: m_btri = data; break;
	case 12: portx_w(data); break;
	case 13: m_xmask = data & 0x1f; break;
	case 14: m_xdir = data & 0x1f; m_out_portx_cb(0, m_xdata, m_xdir); break;
	case 15: m_xtri = data & 0x11f; break;
	default: write_reg(reg & 0x0f, data); break;
	}
}

// input pins read through the XOR comparison latch; output pins read back the latch
uint16_t nc4000_device::portb_r()
{
	uint16_t const pins = m_in_portb_cb();
	return ((pins ^ m_bdata) & ~m_bdir) | (m_bdata & m_bdir);
}

uint8_t nc4000_device::portx_r()
{
	uint8_t const pins = m_in_portx_cb() & 0x1f;
	return ((pins ^ m_xdata) & ~m_xdir) | (m_xdata & m_xdir);
}

// bits set in the mask register are protected from writes
void nc4000_device::portb_w(uint16_t data)
{
	m_bdata = (m_bdata & m_bmask) | (data & ~m_bmask);
	m_out_portb_cb(0, m_bdata, m_bdir);
}

void nc4000_device::portx_w(uint8_t data)
{
	m_xdata = ((m_xdata & m_xmask) | (data & ~m_xmask)) & 0x1f;
	m_out_portx_cb(0, m_xdata, m_xdir);
}
