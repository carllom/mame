// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C674x SPLOOP loop buffer

    Used by the interpreter, and by the recompiler on a copy of the loop
    buffer while it compiles a software pipelined loop. The environment
    provides what the loop buffer reads and changes elsewhere: ILC and
    RILC, TSR.SPLX, the SPLOOP condition register and pending
    interrupts. The interpreter's environment uses the CPU state; the
    recompiler's records what the generated code has to test and do.

***************************************************************************/
#ifndef MAME_CPU_TMS320C6X_TMS320C674XLB_IPP
#define MAME_CPU_TMS320C6X_TMS320C674XLB_IPP

#pragma once


inline u32 tms320c674x_device::unit_mask(u8 unit)
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

inline u32 tms320c674x_device::insn_nops(insn const &i)
{
	switch (i.op)
	{
	case dasm::OP_BNOP: case dasm::OP_BNOP_REG: case dasm::OP_ADDKPC:
		return i.cst2;
	default:
		return (i.flags & dasm::F_PROT) ? 4 : 0;
	}
}

template <typename Env>
void tms320c674x_device::lb_packet(loop_buffer &lb, Env &env, insn const *ep, u32 count, u32 &mask, bool &kernel, bool &restart)
{
	// units masked by SPMASK, the end of the loop body, and returning to an interrupted SPLOOP
	mask = 0;
	kernel = false;
	restart = false;
	for (u32 n = 0; n < count; n++)
	{
		if ((ep[n].op == dasm::OP_SPLOOP) || (ep[n].op == dasm::OP_SPLOOPD) || (ep[n].op == dasm::OP_SPLOOPW))
		{
			restart = !lb.active && env.splx();
		}
		else if ((ep[n].op == dasm::OP_SPMASK) || (ep[n].op == dasm::OP_SPMASKR))
		{
			mask |= ep[n].cst;
			if ((ep[n].op == dasm::OP_SPMASKR) && lb.active && lb.reload_pending && !lb.kernelr)
				lb.reload_start = true;
		}
		else if (ep[n].op == dasm::OP_SPKERNEL)
		{
			kernel = true;
			lb.stgcyc = ep[n].cst;
		}
		else if (ep[n].op == dasm::OP_SPKERNELR)
		{
			// post-loop code and reload both start with the epilog
			kernel = true;
			lb.stgcyc = 0;
			if (lb.active && lb.loading)
				lb.kernelr = true;
		}
	}
}

inline u32 tms320c674x_device::lb_select(loop_buffer &lb, u32 mask, bool resuming, u8 *exec)
{
	// loop buffer instructions due this cycle; piping up after an interrupt ignores SPMASK
	u32 count = 0;
	u32 const lb_mask = resuming ? 0 : mask;
	for (u32 n = 0; n < lb.count; n++)
	{
		lb_entry &e = lb.entry[n];
		bool run = false;

		// the previous invocation drains on its own LBC while this one reloads
		if (lb.old_active && (e.slot == lb.old_lbc))
		{
			if (e.loadcnt == lb.old_drain)
				e.old_valid = false;
			run = e.old_valid;
		}

		// draining disables and reloading enables instructions in the order they were loaded
		if (e.slot == lb.lbc)
		{
			if (lb.draining && (e.loadcnt == lb.drain))
				e.valid = false;
			if (lb.reloading && !lb.initial_term && (e.loadcnt == lb.reloadcnt))
				e.valid = true;
			run = run || e.valid;
		}

		if (run && !(unit_mask(lb.decoded[n].unit) & lb_mask))
			exec[count++] = n;
	}
	return count;
}

inline int tms320c674x_device::lb_pm_action(loop_buffer const &lb, insn const &i, u32 mask, bool restart, bool loading, bool resuming)
{
	// what happens to a program memory instruction in a cycle the loop buffer may be active
	bool const is_sploop = (i.op == dasm::OP_SPLOOPD) || (i.op == dasm::OP_SPLOOP) || (i.op == dasm::OP_SPLOOPW);
	if (restart && !is_sploop)
		return PM_SKIP; // instructions in parallel with a resumed SPLOOP don't execute
	if (loading && lb.initial_term && !(unit_mask(i.unit) & mask))
		return PM_SKIP;
	if (resuming && ((unit_mask(i.unit) & mask) || (i.op == dasm::OP_BNOP)))
		return PM_SKIP_NOPS; // SPMASKed instructions are NOPs when resuming, but keep their NOP cycles
	return PM_EXECUTE;
}

