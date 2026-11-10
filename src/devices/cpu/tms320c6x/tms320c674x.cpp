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

    The core steps one cycle at a time. Each cycle runs one execute packet
    from program memory (or a cycle of a multi-cycle NOP) together with any
    loop buffer instructions due in that cycle. Results are queued and
    written after the instruction's delay slots, so code that relies on
    reading old values in delay slots behaves as on hardware. Branches take
    effect after their five delay slots.

    TODO:
    - instructions not used by the firmware seen so far (logged when hit)
    - floating-point rounding modes, FADCR/FAUCR/FMCR flags
    - circular addressing for the ADDA and SUBA instructions
    - SPLOOP reload with RILC = 0 (a skipped invocation)
    - exceptions, supervisor and user modes
    - pipeline stalls and memory wait states

***************************************************************************/

#include "emu.h"
#include "tms320c674x.h"

#include <algorithm>
#include <bit>
#include <cmath>

#define LOG_SPLOOP (1U << 1)
#define LOG_INT    (1U << 2)

#define VERBOSE (0)
#include "logmacro.h"


tms320c674x_device::tms320c674x_device(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock, address_map_constructor internal_map, u32 istp_reset, u8 revision) :
	cpu_device(mconfig, type, tag, owner, clock),
	m_program_config("program", ENDIANNESS_LITTLE, 32, 32, 0, internal_map),
	m_istp_reset(istp_reset),
	m_revision(revision),
	m_pc(0),
	m_r{},
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
	m_tsc_start(0),
	m_tsch(0),
	m_tsc_running(false),
	m_fadcr(0),
	m_faucr(0),
	m_fmcr(0),
	m_cycle(0),
	m_wb_reg{},
	m_wb_value{},
	m_wb_count{},
	m_branch_when{},
	m_branch_target{},
	m_branches(0),
	m_nop_cycles(0),
	m_idle(false),
	m_int_lines(0),
	m_store_count(0),
	m_ep_nops(0),
	m_ep_sploop(false),
	m_lb{},
	m_lb_count(0),
	m_lb_active(false),
	m_lb_loading(false),
	m_lb_draining(false),
	m_lb_fetch(true),
	m_lb_kind(0),
	m_lb_ii(1),
	m_lb_lbc(0),
	m_lb_loadcnt(0),
	m_lb_dynlen(0),
	m_lb_drain(0),
	m_lb_delay(0),
	m_lb_stgcyc(0),
	m_lb_cycles(0),
	m_lb_kernel(false),
	m_lb_initial_term(false),
	m_lb_creg(0),
	m_lb_z(false),
	m_lb_cond_history(0),
	m_lb_address(0),
	m_lb_int_drain(false),
	m_lb_restart(false),
	m_lb_kernelr(false),
	m_lb_reload_pending(false),
	m_lb_reload_start(false),
	m_lb_reload_branch(false),
	m_lb_reloading(false),
	m_lb_reloadcnt(0),
	m_lb_old_active(false),
	m_lb_old_lbc(0),
	m_lb_old_drain(0),
	m_icount(0)
{
}

device_memory_interface::space_config_vector tms320c674x_device::memory_space_config() const
{
	return space_config_vector{ std::make_pair(AS_PROGRAM, &m_program_config) };
}

u32 tms320c674x_device::execute_min_cycles() const noexcept
{
	return 1;
}

u32 tms320c674x_device::execute_max_cycles() const noexcept
{
	return 1;
}

bool tms320c674x_device::execute_input_edge_triggered(int inputnum) const noexcept
{
	return true;
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

	m_ep_cache = std::make_unique<ep_cache_entry []>(EP_CACHE_SIZE);
	for (u32 n = 0; n < EP_CACHE_SIZE; n++)
		m_ep_cache[n].count = 0;

	state_add(STATE_GENPC, "GENPC", m_pc).noshow();
	state_add(STATE_GENPCBASE, "CURPC", m_pc).noshow();
	state_add(C6X_PC, "PC", m_pc);
	for (u32 i = 0; i < 32; i++)
		state_add(C6X_A0 + i, util::string_format("A%u", i).c_str(), m_r[i]);
	for (u32 i = 0; i < 32; i++)
		state_add(C6X_B0 + i, util::string_format("B%u", i).c_str(), m_r[32 + i]);
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
	save_item(NAME(m_r));
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
	save_item(NAME(m_tsc_start));
	save_item(NAME(m_tsch));
	save_item(NAME(m_tsc_running));
	save_item(NAME(m_fadcr));
	save_item(NAME(m_faucr));
	save_item(NAME(m_fmcr));
	save_item(NAME(m_cycle));
	save_item(NAME(m_wb_reg));
	save_item(NAME(m_wb_value));
	save_item(NAME(m_wb_count));
	save_item(NAME(m_branch_when));
	save_item(NAME(m_branch_target));
	save_item(NAME(m_branches));
	save_item(NAME(m_nop_cycles));
	save_item(NAME(m_idle));
	save_item(NAME(m_int_lines));
	save_item(STRUCT_MEMBER(m_lb, opcode));
	save_item(STRUCT_MEMBER(m_lb, header));
	save_item(STRUCT_MEMBER(m_lb, pce1));
	save_item(STRUCT_MEMBER(m_lb, size));
	save_item(STRUCT_MEMBER(m_lb, slot));
	save_item(STRUCT_MEMBER(m_lb, loadcnt));
	save_item(STRUCT_MEMBER(m_lb, valid));
	save_item(STRUCT_MEMBER(m_lb, old_valid));
	save_item(NAME(m_lb_count));
	save_item(NAME(m_lb_active));
	save_item(NAME(m_lb_loading));
	save_item(NAME(m_lb_draining));
	save_item(NAME(m_lb_fetch));
	save_item(NAME(m_lb_kind));
	save_item(NAME(m_lb_ii));
	save_item(NAME(m_lb_lbc));
	save_item(NAME(m_lb_loadcnt));
	save_item(NAME(m_lb_dynlen));
	save_item(NAME(m_lb_drain));
	save_item(NAME(m_lb_delay));
	save_item(NAME(m_lb_stgcyc));
	save_item(NAME(m_lb_cycles));
	save_item(NAME(m_lb_kernel));
	save_item(NAME(m_lb_initial_term));
	save_item(NAME(m_lb_creg));
	save_item(NAME(m_lb_z));
	save_item(NAME(m_lb_cond_history));
	save_item(NAME(m_lb_address));
	save_item(NAME(m_lb_int_drain));
	save_item(NAME(m_lb_restart));
	save_item(NAME(m_lb_kernelr));
	save_item(NAME(m_lb_reload_pending));
	save_item(NAME(m_lb_reload_start));
	save_item(NAME(m_lb_reload_branch));
	save_item(NAME(m_lb_reloading));
	save_item(NAME(m_lb_reloadcnt));
	save_item(NAME(m_lb_old_active));
	save_item(NAME(m_lb_old_lbc));
	save_item(NAME(m_lb_old_drain));
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
	m_tsc_running = false;

	std::fill(std::begin(m_wb_count), std::end(m_wb_count), 0);
	m_branches = 0;
	m_nop_cycles = 0;
	m_idle = false;
	sploop_stop();

	// execution begins at the ISTB field of ISTP
	m_pc = m_istp & 0xffff'fc00;
}

void tms320c674x_device::device_post_load()
{
	for (u32 n = 0; n < m_lb_count; n++)
	{
		if (m_lb[n].size == 2)
			dasm::decode16(m_lb[n].opcode, m_lb[n].header, m_lb[n].pce1, m_lb_insn[n]);
		else
			dasm::decode32(m_lb[n].opcode, m_lb[n].pce1, dasm::is_header(m_lb[n].header), m_lb_insn[n]);
	}
}


//**************************************************************************
//  INTERRUPTS
//**************************************************************************

void tms320c674x_device::execute_set_input(int inputnum, int state)
{
	int const bit = (inputnum == INPUT_LINE_NMI) ? 1 : inputnum;
	if ((bit < 1) || (bit > 15) || (bit == 2) || (bit == 3))
		return;

	if (state && !BIT(m_int_lines, bit))
		m_ifr |= 1U << bit;
	if (state)
		m_int_lines |= 1 << bit;
	else
		m_int_lines &= ~(1 << bit);
}

void tms320c674x_device::interrupt_pulse(u32 line)
{
	if ((line >= 4) && (line <= 15))
		m_ifr |= 1U << line;
}

int tms320c674x_device::pending_interrupt() const
{
	// not in branch delay slots
	if (m_branches || !BIT(m_ier, 1))
		return -1;

	if (BIT(m_ifr, 1))
		return 1;

	u32 const pending = m_ifr & m_ier & 0xfff0;
	if (pending && BIT(m_csr, 0))
		return std::countr_zero(pending);
	return -1;
}

void tms320c674x_device::check_interrupts()
{
	// the loop buffer takes interrupts at stage boundaries (sploop_end_cycle)
	if (m_lb_active)
		return;

	int const number = pending_interrupt();
	if (number >= 0)
		take_interrupt(number, m_pc, false);
}

