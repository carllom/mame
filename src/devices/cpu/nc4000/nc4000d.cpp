// license:BSD-3-Clause
// copyright-holders:Carl Lom
/*****************************************************************************

    Novix NC4000 / NC4016 disassembler

    Instructions are shown as the cmFORTH phrase they execute, following
    Tables 3.5 and 3.6 in "Footsteps in an Empty Valley". Combinations that
    the book lists as invalid are still decoded field by field.

    Numbers are hex; internal registers are shown as "n I@" with the
    register name in parentheses.

*****************************************************************************/

#include "emu.h"
#include "nc4000d.h"

namespace {

// ALU result T op Y, named as the Forth word that applies it to ( Y T )
constexpr char const *BINARY_NAMES[8] = { "", "AND", "SWAP-", "OR", "+", "XOR", "-", "" };

// ALU result T op Y, named as the Forth word that applies it to ( T Y ),
// used when Y is fetched or a literal ("a-" in Table 3.6)
constexpr char const *FETCH_NAMES[8] = { "DROP", "AND", "-", "OR", "+", "XOR", "SWAP-", "NIP" };

constexpr char const *REG_NAMES[16] = {
	"JK", "I", "P", "TRUE", "MD", "MD", "SR", "SR",
	"B", "BMASK", "BDIR", "BTRI", "X", "XMASK", "XDIR", "XTRI" };

std::string with_carry(char const *name, bool carry)
{
	return carry ? std::string(name) + "c" : std::string(name);
}

} // anonymous namespace

std::string nc4000_disassembler::reg_name(int reg)
{
	if (reg == 17)
		return "17 (#TIMES)";
	return util::string_format("%d (%s)", reg, REG_NAMES[reg & 0x0f]);
}

std::string nc4000_disassembler::alu_op(u16 op)
{
	int const func = BIT(op, 9, 3);
	int const ysel = BIT(op, 7, 2);
	int const stack = (BIT(op, 6) << 1) | BIT(op, 4); // TN, SA
	int const shift = BIT(op, 0, 4); // D?, %, SL, SR

	// step instructions (book's octal opcodes in the comments)
	switch (op & ~0x0020)
	{
	case 0x8909: return "*'"; // 104411
	case 0x8509: return "*-"; // 102411
	case 0x850a: return "*F"; // 102412
	case 0x850e: return "/'"; // 102416
	case 0x850c: return "/\""; // 102414
	case 0x858e: return "S'"; // 102616
	case 0x8504: return "MOD'"; // 102404
	}

	std::string text;
	if (ysel >= 2)
	{
		// MD or SR as the Y operand
		static constexpr char const *PREFIX[4] = { "", "NIP ", "NIP DUP ", "DUP " };
		char const *const reg = (ysel == 2) ? "4 I@" : "6 I@";
		text = PREFIX[stack];
		if (func == 7)
			text = text + "DROP " + reg;
		else if (func != 0)
			text = text + reg + ' ' + FETCH_NAMES[func];
	}
	else if (func == 0)
	{
		static constexpr char const *NAMES[4] = { "", "NIP", "NIP DUP", "DUP" };
		text = NAMES[stack];
	}
	else if (func == 7)
	{
		static constexpr char const *NAMES[4] = { "DROP DUP", "DROP", "SWAP", "OVER" };
		text = NAMES[stack];
	}
	else
	{
		static constexpr char const *PREFIX[4] = { "OVER SWAP ", "", "SWAP OVER ", "2DUP " };
		text = PREFIX[stack] + with_carry(BINARY_NAMES[func], ysel == 1);
	}

	static constexpr char const *SHIFTS[16] = {
			"", "2/", "2*", "0<", "%", "% 2/", "% 2*", "% 0<",
			"N2*", "D2/", "D2*", "D?", "% N2*", "% D2/", "% D2*", "% D?" };
	if (shift)
		text += text.empty() ? SHIFTS[shift] : std::string(" ") + SHIFTS[shift];
	if (text.empty())
		text = "NOP";
	return text;
}