template <typename Env>
void tms320c674x_device::sploop_start(loop_buffer &lb, Env &env, insn const &i, u32 address)
{
	if (lb.active)
		env.error("nested SPLOOP at %08x\n", address);

	// SPRUFE8B 7.13.2: an interrupted SPLOOPD resumes as SPLOOP
	lb.restart = env.splx();
	lb.active = true;
	lb.kind = (lb.restart && (i.op == dasm::OP_SPLOOPD)) ? dasm::OP_SPLOOP : i.op;
	lb.ii = i.cst;
	lb.lbc = 0;
	lb.loadcnt = 0;
	lb.count = 0;
	lb.loading = true;
	lb.draining = false;
	lb.fetch = true;
	lb.kernel = false;
	lb.cycles = 0;
	lb.drain = 0;
	lb.dynlen = 0;
	lb.delay = 0;
	lb.creg = i.creg;
	lb.z = i.z;
	env.cond_reset(lb);
	lb.address = address;
	lb.int_drain = false;
	lb.kernelr = false;
	lb.reload_pending = false;
	lb.reload_start = false;
	lb.reload_branch = false;
	lb.reloading = false;
	lb.old_active = false;
	env.set_splx(true);

	lb.initial_term = false;
	if (lb.kind == dasm::OP_SPLOOP)
	{
		if (env.ilc_zero())
			lb.initial_term = true;
		else
			env.ilc_decrement();
	}
	env.log("%s %u at %08x, ILC %u%s\n", dasm::mnemonic(dasm::op_t(i.op)), i.cst, address, env.ilc(), lb.restart ? " (resumed)" : "");
}

template <typename Env>
void tms320c674x_device::sploop_load(loop_buffer &lb, Env &env, insn const *ep, u32 const *raw, u32 const *hdr, u32 const *pce1, u32 count, u32 mask, u32 pc)
{
	for (u32 n = 0; n < count; n++)
	{
		insn const &i = ep[n];
		if ((i.unit == dasm::UNIT_NONE) || (i.op == dasm::OP_BNOP) || (unit_mask(i.unit) & mask))
			continue;
		if (lb.count >= LB_SIZE)
		{
			env.error("loop buffer overflow at %08x\n", pc);
			return;
		}
		lb_entry &e = lb.entry[lb.count];
		e.opcode = raw[n];
		e.header = hdr[n];
		e.pce1 = pce1[n];
		e.size = i.size;
		e.slot = lb.lbc;
		e.loadcnt = lb.loadcnt;
		e.valid = !lb.initial_term;
		lb.decoded[lb.count] = i;
		lb.count++;
	}
}

template <typename Env>
bool tms320c674x_device::sploop_terminate(loop_buffer const &lb, Env &env)
{
	// stage boundary termination condition, false for the first 3 cycles of SPLOOPD and SPLOOPW
	if ((lb.kind != dasm::OP_SPLOOP) && (lb.cycles <= 3))
		return false;
	if (lb.kind == dasm::OP_SPLOOPW)
		return !env.cond_bit(lb, 3);
	return env.ilc_zero();
}

template <typename Env>
bool tms320c674x_device::sploop_reload_condition(loop_buffer const &lb, Env &env)
{
	// SPRUFE8B 7.9.6: a predicated SPLOOP(D) whose condition was true 4 cycles before the last kernel stage boundary
	return (lb.kind != dasm::OP_SPLOOPW) && lb.creg && env.cond_bit(lb, 4);
}

template <typename Env>
bool tms320c674x_device::sploop_interruptible(loop_buffer const &lb, Env &env)
{
	// SPRUFE8B 7.13.1, checked on a stage boundary where the loop doesn't terminate
	if (!lb.kernel || lb.loading || lb.draining)
		return false;
	if (lb.reloading || lb.reload_pending || lb.reload_start || lb.old_active)
		return false;
	if ((lb.kind != dasm::OP_SPLOOP) && (lb.cycles <= 3))
		return false;

	// with ILC too low to return to, the loop would end early
	return env.interrupt_takeable(lb.kind != dasm::OP_SPLOOPW, (lb.dynlen + lb.ii - 1U) / lb.ii);
}

template <typename Env>
void tms320c674x_device::lb_end_packet(loop_buffer &lb, Env &env, bool kernel)
{
	if (kernel && lb.loading)
	{
		// last execute packet of the loop body
		lb.loading = false;
		lb.kernel = true;
		lb.dynlen = lb.loadcnt + 1;

		// stage and cycle split depends on ii
		u32 const ii = lb.ii;
		u32 const cbits = (ii <= 1) ? 0 : (ii <= 2) ? 1 : (ii <= 4) ? 2 : (ii <= 8) ? 3 : 4;
		u32 const fcyc = lb.stgcyc & make_bitmask<u32>(cbits);
		u32 stg = 0;
		for (u32 b = 0; b < 6 - cbits; b++)
			stg |= BIT(lb.stgcyc, 5 - b) << b;
		lb.delay = stg * ii + fcyc;
		lb.fetch = false;
		env.log("SPLOOP kernel: ii %u dynlen %u delay %u\n", ii, lb.dynlen, lb.delay);
	}
	sploop_end_cycle(lb, env);
}

