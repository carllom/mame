// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Texas Instruments TMS320C674x fixed- and floating-point VLIW DSP core

    The C674x core joins the C64x+ fixed-point and C67x+ floating-point
    instruction sets: eight functional units (.L1 .S1 .M1 .D1 / .L2 .S2
    .M2 .D2) on two 32-register files, 256-bit fetch packets that may hold
    16-bit compact instructions (header-based fetch packets), delay slots
    and the SPLOOP software-pipelined loop buffer.

    References:
    - SPRUFE8B: TMS320C674x DSP CPU and Instruction Set Reference Guide
    - SPRUFK5: TMS320C674x DSP Megamodule Reference Guide

    This is a skeleton: it has the register state and a unified 32-bit
    byte-addressed little-endian address space, but executes nothing.

    TODO:
    - instruction decode and execution (C64x+ and C67x+ sets, compact
      instructions, execute packets and the pipeline with delay slots)
    - SPLOOP buffer
    - interrupts (NMI, INT4-INT15), exceptions and the TSR/ITSR/NTSR
      task state machinery
    - supervisor and user modes
    - TSCL/TSCH time-stamp counter
    - L1P/L1D/L2 cache behaviour (the megamodule is in the SoC device)

***************************************************************************/

#include "emu.h"
#include "tms320c674x.h"
#include "tms320c6x_dasm.h"


tms320c674x_device::tms320c674x_device(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock, address_map_constructor internal_map, u32 istp_reset, u8 revision) :
	cpu_device(mconfig, type, tag, owner, clock),
	m_program_config("program", ENDIANNESS_LITTLE, 32, 32, 0, internal_map),
	m_istp_reset(istp_reset),
	m_revision(revision),
	m_pc(0),
	m_a{},
	m_b{},
	m_amr(0),
	m_csr(0),
	m_ifr(0),
	m_ier(0),
	m_istp(0),
	m_irp(0),
	m_nrp(0),
	m_tsr(0),
	m_itsr(0),
	m_ntsr(0),
	m_efr(0),
	m_ierr(0),
	m_ilc(0),
	m_rilc(0),
	m_rep(0),
	m_ssr(0),
	m_gfpgfr(0),
	m_gplya(0),
	m_gplyb(0),
	m_fadcr(0),
	m_faucr(0),
	m_fmcr(0),
	m_icount(0)
{
}

device_memory_interface::space_config_vector tms320c674x_device::memory_space_config() const
{
	return space_config_vector{ std::make_pair(AS_PROGRAM, &m_program_config) };
}

std::unique_ptr<util::disasm_interface> tms320c674x_device::create_disassembler()
{
	return std::make_unique<tms320c6x_disassembler>();
}

void tms320c674x_device::device_start()
{
	space(AS_PROGRAM).cache(m_cache);
	space(AS_PROGRAM).specific(m_program);
	set_icountptr(m_icount);

	state_add(STATE_GENPC, "GENPC", m_pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_pc).noshow();
	state_add(C6X_PC, "PCE1", m_pc);
	for (unsigned i = 0; i < 32; i++)
		state_add(C6X_A0 + i, util::string_format("A%u", i).c_str(), m_a[i]);
	for (unsigned i = 0; i < 32; i++)
		state_add(C6X_B0 + i, util::string_format("B%u", i).c_str(), m_b[i]);
	state_add(C6X_AMR, "AMR", m_amr);
	state_add(C6X_CSR, "CSR", m_csr);
	state_add(C6X_IFR, "IFR", m_ifr);
	state_add(C6X_IER, "IER", m_ier);
	state_add(C6X_ISTP, "ISTP", m_istp);
	state_add(C6X_IRP, "IRP", m_irp);
	state_add(C6X_NRP, "NRP", m_nrp);
	state_add(C6X_TSR, "TSR", m_tsr);
	state_add(C6X_ITSR, "ITSR", m_itsr);
	state_add(C6X_NTSR, "NTSR", m_ntsr);
	state_add(C6X_EFR, "EFR", m_efr);
	state_add(C6X_IERR, "IERR", m_ierr);
	state_add(C6X_ILC, "ILC", m_ilc);
	state_add(C6X_RILC, "RILC", m_rilc);
	state_add(C6X_REP, "REP", m_rep);
	state_add(C6X_SSR, "SSR", m_ssr);
	state_add(C6X_GFPGFR, "GFPGFR", m_gfpgfr);
	state_add(C6X_GPLYA, "GPLYA", m_gplya);
	state_add(C6X_GPLYB, "GPLYB", m_gplyb);
	state_add(C6X_FADCR, "FADCR", m_fadcr);
	state_add(C6X_FAUCR, "FAUCR", m_faucr);
	state_add(C6X_FMCR, "FMCR", m_fmcr);

	save_item(NAME(m_pc));
	save_item(NAME(m_a));
	save_item(NAME(m_b));
	save_item(NAME(m_amr));
	save_item(NAME(m_csr));
	save_item(NAME(m_ifr));
	save_item(NAME(m_ier));
	save_item(NAME(m_istp));
	save_item(NAME(m_irp));
	save_item(NAME(m_nrp));
	save_item(NAME(m_tsr));
	save_item(NAME(m_itsr));
	save_item(NAME(m_ntsr));
	save_item(NAME(m_efr));
	save_item(NAME(m_ierr));
	save_item(NAME(m_ilc));
	save_item(NAME(m_rilc));
	save_item(NAME(m_rep));
	save_item(NAME(m_ssr));
	save_item(NAME(m_gfpgfr));
	save_item(NAME(m_gplya));
	save_item(NAME(m_gplyb));
	save_item(NAME(m_fadcr));
	save_item(NAME(m_faucr));
	save_item(NAME(m_fmcr));
}

void tms320c674x_device::device_reset()
{
	// SPRUFE8B 5.3.4.1: CPU state after reset
	m_amr = 0;
	m_ifr = 0;
	m_ier = 1;
	m_istp = m_istp_reset;
	m_csr = (u32(0x14) << 24) | (u32(m_revision) << 16) | 0x0100; // C674x CPU ID, little-endian
	m_tsr = 0;
	m_itsr = 0;
	m_ntsr = 0;

	// execution begins at the ISTB field of ISTP
	m_pc = m_istp & 0xfffffc00;
}

void tms320c674x_device::execute_run()
{
	// TODO: fetch packets, execute packets and the pipeline
	debugger_instruction_hook(m_pc);
	m_icount = 0;
}
