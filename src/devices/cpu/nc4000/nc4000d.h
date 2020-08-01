#ifndef MAME_CPU_NC4000_NC4000D_H
#define MAME_CPU_NC4000_NC4000D_H

#pragma once

class nc4000_disassembler : public util::disasm_interface
{
public:
	nc4000_disassembler() = default;
	virtual ~nc4000_disassembler() = default;

	virtual u32 opcode_alignment() const override;
	virtual offs_t disassemble(std::ostream &stream, offs_t pc, const data_buffer &opcodes, const data_buffer &params) override;
};

#endif