void tms320c674x_device::take_interrupt(int number, u32 return_pc, bool splx)
{
	LOGMASKED(LOG_INT, "interrupt %d at %08x\n", number, return_pc);

	// in-flight instructions complete before the service routine starts
	flush_writes();
	m_idle = false;
	m_nop_cycles = 0;
	m_ifr &= ~(1U << number);

	// an interrupted SPLOOP returns to its execute packet with SPLX set
	u32 const tsr = (m_tsr & ~(1U | (1U << 14))) | BIT(m_csr, 0) | (splx ? (1U << 14) : 0);
	if (number == 1)
	{
		m_nrp = return_pc;
		m_ntsr = tsr;
		m_ier &= ~2U; // NMIE
	}
	else
	{
		m_irp = return_pc;
		m_itsr = tsr;
		m_csr = (m_csr & ~2U) | (BIT(m_csr, 0) << 1); // PGIE = GIE
	}
	m_csr &= ~1U; // GIE
	m_tsr |= 1U << 9; // INT

	standard_irq_callback(number, return_pc);
	m_pc = (m_istp & 0xffff'fc00) | (number << 5);
}


//**************************************************************************
//  PIPELINE
//**************************************************************************

void tms320c674x_device::write(u32 r, u32 value, u32 delay)
{
	u32 const slot = (m_cycle + delay + 1) % WB_SLOTS;
	u32 const n = m_wb_count[slot];
	if (n >= WB_DEPTH)
	{
		logerror("write queue overflow at %08x\n", m_pc);
		return;
	}
	m_wb_reg[slot * WB_DEPTH + n] = r;
	m_wb_value[slot * WB_DEPTH + n] = value;
	m_wb_count[slot] = n + 1;
}

void tms320c674x_device::write_pair(u32 r, u64 value, u32 delay)
{
	write(r & ~1U, u32(value), delay);
	write(r | 1U, u32(value >> 32), delay);
}

void tms320c674x_device::flush_writes()
{
	for (u32 d = 1; d <= WB_SLOTS; d++)
	{
		u32 const slot = (m_cycle + d) % WB_SLOTS;
		for (u32 n = 0; n < m_wb_count[slot]; n++)
		{
			u8 const r = m_wb_reg[slot * WB_DEPTH + n];
			u32 const v = m_wb_value[slot * WB_DEPTH + n];
			if (r < 64)
				m_r[r] = v;
			else if (r == REG_ILC)
				m_ilc = v;
			else if (r == REG_RILC)
				m_rilc = v;
		}
		m_wb_count[slot] = 0;
	}
}

void tms320c674x_device::branch(u32 target)
{
	if (m_branches >= BRANCH_SLOTS)
	{
		logerror("too many branches in flight at %08x\n", m_pc);
		return;
	}
	m_branch_when[m_branches] = m_cycle + 6;
	m_branch_target[m_branches] = target;
	m_branches++;
}

void tms320c674x_device::tick()
{
	m_cycle++;
	m_icount--;

	u32 const slot = m_cycle % WB_SLOTS;
	for (u32 n = 0; n < m_wb_count[slot]; n++)
	{
		u8 const r = m_wb_reg[slot * WB_DEPTH + n];
		u32 const v = m_wb_value[slot * WB_DEPTH + n];
		if (r < 64)
			m_r[r] = v;
		else if (r == REG_ILC)
			m_ilc = v;
		else if (r == REG_RILC)
			m_rilc = v;
	}
	m_wb_count[slot] = 0;

	for (u32 n = 0; n < m_branches; )
	{
		if (m_branch_when[n] == m_cycle)
		{
			m_pc = m_branch_target[n];
			m_nop_cycles = 0;
			m_idle = false;
			if (m_lb_active)
			{
				if (m_lb_reload_branch)
				{
					// SPRUFE8B 7.9.6.3: the outer loop branch disables fetch, the PC stays at its target
					m_lb_reload_branch = false;
					m_lb_fetch = false;
				}
				else
					sploop_stop();
			}
			m_branches--;
			m_branch_when[n] = m_branch_when[m_branches];
			m_branch_target[n] = m_branch_target[m_branches];
		}
		else
			n++;
	}
}

bool tms320c674x_device::condition(insn const &i) const
{
	if (!i.creg)
		return true;
	bool const zero = !m_r[i.creg - 1];
	return i.z ? zero : !zero;
}

u32 tms320c674x_device::decode_ep(u32 pc, ep_cache_entry &e)
{
	u32 count = 0;
	e.pc = pc;
	for (;;)
	{
		u32 const fp = pc & ~u32(31);
		u32 const header = m_cache.read_dword(fp + 28);
		bool const header_fp = dasm::is_header(header);
		bool p;

		if (header_fp && ((pc & 31) == 28))
		{
			pc = fp + 32;
			continue;
		}

		insn &i = e.ep[count];
		if (header_fp && BIT(header, 21 + ((pc & 31) >> 2)))
		{
			pc &= ~u32(1);
			u16 const op = m_cache.read_word(pc);
			dasm::decode16(op, header, fp, i);
			p = BIT(header, ((pc & 31) >> 1));
			e.addr[count] = pc;
			e.raw[count] = op;
			pc += 2;
		}
		else
		{
			pc &= ~u32(3);
			u32 const op = m_cache.read_dword(pc);
			dasm::decode32(op, fp, header_fp, i);
			if (header_fp && BIT(header, 20))
			{
				switch (i.op)
				{
				case dasm::OP_LDB: case dasm::OP_LDBU: case dasm::OP_LDH: case dasm::OP_LDHU:
				case dasm::OP_LDW: case dasm::OP_LDNW: case dasm::OP_LDDW: case dasm::OP_LDNDW:
					i.flags |= dasm::F_PROT;
					break;
				default:
					break;
				}
			}
			p = BIT(op, 0);
			e.addr[count] = pc;
			e.raw[count] = op;
			pc += 4;
		}
		e.hdr[count] = header_fp ? header : 0;
		e.pce1[count] = fp;
		count++;

		if (!p || (count >= MAX_EP))
			break;
	}

	// skip a header word at the end of the packet
	if (((pc & 31) == 28) && dasm::is_header(m_cache.read_dword(pc)))
		pc += 4;

	e.next = pc;
	e.count = count;
	return count;
}

tms320c674x_device::ep_cache_entry const &tms320c674x_device::fetch_ep(u32 pc)
{
	ep_cache_entry &e = m_ep_cache[(pc >> 1) & (EP_CACHE_SIZE - 1)];
	if (e.count && (e.pc == pc))
	{
		// code may have been rewritten since it was decoded
		bool valid = true;
		for (u32 n = 0; valid && (n < e.count); n++)
		{
			u32 const op = (e.ep[n].size == 2) ? m_cache.read_word(e.addr[n]) : m_cache.read_dword(e.addr[n]);
			u32 const header = m_cache.read_dword(e.pce1[n] + 28);
			valid = (op == e.raw[n]) && ((dasm::is_header(header) ? header : 0) == e.hdr[n]);
		}
		if (valid)
			return e;
	}
	decode_ep(pc, e);
	return e;
}

void tms320c674x_device::step()
{
	static constexpr ep_cache_entry NONE{};
	ep_cache_entry const *e = &NONE;
	u32 next = m_pc;

	m_store_count = 0;
	m_ep_nops = 0;
	m_ep_sploop = false;

	if (m_idle)
	{
		check_interrupts();
		if (m_idle)
		{
			m_icount -= std::max(m_icount, 0);
			tick();
			return;
		}
	}

	// reload begins the cycle after SPMASKR or the last kernel stage boundary (SPKERNELR)
	if (m_lb_reload_start)
		sploop_reload();

	bool fetched = false;
	if (m_nop_cycles)
		m_nop_cycles--;
	else if (!m_lb_active || m_lb_fetch)
	{
		check_interrupts();
		debugger_instruction_hook(m_pc);
		e = &fetch_ep(m_pc);
		next = e->next;
		fetched = true;
	}
	u32 const count = e->count;
	insn const *const ep = e->ep;
	u32 const *const pce1 = e->pce1;

	// units masked by SPMASK
	u32 mask = 0;
	bool kernel = false;
	bool restart = false;
	for (u32 n = 0; n < count; n++)
	{
		if ((ep[n].op == dasm::OP_SPLOOP) || (ep[n].op == dasm::OP_SPLOOPD) || (ep[n].op == dasm::OP_SPLOOPW))
		{
			// returning to an interrupted SPLOOP
			restart = !m_lb_active && BIT(m_tsr, 14);
		}
		else if ((ep[n].op == dasm::OP_SPMASK) || (ep[n].op == dasm::OP_SPMASKR))
		{
			mask |= ep[n].cst;
			if ((ep[n].op == dasm::OP_SPMASKR) && m_lb_active && m_lb_reload_pending && !m_lb_kernelr)
				m_lb_reload_start = true;
		}
		else if (ep[n].op == dasm::OP_SPKERNEL)
		{
			kernel = true;
			m_lb_stgcyc = ep[n].cst;
		}
		else if (ep[n].op == dasm::OP_SPKERNELR)
		{
			// post-loop code and reload both start with the epilog
			kernel = true;
			m_lb_stgcyc = 0;
			if (m_lb_active && m_lb_loading)
				m_lb_kernelr = true;
		}
	}

	// loop buffer instructions due this cycle; piping up after an interrupt ignores SPMASK
	u32 lb_exec[LB_SIZE];
	u32 lb_count = 0;
	bool const lb_was_active = m_lb_active;
	bool const loading = m_lb_active && m_lb_loading;
	bool const resuming = loading && m_lb_restart;
	if (m_lb_active)
	{
		u32 const lb_mask = resuming ? 0 : mask;
		for (u32 n = 0; n < m_lb_count; n++)
		{
			lb_entry &lb = m_lb[n];
			bool run = false;

			// the previous invocation drains on its own LBC while this one reloads
			if (m_lb_old_active && (lb.slot == m_lb_old_lbc))
			{
				if (lb.loadcnt == m_lb_old_drain)
					lb.old_valid = false;
				run = lb.old_valid;
			}

			// draining disables and reloading enables instructions in the order they were loaded
			if (lb.slot == m_lb_lbc)
			{
				if (m_lb_draining && (lb.loadcnt == m_lb_drain))
					lb.valid = false;
				if (m_lb_reloading && !m_lb_initial_term && (lb.loadcnt == m_lb_reloadcnt))
					lb.valid = true;
				run = run || lb.valid;
			}

			if (run && !(unit_mask(m_lb_insn[n].unit) & lb_mask))
				lb_exec[lb_count++] = n;
		}
	}

	// program memory instructions
	for (u32 n = 0; n < count; n++)
	{
		insn const &i = ep[n];
		bool const is_sploop = (i.op == dasm::OP_SPLOOPD) || (i.op == dasm::OP_SPLOOP) || (i.op == dasm::OP_SPLOOPW);
		if (restart && !is_sploop)
			continue; // instructions in parallel with a resumed SPLOOP don't execute
		if (loading && m_lb_initial_term && !(unit_mask(i.unit) & mask))
			continue;
		if (resuming && ((unit_mask(i.unit) & mask) || (i.op == dasm::OP_BNOP)))
		{
			// SPMASKed instructions are NOPs when resuming, but keep their NOP cycles
			m_ep_nops = std::max(m_ep_nops, insn_nops(i));
			continue;
		}
		if (condition(i))
			execute(i, pce1[n], next);
		else if ((i.op == dasm::OP_BNOP) || (i.op == dasm::OP_BNOP_REG))
			m_ep_nops = std::max<u32>(m_ep_nops, i.cst2);
		else if (is_sploop)
			execute(i, pce1[n], next); // the condition only matters for reload or termination
	}

	// loop buffer instructions; their NOP counts (protected loads, BNOP) don't hold up program fetch
	u32 const pm_nops = m_ep_nops;
	for (u32 n = 0; n < lb_count; n++)
	{
		insn const &i = m_lb_insn[lb_exec[n]];
		if (condition(i))
			execute(i, m_lb[lb_exec[n]].pce1, next);
	}
	m_ep_nops = pm_nops;

	// load the loop buffer
	if (loading && fetched && !m_ep_sploop)
		sploop_load(ep, e->raw, e->hdr, pce1, count, mask);

	// stores happen after the loads in the same cycle
	for (u32 n = 0; n < m_store_count; n++)
	{
		store const &s = m_stores[n];
		switch (s.size)
		{
		case 1:
			m_program.write_byte(s.address, u8(s.data));
			break;
		case 2:
			m_program.write_word(s.address & ~1U, u16(s.data));
			break;
		case 4:
			if (s.unaligned)
			{
				for (u32 b = 0; b < 4; b++)
					m_program.write_byte(s.address + b, u8(s.data >> (b * 8)));
			}
			else
				m_program.write_dword(s.address & ~3U, u32(s.data));
			break;
		case 8:
			if (s.unaligned)
			{
				for (u32 b = 0; b < 8; b++)
					m_program.write_byte(s.address + b, u8(s.data >> (b * 8)));
			}
			else
			{
				m_program.write_dword(s.address & ~7U, u32(s.data));
				m_program.write_dword((s.address & ~7U) + 4, u32(s.data >> 32));
			}
			break;
		}
	}

	if (fetched)
	{
		// SPLX from B IRP/B NRP only resumes a SPLOOP at the branch target
		if (!m_lb_active && !m_branches && !restart)
			m_tsr &= ~(1U << 14);
		m_pc = next;
		m_nop_cycles = m_ep_nops;
	}

	if (lb_was_active && m_lb_active)
	{
		if (kernel && m_lb_loading)
		{
			// last execute packet of the loop body
			m_lb_loading = false;
			m_lb_kernel = true;
			m_lb_dynlen = m_lb_loadcnt + 1;

			// stage and cycle split depends on ii
			u32 const ii = m_lb_ii;
			u32 const cbits = (ii <= 1) ? 0 : (ii <= 2) ? 1 : (ii <= 4) ? 2 : (ii <= 8) ? 3 : 4;
			u32 const fcyc = m_lb_stgcyc & make_bitmask<u32>(cbits);
			u32 stg = 0;
			for (u32 b = 0; b < 6 - cbits; b++)
				stg |= BIT(m_lb_stgcyc, 5 - b) << b;
			m_lb_delay = stg * ii + fcyc;
			m_lb_fetch = false;
			LOGMASKED(LOG_SPLOOP, "SPLOOP kernel: ii %u dynlen %u delay %u\n", ii, m_lb_dynlen, m_lb_delay);
		}
		sploop_end_cycle();
	}

	tick();
}

void tms320c674x_device::execute_run()
{
	while (m_icount > 0)
		step();
}


//**************************************************************************
//  LOOP BUFFER
//**************************************************************************

u32 tms320c674x_device::unit_mask(u8 unit)
{
	switch (unit)
	{
	case dasm::UNIT_L1: return 1 << 0;
	case dasm::UNIT_L2: return 1 << 1;
	case dasm::UNIT_S1: return 1 << 2;
	case dasm::UNIT_S2: return 1 << 3;
	case dasm::UNIT_D1: return 1 << 4;
	case dasm::UNIT_D2: return 1 << 5;
	case dasm::UNIT_M1: return 1 << 6;
	case dasm::UNIT_M2: return 1 << 7;
	default: return 0;
	}
}

u32 tms320c674x_device::insn_nops(insn const &i)
{
	switch (i.op)
	{
	case dasm::OP_BNOP: case dasm::OP_BNOP_REG: case dasm::OP_ADDKPC:
		return i.cst2;
	default:
		return (i.flags & dasm::F_PROT) ? 4 : 0;
	}
}

void tms320c674x_device::sploop_start(insn const &i, u32 address)
{
	if (m_lb_active)
		logerror("nested SPLOOP at %08x\n", address);

	// SPRUFE8B 7.13.2: an interrupted SPLOOPD resumes as SPLOOP
	m_lb_restart = BIT(m_tsr, 14);
	m_ep_sploop = true;
	m_lb_active = true;
	m_lb_kind = (m_lb_restart && (i.op == dasm::OP_SPLOOPD)) ? dasm::OP_SPLOOP : i.op;
	m_lb_ii = i.cst;
	m_lb_lbc = 0;
	m_lb_loadcnt = 0;
	m_lb_count = 0;
	m_lb_loading = true;
	m_lb_draining = false;
	m_lb_fetch = true;
	m_lb_kernel = false;
	m_lb_cycles = 0;
	m_lb_drain = 0;
	m_lb_dynlen = 0;
	m_lb_delay = 0;
	m_lb_creg = i.creg;
	m_lb_z = i.z;
	m_lb_cond_history = 0;
	m_lb_address = address;
	m_lb_int_drain = false;
	m_lb_kernelr = false;
	m_lb_reload_pending = false;
	m_lb_reload_start = false;
	m_lb_reload_branch = false;
	m_lb_reloading = false;
	m_lb_old_active = false;
	m_tsr |= 1U << 14; // SPLX

	m_lb_initial_term = false;
	if (m_lb_kind == dasm::OP_SPLOOP)
	{
		if (!m_ilc)
			m_lb_initial_term = true;
		else
			m_ilc--;
	}
	LOGMASKED(LOG_SPLOOP, "%s %u at %08x, ILC %u%s\n", dasm::mnemonic(dasm::op_t(i.op)), i.cst, address, m_ilc, m_lb_restart ? " (resumed)" : "");
}

void tms320c674x_device::sploop_load(insn const *ep, u32 const *raw, u32 const *hdr, u32 const *pce1, u32 count, u32 mask)
{
	for (u32 n = 0; n < count; n++)
	{
		insn const &i = ep[n];
		if ((i.unit == dasm::UNIT_NONE) || (i.op == dasm::OP_BNOP) || (unit_mask(i.unit) & mask))
			continue;
		if (m_lb_count >= LB_SIZE)
		{
			logerror("loop buffer overflow at %08x\n", m_pc);
			return;
		}
		lb_entry &e = m_lb[m_lb_count];
		e.opcode = raw[n];
		e.header = hdr[n];
		e.pce1 = pce1[n];
		e.size = i.size;
		e.slot = m_lb_lbc;
		e.loadcnt = m_lb_loadcnt;
		e.valid = !m_lb_initial_term;
		m_lb_insn[m_lb_count] = i;
		m_lb_count++;
	}
}

bool tms320c674x_device::sploop_terminate() const
{
	// stage boundary termination condition, false for the first 3 cycles of SPLOOPD and SPLOOPW
	if ((m_lb_kind != dasm::OP_SPLOOP) && (m_lb_cycles <= 3))
		return false;
	if (m_lb_kind == dasm::OP_SPLOOPW)
		return !BIT(m_lb_cond_history, 3);
	return !m_ilc;
}

bool tms320c674x_device::sploop_reload_condition() const
{
	// SPRUFE8B 7.9.6: a predicated SPLOOP(D) whose condition was true 4 cycles before the last kernel stage boundary
	return (m_lb_kind != dasm::OP_SPLOOPW) && m_lb_creg && BIT(m_lb_cond_history, 4);
}

bool tms320c674x_device::sploop_interruptible() const
{
	// SPRUFE8B 7.13.1, checked on a stage boundary where the loop doesn't terminate
	if (!m_lb_kernel || m_lb_loading || m_lb_draining)
		return false;
	if (m_lb_reloading || m_lb_reload_pending || m_lb_reload_start || m_lb_old_active)
		return false;
	if ((m_lb_kind != dasm::OP_SPLOOP) && (m_lb_cycles <= 3))
		return false;
	if ((m_lb_kind != dasm::OP_SPLOOPW) && (m_ilc < ((m_lb_dynlen + m_lb_ii - 1U) / m_lb_ii)))
		return false; // returning would early-exit
	return pending_interrupt() >= 0;
}

void tms320c674x_device::sploop_end_cycle()
{
	m_lb_cycles++;
	if (m_lb_loading)
		m_lb_loadcnt++;
	if (m_lb_draining)
		m_lb_drain++;
	if (m_lb_reloading && (++m_lb_reloadcnt >= m_lb_dynlen))
	{
		// every instruction is enabled again; fetch stops for the kernel
		m_lb_reloading = false;
		m_lb_fetch = false;
		if (m_lb_initial_term)
		{
			logerror("SPLOOP at %08x skipped a reload with RILC 0\n", m_lb_address);
			sploop_stop();
			return;
		}
	}
	if (m_lb_old_active)
	{
		if (++m_lb_old_drain >= (m_lb_dynlen - m_lb_ii))
			m_lb_old_active = false;
		else if (++m_lb_old_lbc >= m_lb_ii)
			m_lb_old_lbc = 0;
	}

	// SPLOOPW tests its condition 3 cycles before the stage boundary, reload 4 cycles before
	bool c = true;
	if (m_lb_creg)
	{
		bool const zero = !m_r[m_lb_creg - 1];
		c = m_lb_z ? zero : !zero;
	}
	m_lb_cond_history = (m_lb_cond_history << 1) | (c ? 1 : 0);

	if (++m_lb_lbc >= m_lb_ii)
	{
		m_lb_lbc = 0;

		if (m_lb_initial_term)
		{
			if (m_lb_kernel)
			{
				sploop_stop();
				return;
			}
		}
		else if (m_lb_int_drain)
		{
			// SPLOOPW still tests its condition; if it ends, the instruction after the loop is interrupted
			if ((m_lb_kind == dasm::OP_SPLOOPW) && sploop_terminate())
			{
				LOGMASKED(LOG_SPLOOP, "SPLOOPW done while interrupt draining\n");
				sploop_stop();
				return;
			}
		}
		else if (!m_lb_draining && !m_lb_reload_pending && !m_lb_reload_start)
		{
			if (sploop_terminate())
			{
				if (m_lb_kind == dasm::OP_SPLOOPW)
				{
					// no epilog
					LOGMASKED(LOG_SPLOOP, "SPLOOPW done\n");
					if (m_lb_kernel)
					{
						sploop_stop();
						return;
					}
				}
				m_lb_draining = true;
				m_lb_drain = 0;
				if (sploop_reload_condition())
				{
					// SPKERNELR reloads from the first epilog cycle, otherwise SPMASKR sets the point
					LOGMASKED(LOG_SPLOOP, "SPLOOP at %08x will reload\n", m_lb_address);
					m_lb_reload_pending = true;
					m_lb_reload_branch = true;
					if (m_lb_kernelr)
						m_lb_reload_start = true;
				}
			}
			else if (sploop_interruptible())
			{
				// pipe down, then take the interrupt; ILC keeps the remaining iterations
				LOGMASKED(LOG_SPLOOP, "SPLOOP at %08x interrupt draining, ILC %u\n", m_lb_address, m_ilc);
				m_lb_draining = true;
				m_lb_int_drain = true;
				m_lb_drain = 0;
			}
			else if ((m_lb_kind != dasm::OP_SPLOOPW) && !((m_lb_kind == dasm::OP_SPLOOPD) && (m_lb_cycles <= 3)))
			{
				m_ilc--; // non-zero, or the loop would have terminated
			}
		}
	}

	if (m_lb_draining && m_lb_kernel)
	{
		if (m_lb_drain >= (m_lb_dynlen - m_lb_ii))
		{
			if (m_lb_reload_pending || m_lb_reload_start)
			{
				// drained before the reload point: stay active and wait for it
				m_lb_draining = false;
				for (u32 n = 0; n < m_lb_count; n++)
					m_lb[n].valid = false;
				m_lb_fetch = true;
				return;
			}
			bool const interrupt = m_lb_int_drain;
			sploop_stop();
			if (interrupt)
			{
				// SPRUFE8B 7.13.6: if the interrupt went away, carry on after the loop body
				int const number = pending_interrupt();
				if (number >= 0)
					take_interrupt(number, m_lb_address, true);
			}
			return;
		}
		if (!m_lb_int_drain && (m_lb_drain >= m_lb_delay))
			m_lb_fetch = true; // program memory stays disabled while interrupt draining
	}
}

void tms320c674x_device::sploop_stop()
{
	m_lb_active = false;
	m_lb_loading = false;
	m_lb_draining = false;
	m_lb_int_drain = false;
	m_lb_restart = false;
	m_lb_kernelr = false;
	m_lb_reload_pending = false;
	m_lb_reload_start = false;
	m_lb_reload_branch = false;
	m_lb_reloading = false;
	m_lb_old_active = false;
	m_lb_fetch = true;
	m_tsr &= ~(1U << 14); // SPLX
}

void tms320c674x_device::sploop_reload()
{
	m_lb_reload_start = false;
	m_lb_reload_pending = false;

	// the previous invocation finishes its epilog alongside the reload prolog
	m_lb_old_active = m_lb_draining;
	m_lb_old_lbc = m_lb_lbc;
	m_lb_old_drain = m_lb_drain;
	for (u32 n = 0; n < m_lb_count; n++)
	{
		m_lb[n].old_valid = m_lb_draining && m_lb[n].valid;
		m_lb[n].valid = false;
	}

	// SPRUFE8B 7.9.6.2: ILC = RILC - 1, or RILC = 0 skips this invocation
	m_lb_initial_term = !m_rilc;
	if (m_rilc)
		m_ilc = m_rilc - 1;
	m_lb_draining = false;
	m_lb_drain = 0;
	m_lb_reloading = true;
	m_lb_reloadcnt = 0;
	m_lb_lbc = 0;
	LOGMASKED(LOG_SPLOOP, "SPLOOP at %08x reloading, ILC %u\n", m_lb_address, m_ilc);
}


//**************************************************************************
//  CONTROL REGISTERS
//**************************************************************************

u32 tms320c674x_device::read_control(u32 crlo, u32 pce1)
{
	switch (crlo)
	{
	case 0x00: return m_amr;
	case 0x01: return m_csr;
	case 0x02: return m_ifr;
	case 0x04: return m_ier | 1;
	case 0x05:
		{
			// HPEINT: highest priority enabled pending interrupt
			u32 const pending = m_ifr & m_ier & 0xfff2;
			u32 const hpeint = pending ? std::countr_zero(pending) : 0;
			return (m_istp & 0xffff'fc00) | (hpeint << 5);
		}
	case 0x06: return m_irp;
	case 0x07: return m_nrp;
	case 0x0a:
		{
			u64 const tsc = m_tsc_running ? (m_cycle - m_tsc_start) : 0;
			m_tsch = u32(tsc >> 32);
			return u32(tsc);
		}
	case 0x0b: return m_tsch;
	case 0x0d: return m_ilc;
	case 0x0e: return m_rilc;
	case 0x0f: return m_rep;
	case 0x10: return pce1;
	case 0x11: return 0; // DNUM
	case 0x12: return m_fadcr;
	case 0x13: return m_faucr;
	case 0x14: return m_fmcr;
	case 0x15: return m_ssr;
	case 0x16: return m_gplya;
	case 0x17: return m_gplyb;
	case 0x18: return m_gfpgfr;
	case 0x1a: return (m_tsr & ~1U) | BIT(m_csr, 0);
	case 0x1b: return m_itsr;
	case 0x1c: return m_ntsr;
	case 0x1d: return m_efr;
	case 0x1f: return m_ierr;
	default:
		logerror("read of control register %02x at %08x\n", crlo, m_pc);
		return 0;
	}
}

void tms320c674x_device::write_control(u32 crlo, u32 value)
{
	switch (crlo)
	{
	case 0x00: m_amr = value; break;
	case 0x01: m_csr = (m_csr & 0xffff'0100) | (value & 0x0000'feff); break;
	case 0x02: m_ifr |= value & 0xfff0; break;      // ISR
	case 0x03: m_ifr &= ~(value & 0xfff0); break;   // ICR
	case 0x04: m_ier = (value & 0xfff2) | 1; break;
	case 0x05: m_istp = value & 0xffff'fc00; break;
	case 0x06: m_irp = value; break;
	case 0x07: m_nrp = value; break;
	case 0x0a: // writing TSCL starts the counter
		if (!m_tsc_running)
		{
			m_tsc_running = true;
			m_tsc_start = m_cycle;
		}
		break;
	case 0x0d: write(REG_ILC, value, 3); break;
	case 0x0e: write(REG_RILC, value, 3); break;
	case 0x0f: m_rep = value; break;
	case 0x12: m_fadcr = value; break;
	case 0x13: m_faucr = value; break;
	case 0x14: m_fmcr = value; break;
	case 0x15: m_ssr = value; break;
	case 0x16: m_gplya = value; break;
	case 0x17: m_gplyb = value; break;
	case 0x18: m_gfpgfr = value; break;
	case 0x1a:
		m_tsr = (m_tsr & ~0x1fU) | (value & 0x1e);
		m_csr = (m_csr & ~1U) | (value & 1);
		break;
	case 0x1b: m_itsr = value; break;
	case 0x1c: m_ntsr = value; break;
	case 0x1d: m_efr &= ~value; break; // ECR
	case 0x1f: m_ierr = value; break;
	default:
		logerror("write of %08x to control register %02x at %08x\n", value, crlo, m_pc);
		break;
	}
}


//**************************************************************************
//  MEMORY
//**************************************************************************

u32 tms320c674x_device::address(insn const &i, u32 shift)
{
	u32 const base = m_r[i.base];
	u32 offset = i.offset_reg ? m_r[i.a] : u32(i.cst);
	if (i.scaled)
		offset <<= shift;

	// circular addressing for A4-A7 and B4-B7
	u32 const n = i.base & 31;
	u32 mode = 0;
	if ((n >= 4) && (n <= 7))
		mode = BIT(m_amr, ((i.base & 32) ? 8 : 0) + (n - 4) * 2, 2);
	auto const modify = [this, base, mode] (u32 result) -> u32
	{
		if (!mode || (mode == 3))
			return result;
		u32 const bk = BIT(m_amr, (mode == 1) ? 16 : 21, 5);
		u32 const size_mask = u32((u64(2) << bk) - 1);
		return (base & ~size_mask) | (result & size_mask);
	};

	switch (i.mode)
	{
	case dasm::AM_NEG_OFFSET:
		return modify(base - offset);
	case dasm::AM_POS_OFFSET:
		return modify(base + offset);
	case dasm::AM_PREDEC:
		{
			u32 const a = modify(base - offset);
			write(i.base, a);
			return a;
		}
	case dasm::AM_PREINC:
		{
			u32 const a = modify(base + offset);
			write(i.base, a);
			return a;
		}
	case dasm::AM_POSTDEC:
		write(i.base, modify(base - offset));
		return base;
	case dasm::AM_POSTINC:
		write(i.base, modify(base + offset));
		return base;
	default:
		return base;
	}
}

u64 tms320c674x_device::load(insn const &i, u32 addr)
{
	switch (i.op)
	{
	case dasm::OP_LDB: return u32(s32(s8(m_program.read_byte(addr))));
	case dasm::OP_LDBU: return m_program.read_byte(addr);
	case dasm::OP_LDH: return u32(s32(s16(m_program.read_word(addr & ~1U))));
	case dasm::OP_LDHU: return m_program.read_word(addr & ~1U);
	case dasm::OP_LDW: return m_program.read_dword(addr & ~3U);
	case dasm::OP_LDNW:
		{
			u32 v = 0;
			for (u32 b = 0; b < 4; b++)
				v |= u32(m_program.read_byte(addr + b)) << (b * 8);
			return v;
		}
	case dasm::OP_LDDW:
		return m_program.read_dword(addr & ~7U) | (u64(m_program.read_dword((addr & ~7U) + 4)) << 32);
	case dasm::OP_LDNDW:
		{
			u64 v = 0;
			for (u32 b = 0; b < 8; b++)
				v |= u64(m_program.read_byte(addr + b)) << (b * 8);
			return v;
		}
	default:
		return 0;
	}
}


//**************************************************************************
//  FLOATING POINT
//**************************************************************************

// denormal operands count as zero
float tms320c674x_device::sp_in(u32 value)
{
	if (!(value & 0x7f80'0000))
		value &= 0x8000'0000;
	return std::bit_cast<float>(value);
}

double tms320c674x_device::dp_in(u64 value)
{
	if (!(value & 0x7ff0'0000'0000'0000U))
		value &= 0x8000'0000'0000'0000U;
	return std::bit_cast<double>(value);
}

// denormal results become zero, NaN results are all ones
u32 tms320c674x_device::sp_result(float value)
{
	u32 v = std::bit_cast<u32>(value);
	if (std::isnan(value))
		return 0x7fff'ffff;
	if (!(v & 0x7f80'0000))
		v &= 0x8000'0000;
	return v;
}

u64 tms320c674x_device::dp_result(double value)
{
	u64 v = std::bit_cast<u64>(value);
	if (std::isnan(value))
		return 0x7fff'ffff'ffff'ffffU;
	if (!(v & 0x7ff0'0000'0000'0000U))
		v &= 0x8000'0000'0000'0000U;
	return v;
}


//**************************************************************************
//  INSTRUCTIONS
//**************************************************************************

namespace {

s32 saturate32(s64 v, bool &sat)
{
	if (v > 0x7fff'ffffLL) { sat = true; return 0x7fff'ffff; }
	if (v < -0x8000'0000LL) { sat = true; return s32(0x8000'0000); }
	return s32(v);
}

s64 saturate40(s64 v, bool &sat)
{
	constexpr s64 MAX = (s64(1) << 39) - 1;
	constexpr s64 MIN = -(s64(1) << 39);
	if (v > MAX) { sat = true; return MAX; }
	if (v < MIN) { sat = true; return MIN; }
	return v;
}

s32 sat16(s32 v) { return std::clamp<s32>(v, -0x8000, 0x7fff); }
u32 lo16(u32 v) { return v & 0xffff; }
u32 hi16(u32 v) { return v >> 16; }
s32 slo16(u32 v) { return s16(v); }
s32 shi16(u32 v) { return s16(v >> 16); }

template <typename F>
u32 lanes16(u32 a, u32 b, F f)
{
	return (u32(f(a >> 16, b >> 16) & 0xffff) << 16) | (f(a & 0xffff, b & 0xffff) & 0xffff);
}

template <typename F>
u32 lanes8(u32 a, u32 b, F f)
{
	u32 r = 0;
	for (u32 n = 0; n < 4; n++)
		r |= u32(f((a >> (n * 8)) & 0xff, (b >> (n * 8)) & 0xff) & 0xff) << (n * 8);
	return r;
}

s32 float_to_int(double v, bool truncate)
{
	if (std::isnan(v))
		return 0x7fff'ffff;
	if (!truncate)
		v = std::nearbyint(v);
	else
		v = std::trunc(v);
	if (v >= 2147483648.0)
		return 0x7fff'ffff;
	if (v < -2147483648.0)
		return s32(0x8000'0000);
	return s32(v);
}

u32 norm32(u32 v)
{
	u32 const x = v ^ u32(s32(v) >> 31);
	return std::countl_zero(x) - 1;
}

} // anonymous namespace

void tms320c674x_device::execute(insn const &i, u32 pce1, u32 next_pc)
{
	u32 const d = i.dst;
	bool sat = false;

	switch (i.op)
	{
	// no unit
	case dasm::OP_NOP:
		m_ep_nops = std::max<u32>(m_ep_nops, i.cst - 1);
		break;
	case dasm::OP_IDLE:
		m_idle = true;
		break;
	case dasm::OP_DINT:
		m_tsr = (m_tsr & ~2U) | (BIT(m_csr, 0) << 1);
		m_csr &= ~1U;
		break;
	case dasm::OP_RINT:
		m_csr = (m_csr & ~1U) | BIT(m_tsr, 1);
		break;
	case dasm::OP_SPLOOP: case dasm::OP_SPLOOPD: case dasm::OP_SPLOOPW:
		sploop_start(i, m_pc); // the execute packet, the return address if interrupted
		break;
	case dasm::OP_SPKERNEL: case dasm::OP_SPKERNELR: case dasm::OP_SPMASK: case dasm::OP_SPMASKR:
		break;

	// branches
	case dasm::OP_B:
		branch(i.cst);
		break;
	case dasm::OP_B_REG:
		branch(m_r[i.b]);
		break;
	case dasm::OP_BNOP:
		branch(i.cst);
		m_ep_nops = std::max<u32>(m_ep_nops, i.cst2);
		break;
	case dasm::OP_BNOP_REG:
		branch(m_r[i.b]);
		m_ep_nops = std::max<u32>(m_ep_nops, i.cst2);
		break;
	case dasm::OP_B_IRP:
		branch(m_irp);
		m_csr = (m_csr & ~1U) | BIT(m_itsr, 0);
		m_tsr = m_itsr & ~1U;
		break;
	case dasm::OP_B_NRP:
		branch(m_nrp);
		m_ier |= 2; // NMIE
		m_csr = (m_csr & ~1U) | BIT(m_ntsr, 0);
		m_tsr = m_ntsr & ~1U;
		break;
	case dasm::OP_CALLP:
		branch(i.cst);
		write(d, next_pc);
		m_ep_nops = std::max<u32>(m_ep_nops, 5);
		break;
	case dasm::OP_ADDKPC:
		write(d, i.cst);
		m_ep_nops = std::max<u32>(m_ep_nops, i.cst2);
		break;
	case dasm::OP_BDEC:
		if (s32(m_r[d]) >= 0)
		{
			branch(i.cst);
			write(d, m_r[d] - 1);
		}
		break;
	case dasm::OP_BPOS:
		if (s32(m_r[d]) >= 0)
			branch(i.cst);
		break;

	// moves
	case dasm::OP_MV: write(d, m_r[i.b]); break;
	case dasm::OP_MVK: write(d, i.cst); break;
	case dasm::OP_MVKH: write(d, (m_r[d] & 0xffff) | (u32(i.cst) & 0xffff'0000)); break;
	case dasm::OP_ADDK: write(d, m_r[d] + i.cst); break;
	case dasm::OP_MVD: write(d, m_r[i.b], 3); break;
	case dasm::OP_DMV: write(d | 1, src_a(i)); write(d & ~1U, src_b(i)); break;
	case dasm::OP_MVC_TO: write_control(i.cst, m_r[i.b]); break;
	case dasm::OP_MVC_FROM: write(d, read_control(i.cst, pce1)); break;

	// integer arithmetic
	case dasm::OP_ADD: write(d, src_a(i) + src_b(i)); break;
	case dasm::OP_SUB: write(d, src_a(i) - src_b(i)); break;
	case dasm::OP_ADD_IIL: write_long(d, s64(s32(src_a(i))) + s32(src_b(i))); break;
	case dasm::OP_ADD_IL: write_long(d, s64(s32(src_a(i))) + long40(i.b)); break;
	case dasm::OP_ADDU_IIL: write_long(d, u64(src_a(i)) + src_b(i)); break;
	case dasm::OP_ADDU_IL: write_long(d, u64(src_a(i)) + ulong40(i.b)); break;
	case dasm::OP_SUB_IIL: write_long(d, s64(s32(src_a(i))) - s32(src_b(i))); break;
	case dasm::OP_SUB_IL: write_long(d, s64(s32(src_a(i))) - long40(i.b)); break;
	case dasm::OP_SUBU_IIL: write_long(d, u64(src_a(i)) - src_b(i)); break;
	case dasm::OP_SADD: write(d, saturate32(s64(s32(src_a(i))) + s32(src_b(i)), sat)); break;
	case dasm::OP_SADD_L: write_long(d, saturate40(s64(s32(src_a(i))) + long40(i.b), sat)); break;
	case dasm::OP_SSUB: write(d, saturate32(s64(s32(src_a(i))) - s32(src_b(i)), sat)); break;
	case dasm::OP_SSUB_L: write_long(d, saturate40(s64(s32(src_a(i))) - long40(i.b), sat)); break;
	case dasm::OP_ABS:
		{
			s32 const v = s32(m_r[i.b]);
			write(d, (v == s32(0x8000'0000)) ? 0x7fff'ffff : std::abs(v));
		}
		break;
	case dasm::OP_ABS_L:
		{
			s64 const v = long40(i.b);
			write_long(d, (v == -(s64(1) << 39)) ? ((s64(1) << 39) - 1) : std::abs(v));
		}
		break;
	case dasm::OP_SAT:
		write(d, saturate32(long40(i.b), sat));
		break;
	case dasm::OP_NORM: write(d, norm32(m_r[i.b])); break;
	case dasm::OP_NORM_L:
		{
			u64 const v = ulong40(i.b);
			u64 const x = (v ^ (BIT(v, 39) ? 0xff'ffff'ffffU : 0)) & 0xff'ffff'ffffU;
			write(d, (x ? (std::countl_zero(x) - 24) : 40) - 1);
		}
		break;
	case dasm::OP_LMBD:
		{
			u32 const v = BIT(src_a(i), 0) ? m_r[i.b] : ~m_r[i.b];
			write(d, std::countl_zero(v));
		}
		break;
	case dasm::OP_SUBC:
		{
			u32 const a = src_a(i), b = src_b(i);
			write(d, (a >= b) ? (((a - b) << 1) + 1) : (a << 1));
		}
		break;
	case dasm::OP_ADDAB: write(d, src_a(i) + src_b(i)); break;
	case dasm::OP_ADDAH: write(d, src_a(i) + (src_b(i) << 1)); break;
	case dasm::OP_ADDAW: write(d, src_a(i) + (src_b(i) << 2)); break;
	case dasm::OP_ADDAD: write(d, src_a(i) + (src_b(i) << 3)); break;
	case dasm::OP_SUBAB: write(d, src_a(i) - src_b(i)); break;
	case dasm::OP_SUBAH: write(d, src_a(i) - (src_b(i) << 1)); break;
	case dasm::OP_SUBAW: write(d, src_a(i) - (src_b(i) << 2)); break;

	// logical
	case dasm::OP_AND: write(d, src_a(i) & src_b(i)); break;
	case dasm::OP_ANDN: write(d, src_a(i) & ~src_b(i)); break;
	case dasm::OP_OR: write(d, src_a(i) | src_b(i)); break;
	case dasm::OP_XOR: write(d, src_a(i) ^ src_b(i)); break;

	// compares
	case dasm::OP_CMPEQ: write(d, src_a(i) == src_b(i)); break;
	case dasm::OP_CMPGT: write(d, s32(src_a(i)) > s32(src_b(i))); break;
	case dasm::OP_CMPGTU: write(d, src_a(i) > src_b(i)); break;
	case dasm::OP_CMPLT: write(d, s32(src_a(i)) < s32(src_b(i))); break;
	case dasm::OP_CMPLTU: write(d, src_a(i) < src_b(i)); break;
	case dasm::OP_CMPEQ_L: write(d, s64(s32(src_a(i))) == long40(i.b)); break;
	case dasm::OP_CMPGT_L: write(d, s64(s32(src_a(i))) > long40(i.b)); break;
	case dasm::OP_CMPLT_L: write(d, s64(s32(src_a(i))) < long40(i.b)); break;
	case dasm::OP_CMPGTU_L: write(d, u64(src_a(i)) > ulong40(i.b)); break;
	case dasm::OP_CMPLTU_L: write(d, u64(src_a(i)) < ulong40(i.b)); break;

	// shifts and fields
	case dasm::OP_SHL:
		{
			u32 const n = src_b(i) & 0x3f;
			write(d, (n >= 32) ? 0 : (src_a(i) << n));
		}
		break;
	case dasm::OP_SHL_L:
		{
			u32 const n = src_b(i) & 0x3f;
			write_long(d, (n >= 40) ? 0 : (ulong40(i.a) << n));
		}
		break;
	case dasm::OP_SHL_UL:
		{
			u32 const n = src_b(i) & 0x3f;
			write_long(d, (n >= 40) ? 0 : (u64(src_a(i)) << n));
		}
		break;
	case dasm::OP_SHR:
		{
			u32 const n = std::min<u32>(src_b(i) & 0x3f, 31);
			write(d, u32(s32(src_a(i)) >> n));
		}
		break;
	case dasm::OP_SHR_L:
		{
			u32 const n = std::min<u32>(src_b(i) & 0x3f, 39);
			write_long(d, u64(long40(i.a) >> n));
		}
		break;
	case dasm::OP_SHRU:
		{
			u32 const n = src_b(i) & 0x3f;
			write(d, (n >= 32) ? 0 : (src_a(i) >> n));
		}
		break;
	case dasm::OP_SHRU_L:
		{
			u32 const n = src_b(i) & 0x3f;
			write_long(d, (n >= 40) ? 0 : (ulong40(i.a) >> n));
		}
		break;
	case dasm::OP_SSHL:
		{
			u32 const n = src_b(i) & 0x3f;
			s32 const v = s32(src_a(i));
			s64 const r = (n >= 32) ? (v ? ((v < 0) ? INT64_MIN : INT64_MAX) : 0) : (s64(v) << n);
			write(d, saturate32(r, sat));
		}
		break;
	case dasm::OP_ROTL:
		write(d, std::rotl(src_a(i), src_b(i) & 0x1f), 1);
		break;
	case dasm::OP_EXT: case dasm::OP_EXTU:
		{
			u32 csta, cstb;
			if (i.flags & dasm::F_B_CST)
			{
				csta = i.cst;
				cstb = i.cst2;
			}
			else
			{
				csta = BIT(m_r[i.b], 5, 5);
				cstb = BIT(m_r[i.b], 0, 5);
			}
			u32 const v = m_r[i.a] << csta;
			write(d, (i.op == dasm::OP_EXT) ? u32(s32(v) >> cstb) : (v >> cstb));
		}
		break;
	case dasm::OP_SET: case dasm::OP_CLR:
		{
			u32 csta, cstb;
			if (i.flags & dasm::F_B_CST)
			{
				csta = i.cst;
				cstb = i.cst2;
			}
			else
			{
				csta = BIT(m_r[i.b], 5, 5);
				cstb = BIT(m_r[i.b], 0, 5);
			}
			u32 mask = 0;
			if (cstb >= csta)
				mask = u32((u64(2) << (cstb - csta)) - 1) << csta;
			write(d, (i.op == dasm::OP_SET) ? (m_r[i.a] | mask) : (m_r[i.a] & ~mask));
		}
		break;

	// packed data
	case dasm::OP_ADD2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return x + y; })); break;
	case dasm::OP_SUB2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return x - y; })); break;
	case dasm::OP_ADD4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return x + y; })); break;
	case dasm::OP_SUB4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return x - y; })); break;
	case dasm::OP_MAX2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return u32(std::max<s32>(s16(x), s16(y))); })); break;
	case dasm::OP_MIN2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return u32(std::min<s32>(s16(x), s16(y))); })); break;
	case dasm::OP_MAXU4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return std::max(x, y); })); break;
	case dasm::OP_MINU4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return std::min(x, y); })); break;
	case dasm::OP_SADD2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return u32(sat16(s16(x) + s16(y))); })); break;
	case dasm::OP_SADDU4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return std::min<u32>(x + y, 0xff); })); break;
	case dasm::OP_SADDUS2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return u32(std::clamp<s32>(s32(x) + s16(y), 0, 0xffff)); })); break;
	case dasm::OP_SSUB2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return u32(sat16(s16(x) - s16(y))); })); break;
	case dasm::OP_SUBABS4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return (x > y) ? (x - y) : (y - x); })); break;
	case dasm::OP_AVG2: write(d, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return u32((s32(s16(x)) + s16(y) + 1) >> 1); }), 1); break;
	case dasm::OP_AVGU4: write(d, lanes8(src_a(i), src_b(i), [] (u32 x, u32 y) { return (x + y + 1) >> 1; }), 1); break;
	case dasm::OP_SHR2:
		{
			u32 const n = std::min<u32>(src_b(i) & 0x1f, 15);
			write(d, lanes16(src_a(i), 0, [n] (u32 x, u32) { return u32(s32(s16(x)) >> n); }));
		}
		break;
	case dasm::OP_SHRU2:
		{
			u32 const n = src_b(i) & 0x1f;
			write(d, lanes16(src_a(i), 0, [n] (u32 x, u32) { return (n >= 16) ? 0 : (x >> n); }));
		}
		break;
	case dasm::OP_CMPEQ2:
		{
			u32 const a = src_a(i), b = src_b(i);
			write(d, ((lo16(a) == lo16(b)) ? 1 : 0) | ((hi16(a) == hi16(b)) ? 2 : 0));
		}
		break;
	case dasm::OP_CMPGT2:
		{
			u32 const a = src_a(i), b = src_b(i);
			write(d, ((slo16(a) > slo16(b)) ? 1 : 0) | ((shi16(a) > shi16(b)) ? 2 : 0));
		}
		break;
	case dasm::OP_CMPEQ4: case dasm::OP_CMPGTU4:
		{
			u32 const a = src_a(i), b = src_b(i);
			u32 r = 0;
			for (u32 n = 0; n < 4; n++)
			{
				u32 const x = (a >> (n * 8)) & 0xff, y = (b >> (n * 8)) & 0xff;
				if ((i.op == dasm::OP_CMPEQ4) ? (x == y) : (x > y))
					r |= 1 << n;
			}
			write(d, r);
		}
		break;
	case dasm::OP_PACK2: write(d, (lo16(src_a(i)) << 16) | lo16(src_b(i))); break;
	case dasm::OP_PACKH2: write(d, (hi16(src_a(i)) << 16) | hi16(src_b(i))); break;
	case dasm::OP_PACKHL2: write(d, (hi16(src_a(i)) << 16) | lo16(src_b(i))); break;
	case dasm::OP_PACKLH2: write(d, (lo16(src_a(i)) << 16) | hi16(src_b(i))); break;
	case dasm::OP_PACKL4:
		{
			u32 const a = src_a(i), b = src_b(i);
			write(d, (BIT(a, 16, 8) << 24) | (BIT(a, 0, 8) << 16) | (BIT(b, 16, 8) << 8) | BIT(b, 0, 8));
		}
		break;
	case dasm::OP_PACKH4:
		{
			u32 const a = src_a(i), b = src_b(i);
			write(d, (BIT(a, 24, 8) << 24) | (BIT(a, 8, 8) << 16) | (BIT(b, 24, 8) << 8) | BIT(b, 8, 8));
		}
		break;
	case dasm::OP_SPACK2:
		write(d, (u32(sat16(std::clamp<s32>(s32(src_a(i)), -0x8000, 0x7fff)) & 0xffff) << 16) | (u32(std::clamp<s32>(s32(src_b(i)), -0x8000, 0x7fff)) & 0xffff));
		break;
	case dasm::OP_SPACKU4:
		{
			u32 const a = src_a(i), b = src_b(i);
			auto const u8sat = [] (s32 v) -> u32 { return std::clamp<s32>(v, 0, 0xff); };
			write(d, (u8sat(shi16(a)) << 24) | (u8sat(slo16(a)) << 16) | (u8sat(shi16(b)) << 8) | u8sat(slo16(b)));
		}
		break;
	case dasm::OP_SWAP4:
		{
			u32 const v = m_r[i.b];
			write(d, ((v & 0x00ff'00ff) << 8) | ((v >> 8) & 0x00ff'00ff));
		}
		break;
	case dasm::OP_UNPKLU4:
		{
			u32 const v = m_r[i.b];
			write(d, (BIT(v, 8, 8) << 16) | BIT(v, 0, 8));
		}
		break;
	case dasm::OP_UNPKHU4:
		{
			u32 const v = m_r[i.b];
			write(d, (BIT(v, 24, 8) << 16) | BIT(v, 16, 8));
		}
		break;
	case dasm::OP_ABS2:
		write(d, lanes16(m_r[i.b], 0, [] (u32 x, u32) { s32 const v = s16(x); return u32((v == -0x8000) ? 0x7fff : std::abs(v)); }));
		break;
	case dasm::OP_SHLMB: write(d, (src_b(i) << 8) | (src_a(i) >> 24)); break;
	case dasm::OP_SHRMB: write(d, (src_b(i) >> 8) | (src_a(i) << 24)); break;
	case dasm::OP_BITC4:
		{
			u32 const v = m_r[i.b];
			u32 r = 0;
			for (u32 n = 0; n < 4; n++)
				r |= u32(std::popcount((v >> (n * 8)) & 0xff)) << (n * 8);
			write(d, r, 1);
		}
		break;
	case dasm::OP_BITR:
		{
			u32 v = m_r[i.b];
			u32 r = 0;
			for (u32 n = 0; n < 32; n++)
				r |= BIT(v, n) << (31 - n);
			write(d, r, 1);
		}
		break;
	case dasm::OP_XPND2:
		write(d, (BIT(m_r[i.b], 1) ? 0xffff'0000 : 0) | (BIT(m_r[i.b], 0) ? 0x0000'ffff : 0), 1);
		break;
	case dasm::OP_XPND4:
		{
			u32 r = 0;
			for (u32 n = 0; n < 4; n++)
				if (BIT(m_r[i.b], n))
					r |= 0xffU << (n * 8);
			write(d, r, 1);
		}
		break;
	case dasm::OP_ADDSUB:
		write(d | 1, src_a(i) + src_b(i));
		write(d & ~1U, src_a(i) - src_b(i));
		break;
	case dasm::OP_ADDSUB2:
		write(d | 1, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return x + y; }));
		write(d & ~1U, lanes16(src_a(i), src_b(i), [] (u32 x, u32 y) { return x - y; }));
		break;
	case dasm::OP_DPACK2:
		write(d | 1, (hi16(src_a(i)) << 16) | hi16(src_b(i)));
		write(d & ~1U, (lo16(src_a(i)) << 16) | lo16(src_b(i)));
		break;

	// multiplies
	case dasm::OP_MPY: write(d, slo16(src_a(i)) * slo16(src_b(i)), 1); break;
	case dasm::OP_MPYU: write(d, lo16(src_a(i)) * lo16(src_b(i)), 1); break;
	case dasm::OP_MPYUS: write(d, s32(lo16(src_a(i))) * slo16(src_b(i)), 1); break;
	case dasm::OP_MPYSU: write(d, slo16(src_a(i)) * s32(lo16(src_b(i))), 1); break;
	case dasm::OP_MPYH: write(d, shi16(src_a(i)) * shi16(src_b(i)), 1); break;
	case dasm::OP_MPYHU: write(d, hi16(src_a(i)) * hi16(src_b(i)), 1); break;
	case dasm::OP_MPYHUS: write(d, s32(hi16(src_a(i))) * shi16(src_b(i)), 1); break;
	case dasm::OP_MPYHSU: write(d, shi16(src_a(i)) * s32(hi16(src_b(i))), 1); break;
	case dasm::OP_MPYHL: write(d, shi16(src_a(i)) * slo16(src_b(i)), 1); break;
	case dasm::OP_MPYHLU: write(d, hi16(src_a(i)) * lo16(src_b(i)), 1); break;
	case dasm::OP_MPYHSLU: write(d, shi16(src_a(i)) * s32(lo16(src_b(i))), 1); break;
	case dasm::OP_MPYHULS: write(d, s32(hi16(src_a(i))) * slo16(src_b(i)), 1); break;
	case dasm::OP_MPYLH: write(d, slo16(src_a(i)) * shi16(src_b(i)), 1); break;
	case dasm::OP_MPYLHU: write(d, lo16(src_a(i)) * hi16(src_b(i)), 1); break;
	case dasm::OP_MPYLSHU: write(d, slo16(src_a(i)) * s32(hi16(src_b(i))), 1); break;
	case dasm::OP_MPYLUHS: write(d, s32(lo16(src_a(i))) * shi16(src_b(i)), 1); break;
	case dasm::OP_SMPY: case dasm::OP_SMPYH: case dasm::OP_SMPYHL: case dasm::OP_SMPYLH:
		{
			u32 const a = src_a(i), b = src_b(i);
			s32 x, y;
			switch (i.op)
			{
			case dasm::OP_SMPY: x = slo16(a); y = slo16(b); break;
			case dasm::OP_SMPYH: x = shi16(a); y = shi16(b); break;
			case dasm::OP_SMPYHL: x = shi16(a); y = slo16(b); break;
			default: x = slo16(a); y = shi16(b); break;
			}
			s32 r = (x * y) << 1;
			if (r == s32(0x8000'0000))
			{
				r = 0x7fff'ffff;
				sat = true;
			}
			write(d, r, 1);
		}
		break;
	case dasm::OP_MPYI: write(d, u32(s32(src_a(i)) * s32(src_b(i))), 8); break;
	case dasm::OP_MPYID: write_pair(d, u64(s64(s32(src_a(i))) * s32(src_b(i))), 9); break;
	case dasm::OP_MPY32: write(d, u32(s32(src_a(i)) * s32(src_b(i))), 3); break;
	case dasm::OP_MPY32_64: write_pair(d, u64(s64(s32(src_a(i))) * s32(src_b(i))), 3); break;
	case dasm::OP_MPY32U: write_pair(d, u64(src_a(i)) * src_b(i), 3); break;
	case dasm::OP_MPY32US: write_pair(d, u64(s64(src_a(i)) * s32(src_b(i))), 3); break;
	case dasm::OP_MPY32SU: write_pair(d, u64(s64(s32(src_a(i))) * s64(src_b(i))), 3); break;
	case dasm::OP_MPYLI: write_pair(d, u64(s64(slo16(src_a(i))) * s32(src_b(i))), 3); break;
	case dasm::OP_MPYHI: write_pair(d, u64(s64(shi16(src_a(i))) * s32(src_b(i))), 3); break;
	case dasm::OP_MPYLIR: write(d, u32((s64(slo16(src_a(i))) * s32(src_b(i)) + 0x4000) >> 15), 3); break;
	case dasm::OP_MPYHIR: write(d, u32((s64(shi16(src_a(i))) * s32(src_b(i)) + 0x4000) >> 15), 3); break;
	case dasm::OP_SMPY32:
		{
			s32 const a = src_a(i), b = src_b(i);
			if ((a == s32(0x8000'0000)) && (b == s32(0x8000'0000)))
			{
				write(d, 0x7fff'ffff, 3);
				sat = true;
			}
			else
				write(d, u32((s64(a) * b) >> 31), 3);
		}
		break;
	case dasm::OP_MPY2:
		write_pair(d, (u64(u32(shi16(src_a(i)) * shi16(src_b(i)))) << 32) | u32(slo16(src_a(i)) * slo16(src_b(i))), 3);
		break;
	case dasm::OP_MPYU4: case dasm::OP_MPYSU4:
		{
			u32 const a = src_a(i), b = src_b(i);
			u32 p[4];
			for (u32 n = 0; n < 4; n++)
			{
				s32 const x = (i.op == dasm::OP_MPYSU4) ? s32(s8(a >> (n * 8))) : s32((a >> (n * 8)) & 0xff);
				p[n] = u32(x * s32((b >> (n * 8)) & 0xff)) & 0xffff;
			}
			write_pair(d, (u64((p[3] << 16) | p[2]) << 32) | ((p[1] << 16) | p[0]), 3);
		}
		break;
	case dasm::OP_DOTP2: write(d, shi16(src_a(i)) * shi16(src_b(i)) + slo16(src_a(i)) * slo16(src_b(i)), 3); break;
	case dasm::OP_DOTP2_LL: write_pair(d, u64(s64(shi16(src_a(i)) * shi16(src_b(i))) + s64(slo16(src_a(i)) * slo16(src_b(i)))), 3); break;
	case dasm::OP_DOTPN2: write(d, shi16(src_a(i)) * shi16(src_b(i)) - slo16(src_a(i)) * slo16(src_b(i)), 3); break;
	case dasm::OP_DOTPRSU2:
		write(d, u32((s64(shi16(src_a(i))) * hi16(src_b(i)) + s64(slo16(src_a(i))) * lo16(src_b(i)) + 0x8000) >> 16), 3);
		break;
	case dasm::OP_DOTPNRSU2:
		write(d, u32((s64(shi16(src_a(i))) * hi16(src_b(i)) - s64(slo16(src_a(i))) * lo16(src_b(i)) + 0x8000) >> 16), 3);
		break;
	case dasm::OP_DOTPU4: case dasm::OP_DOTPSU4:
		{
			u32 const a = src_a(i), b = src_b(i);
			s32 r = 0;
			for (u32 n = 0; n < 4; n++)
			{
				s32 const x = (i.op == dasm::OP_DOTPSU4) ? s32(s8(a >> (n * 8))) : s32((a >> (n * 8)) & 0xff);
				r += x * s32((b >> (n * 8)) & 0xff);
			}
			write(d, r, 3);
		}
		break;
	case dasm::OP_SSHVL: case dasm::OP_SSHVR:
		{
			s32 n = std::clamp<s32>(s32(src_a(i)), -32, 31);
			if (i.op == dasm::OP_SSHVR)
				n = -n;
			s32 const v = s32(src_b(i));
			s64 r;
			if (n >= 0)
				r = s64(v) << n;
			else
				r = s64(v) >> std::min(-n, 31);
			write(d, saturate32(r, sat), 1);
		}
		break;

	// floating point
	case dasm::OP_ADDSP: write(d, sp_result(sp_in(src_a(i)) + sp_in(src_b(i))), 3); break;
	case dasm::OP_SUBSP: write(d, sp_result(sp_in(src_a(i)) - sp_in(src_b(i))), 3); break;
	case dasm::OP_MPYSP: write(d, sp_result(sp_in(src_a(i)) * sp_in(src_b(i))), 3); break;
	case dasm::OP_ADDDP: write_pair(d, dp_result(dp_in(pair(i.a)) + dp_in(pair(i.b))), 6); break;
	case dasm::OP_SUBDP: write_pair(d, dp_result(dp_in(pair(i.a)) - dp_in(pair(i.b))), 6); break;
	case dasm::OP_MPYDP: write_pair(d, dp_result(dp_in(pair(i.a)) * dp_in(pair(i.b))), 9); break;
	case dasm::OP_MPYSPDP: write_pair(d, dp_result(double(sp_in(src_a(i))) * dp_in(pair(i.b))), 6); break;
	case dasm::OP_MPYSP2DP: write_pair(d, dp_result(double(sp_in(src_a(i))) * double(sp_in(src_b(i)))), 4); break;
	case dasm::OP_ABSSP: write(d, sp_result(std::fabs(sp_in(m_r[i.b])))); break;
	case dasm::OP_ABSDP: write_pair(d, dp_result(std::fabs(dp_in(pair(i.b)))), 1); break;
	case dasm::OP_CMPEQSP: write(d, sp_in(src_a(i)) == sp_in(src_b(i))); break;
	case dasm::OP_CMPGTSP: write(d, sp_in(src_a(i)) > sp_in(src_b(i))); break;
	case dasm::OP_CMPLTSP: write(d, sp_in(src_a(i)) < sp_in(src_b(i))); break;
	case dasm::OP_CMPEQDP: write(d, dp_in(pair(i.a)) == dp_in(pair(i.b)), 1); break;
	case dasm::OP_CMPGTDP: write(d, dp_in(pair(i.a)) > dp_in(pair(i.b)), 1); break;
	case dasm::OP_CMPLTDP: write(d, dp_in(pair(i.a)) < dp_in(pair(i.b)), 1); break;
	case dasm::OP_INTSP: write(d, sp_result(float(s32(m_r[i.b]))), 3); break;
	case dasm::OP_INTSPU: write(d, sp_result(float(m_r[i.b])), 3); break;
	case dasm::OP_INTDP: write_pair(d, dp_result(double(s32(m_r[i.b]))), 4); break;
	case dasm::OP_INTDPU: write_pair(d, dp_result(double(m_r[i.b])), 4); break;
	case dasm::OP_SPINT: write(d, float_to_int(sp_in(m_r[i.b]), false), 3); break;
	case dasm::OP_SPTRUNC: write(d, float_to_int(sp_in(m_r[i.b]), true), 3); break;
	case dasm::OP_DPINT: write(d, float_to_int(dp_in(pair(i.b)), false), 3); break;
	case dasm::OP_DPTRUNC: write(d, float_to_int(dp_in(pair(i.b)), true), 3); break;
	case dasm::OP_DPSP: write(d, sp_result(float(dp_in(pair(i.b)))), 3); break;
	case dasm::OP_SPDP: write_pair(d, dp_result(double(sp_in(m_r[i.b]))), 1); break;
	case dasm::OP_RCPSP: case dasm::OP_RSQRSP:
		{
			float const x = sp_in(m_r[i.b]);
			float const r = (i.op == dasm::OP_RCPSP) ? (1.0f / x) : (1.0f / std::sqrt(x));
			u32 v = sp_result(r);
			if ((v & 0x7f80'0000) != 0x7f80'0000)
				v &= 0xffff'8000; // the mantissa is accurate to 8 bits
			write(d, v);
		}
		break;
	case dasm::OP_RCPDP: case dasm::OP_RSQRDP:
		{
			double const x = dp_in(pair(i.b));
			double const r = (i.op == dasm::OP_RCPDP) ? (1.0 / x) : (1.0 / std::sqrt(x));
			u64 v = dp_result(r);
			if ((v & 0x7ff0'0000'0000'0000U) != 0x7ff0'0000'0000'0000U)
				v &= 0xffff'f000'0000'0000U;
			write_pair(d, v, 1);
		}
		break;

	// loads and stores
	case dasm::OP_LDB: case dasm::OP_LDBU:
	case dasm::OP_LDH: case dasm::OP_LDHU:
	case dasm::OP_LDW: case dasm::OP_LDNW:
	case dasm::OP_LDDW: case dasm::OP_LDNDW:
		{
			u32 const shift =
					((i.op == dasm::OP_LDB) || (i.op == dasm::OP_LDBU)) ? 0 :
					((i.op == dasm::OP_LDH) || (i.op == dasm::OP_LDHU)) ? 1 :
					((i.op == dasm::OP_LDDW) || (i.op == dasm::OP_LDNDW)) ? 3 : 2;
			u32 const addr = address(i, shift);
			u64 const v = load(i, addr);
			if (shift == 3)
				write_pair(d, v, 4);
			else
				write(d, u32(v), 4);
			if (i.flags & dasm::F_PROT)
				m_ep_nops = std::max<u32>(m_ep_nops, 4);
		}
		break;
	case dasm::OP_STB: case dasm::OP_STH:
	case dasm::OP_STW: case dasm::OP_STNW:
	case dasm::OP_STDW: case dasm::OP_STNDW:
		{
			u32 const shift =
					(i.op == dasm::OP_STB) ? 0 :
					(i.op == dasm::OP_STH) ? 1 :
					((i.op == dasm::OP_STDW) || (i.op == dasm::OP_STNDW)) ? 3 : 2;
			u32 const addr = address(i, shift);
			if (m_store_count < std::size(m_stores))
			{
				store &s = m_stores[m_store_count++];
				s.address = addr;
				s.size = 1 << shift;
				s.unaligned = (i.op == dasm::OP_STNW) || (i.op == dasm::OP_STNDW);
				s.data = (shift == 3) ? pair(d) : m_r[d];
			}
		}
		break;

	default:
		logerror("unimplemented %s at %08x\n", dasm::mnemonic(i.op), m_pc);
		break;
	}

	if (sat)
		set_sat();
}
