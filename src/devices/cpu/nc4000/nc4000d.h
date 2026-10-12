// license:BSD-3-Clause
// copyright-holders:Carl Lom
/*****************************************************************************

    Novix NC4000 / NC4016 disassembler

*****************************************************************************/

#ifndef MAME_CPU_NC4000_NC4000D_H
#define MAME_CPU_NC4000_NC4000D_H

#pragma once

class nc4000_disassembler : public util::disasm_interface
{
public:
	nc4000_disassembler() = default;
	virtual ~nc4000_disassembler() = default;

	virtual u32 opcode_alignment() const override { return 1; }
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override;

private:
	static std::string alu_op(u16 op);
	static std::string reg_name(int reg);
};

#endif // MAME_CPU_NC4000_NC4000D_H