offs_t nc4000_disassembler::disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params)
{
	u16 const op = opcodes.r16(pc);
	offs_t length = 1;
	offs_t flags = SUPPORTED;

	if (!BIT(op, 15))
	{
		util::stream_format(stream, "CALL $%04X", op);
		return length | flags | STEP_OVER;
	}

	int const cls = BIT(op, 12, 3);
	if (cls >= 1 && cls <= 3)
	{
		// branches stay within the 4K page of the following instruction
		static constexpr char const *BRANCH_NAMES[4] = { "", "IF", "NEXT", "ELSE" };
		util::stream_format(stream, "%s $%04X", BRANCH_NAMES[cls], ((pc + 1) & 0xf000) | (op & 0x0fff));
		if (cls != 3)
			flags |= STEP_COND;
		return length | flags;
	}

	int const func = BIT(op, 9, 3);
	int const m = BIT(op, 6, 3);
	int const lit = BIT(op, 0, 5);
	bool const carry = BIT(m, 1);
	std::string text;

	switch (cls)
	{
	case 0: // ALU
		text = alu_op(op);
		break;

	case 4: // local memory, register or long literal fetch
		switch (m)
		{
		case 0: case 2:
			text = util::string_format("$%02X @ %s", lit, with_carry(FETCH_NAMES[func], carry));
			if (op == 0xc000) // 140000 octal
				text = "TWO";
			break;
		case 1:
			text = util::string_format("$%02X @", lit);
			if (func != 7)
				text = "DUP " + text + " " + FETCH_NAMES[func];
			break;
		case 3:
			if (lit == 17 && func == 7)
				text = "R>";
			else if (lit == 1 && func == 7)
				text = "R@";
			else if (lit == 3 && func == 7)
				text = "-1";
			else
			{
				text = util::string_format("%s I@", reg_name(lit));
				if (func != 7)
					text = "DUP " + text + " " + FETCH_NAMES[func];
			}
			break;
		case 7:
			if (lit == 17 && func == 0)
				text = "R> DROP";
			else
				text = util::string_format("%s I@ %s", reg_name(lit), FETCH_NAMES[func]);
			break;
		default:
		{
			u16 const data = opcodes.r16(pc + 1);
			length = 2;
			if (m == 5)
			{
				text = util::string_format("$%04X", data);
				if (func != 7)
					text = "DUP " + text + " " + FETCH_NAMES[func];
			}
			else
			{
				text = util::string_format("$%04X %s", data, with_carry(FETCH_NAMES[func], carry));
			}
			break;
		}
		}
		break;

	case 5: // local memory or register store, short literal, register exchange
		switch (m)
		{
		case 0: case 1:
			text = util::string_format("$%02X !", lit);
			if (func != 7)
				text = "DUP " + text + " " + BINARY_NAMES[func];
			break;
		case 2:
			if (lit == 1 && func == 7)
				text = ">R";
			else if (lit == 17 && func == 7)
				text = "TIMES";
			else
			{
				text = util::string_format("%s I!", reg_name(lit));
				if (func != 7)
					text = "DUP " + text + " " + with_carry(BINARY_NAMES[func], true);
			}
			break;
		case 3:
			text = (lit == 1) ? std::string("DUP >R") : util::string_format("DUP %s I!", reg_name(lit));
			if (func != 0)
				text = text + ' ' + BINARY_NAMES[func];
			break;
		case 4: case 6:
			text = util::string_format("$%02X %s", lit, with_carry(FETCH_NAMES[func], m == 6));
			break;
		case 5:
			text = util::string_format("$%02X", lit);
			if (func != 7)
				text = "DUP " + text + " " + FETCH_NAMES[func];
			break;
		case 7:
			text = (lit == 1 && func == 7) ? std::string("R> SWAP >R") : util::string_format("%s I@!", reg_name(lit));
			if (func != 7)
				text = text + ' ' + FETCH_NAMES[func];
			break;
		}
		break;

	case 6: // fetch from the address in T
		switch (m)
		{
		case 7:
			if (func == 4)
				text = util::string_format("$%02X @+", lit);
			else if (func == 2)
				text = util::string_format("$%02X @-", lit);
			else
				text = util::string_format("DUP @ SWAP $%02X %s", lit, FETCH_NAMES[func]);
			break;
		case 1: case 3:
			text = (func == 7) ? std::string("@") : util::string_format("@ (%s)", FETCH_NAMES[func]);
			break;
		case 5:
			text = util::string_format("$%02X X@", lit);
			break;
		case 4: case 6:
			text = util::string_format("$%02X X@ %s", lit, with_carry(FETCH_NAMES[func], carry));
			break;
		default:
			text = util::string_format("@ %s", with_carry(FETCH_NAMES[func], carry));
			break;
		}
		break;

	case 7: // store to the address in T
		switch (m)
		{
		case 7:
			if (func == 4)
				text = util::string_format("$%02X !+", lit);
			else if (func == 2)
				text = util::string_format("$%02X !-", lit);
			else
				text = util::string_format("SWAP OVER ! $%02X %s", lit, FETCH_NAMES[func]);
			break;
		case 3:
			text = "N!";
			break;
		case 4:
			text = util::string_format("$%02X X!", lit);
			break;
		case 5:
			text = util::string_format("DUP $%02X X!", lit);
			break;
		default:
			text = "!";
			break;
		}
		break;
	}

	if (BIT(op, 5))
	{
		text = (text == "NOP") ? std::string(";") : text + " ;";
		flags |= STEP_OUT;
	}
	stream << text;
	return length | flags;
}
