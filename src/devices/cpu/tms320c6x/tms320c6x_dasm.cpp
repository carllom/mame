// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Texas Instruments TMS320C6000 disassembler

    Skeleton: shows each 32-bit word as data.

    TODO:
    - C64x+ and C67x+ instruction sets
    - header-based fetch packets with 16-bit compact instructions (the
      header is the last word of the 32-byte aligned fetch packet)
    - parallel (||) execute packet marking

***************************************************************************/

#include "emu.h"
#include "tms320c6x_dasm.h"


u32 tms320c6x_disassembler::opcode_alignment() const
{
	return 4;
}

offs_t tms320c6x_disassembler::disassemble(std::ostream &stream, offs_t pc, data_buffer const &opcodes, data_buffer const &params)
{
	util::stream_format(stream, ".word 0x%08x", opcodes.r32(pc));
	return 4 | SUPPORTED;
}