template <typename Env>
void tms320c674x_device::sploop_end_cycle(loop_buffer &lb, Env &env)
{
	lb.cycles++;
	if (lb.loading)
		lb.loadcnt++;
	if (lb.draining)
		lb.drain++;
	if (lb.reloading && (++lb.reloadcnt >= lb.dynlen))
	{
		// every instruction is enabled again; fetch stops for the kernel
		lb.reloading = false;
		lb.fetch = false;
		if (lb.initial_term)
		{
			env.error("SPLOOP at %08x skipped a reload with RILC 0\n", lb.address);
			sploop_stop(lb, env);
			return;
		}
	}
	if (lb.old_active)
	{
		if (++lb.old_drain >= (lb.dynlen - lb.ii))
			lb.old_active = false;
		else if (++lb.old_lbc >= lb.ii)
			lb.old_lbc = 0;
	}

	// SPLOOPW tests its condition 3 cycles before the stage boundary, reload 4 cycles before
	env.cond_shift(lb);

	if (++lb.lbc >= lb.ii)
	{
		lb.lbc = 0;

		if (lb.initial_term)
		{
			if (lb.kernel)
			{
				sploop_stop(lb, env);
				return;
			}
		}
		else if (lb.int_drain)
		{
			// SPLOOPW still tests its condition; if it ends, the instruction after the loop is interrupted
			if ((lb.kind == dasm::OP_SPLOOPW) && sploop_terminate(lb, env))
			{
				env.log("SPLOOPW done while interrupt draining\n");
				sploop_stop(lb, env);
				return;
			}
		}
		else if (!lb.draining && !lb.reload_pending && !lb.reload_start)
		{
			if (sploop_terminate(lb, env))
			{
				if (lb.kind == dasm::OP_SPLOOPW)
				{
					// no epilog
					env.log("SPLOOPW done\n");
					if (lb.kernel)
					{
						sploop_stop(lb, env);
						return;
					}
				}
				lb.draining = true;
				lb.drain = 0;
				if (sploop_reload_condition(lb, env))
				{
					// SPKERNELR reloads from the first epilog cycle, otherwise SPMASKR sets the point
					env.log("SPLOOP at %08x will reload\n", lb.address);
					lb.reload_pending = true;
					lb.reload_branch = true;
					if (lb.kernelr)
						lb.reload_start = true;
				}
			}
			else if (sploop_interruptible(lb, env))
			{
				// pipe down, then take the interrupt; ILC keeps the remaining iterations
				env.log("SPLOOP at %08x interrupt draining, ILC %u\n", lb.address, env.ilc());
				lb.draining = true;
				lb.int_drain = true;
				lb.drain = 0;
			}
			else if ((lb.kind != dasm::OP_SPLOOPW) && !((lb.kind == dasm::OP_SPLOOPD) && (lb.cycles <= 3)))
			{
				env.ilc_decrement(); // non-zero, or the loop would have terminated
			}
		}
	}

	if (lb.draining && lb.kernel)
	{
		if (lb.drain >= (lb.dynlen - lb.ii))
		{
			if (lb.reload_pending || lb.reload_start)
			{
				// drained before the reload point: stay active and wait for it
				lb.draining = false;
				for (u32 n = 0; n < lb.count; n++)
					lb.entry[n].valid = false;
				lb.fetch = true;
				return;
			}
			bool const interrupt = lb.int_drain;
			sploop_stop(lb, env);
			if (interrupt)
			{
				// SPRUFE8B 7.13.6: if the interrupt went away, carry on after the loop body
				env.take_pending_interrupt(lb.address);
			}
			return;
		}
		if (!lb.int_drain && (lb.drain >= lb.delay))
			lb.fetch = true; // program memory stays disabled while interrupt draining
	}
}

template <typename Env>
void tms320c674x_device::sploop_stop(loop_buffer &lb, Env &env)
{
	lb.active = false;
	lb.loading = false;
	lb.draining = false;
	lb.int_drain = false;
	lb.restart = false;
	lb.kernelr = false;
	lb.reload_pending = false;
	lb.reload_start = false;
	lb.reload_branch = false;
	lb.reloading = false;
	lb.old_active = false;
	lb.fetch = true;
	env.set_splx(false);
}

template <typename Env>
void tms320c674x_device::sploop_reload(loop_buffer &lb, Env &env)
{
	lb.reload_start = false;
	lb.reload_pending = false;

	// the previous invocation finishes its epilog alongside the reload prolog
	lb.old_active = lb.draining;
	lb.old_lbc = lb.lbc;
	lb.old_drain = lb.drain;
	for (u32 n = 0; n < lb.count; n++)
	{
		lb.entry[n].old_valid = lb.draining && lb.entry[n].valid;
		lb.entry[n].valid = false;
	}

	// SPRUFE8B 7.9.6.2: ILC = RILC - 1, or RILC = 0 skips this invocation
	lb.initial_term = env.rilc_zero();
	if (!lb.initial_term)
		env.ilc_from_rilc();
	lb.draining = false;
	lb.drain = 0;
	lb.reloading = true;
	lb.reloadcnt = 0;
	lb.lbc = 0;
	env.log("SPLOOP at %08x reloading, ILC %u\n", lb.address, env.ilc());
}

template <typename Env>
void tms320c674x_device::lb_branch_landed(loop_buffer &lb, Env &env)
{
	if (lb.reload_branch)
	{
		// SPRUFE8B 7.9.6.3: the outer loop branch disables fetch, the PC stays at its target
		lb.reload_branch = false;
		lb.fetch = false;
	}
	else
	{
		sploop_stop(lb, env);
	}
}

#endif // MAME_CPU_TMS320C6X_TMS320C674XLB_IPP
