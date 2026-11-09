// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C6X_DASM_H
#define MAME_CPU_TMS320C6X_TMS320C6X_DASM_H

#pragma once

class tms320c6x_disassembler : public util::disasm_interface
{
public:
	tms320c6x_disassembler() = default;

	virtual u32 opcode_alignment() const override;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, data_buffer const &opcodes, data_buffer const &params) override;
};

#endif // MAME_CPU_TMS320C6X_TMS320C6X_DASM_H
