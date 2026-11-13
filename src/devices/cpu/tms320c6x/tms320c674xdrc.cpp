// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C674x recompiler

    A block is a run of execute packets compiled together with their
    timing: a packet takes one cycle plus its NOP cycles, and a branch
    takes effect six cycles after the packet that issued it. A result
    with delay slots is kept aside and only written to its register at
    the start of the cycle it becomes visible in, so the generated code
    reads the same values as the interpreter does. Results still in
    flight when a block ends go to the interpreter's write queue, and the
    next block commits them at the right cycles.

    Cycles are counted for every packet, and a block is left as soon as
    the CPU runs out of cycles or an interrupt might have to be taken.
    The recompiler therefore runs everything at the same cycle as the
    interpreter, and can hand over to it at any packet. The interpreter
    runs what isn't compiled: the loop buffer, IDLE, a branch issued while
    another one is in flight, and an execute packet the CPU was in the
    middle of when its time slice ended.

    Instructions without generated code are run by calling the
    interpreter. Their results go to the write queue.

***************************************************************************/

#include "emu.h"
#include "tms320c674x.h"

#include "cpu/drcumlsh.h"

#include <algorithm>


namespace {

// results of running recompiled code
enum
{
	EXECUTE_OUT_OF_CYCLES = 0,
	EXECUTE_MISSING_CODE,
	EXECUTE_UNMAPPED_CODE,
	EXECUTE_RESET_CACHE,
	EXECUTE_INTERPRET,      // run one cycle in the interpreter
	EXECUTE_CONTINUE        // run the interpreter until no branch is in flight
};

constexpr u32 MAX_BLOCK_PACKETS = 64;
constexpr u32 TSR_SPLX = 1U << 14;

constexpr u64 reg_bit(u32 r) { return u64(1) << r; }

} // anonymous namespace


//**************************************************************************
//  COMPILER STATE
//**************************************************************************

// a result waiting for the end of its delay slots
struct tms320c674x_device::drc_pending
{
	u8 reg;     // 0-63, REG_ILC or REG_RILC
	u16 tmp;    // where the result is kept
	s16 pred;   // condition, or -1 for an unconditional instruction
	u32 cycle;  // block cycle it becomes visible in
};

// a branch in flight
struct tms320c674x_device::drc_flight
{
	bool dynamic;
	s16 pred;       // condition, or -1 if it's taken
	u8 slot;        // where a target from a register is kept
	u32 target;
	u32 landing;    // block cycle it takes effect in
};

// state to hand over when leaving a block between packets
struct tms320c674x_device::drc_exit
{
	tms320c674x_device *cpu;
	u32 boundary;       // block cycle about to begin
	u32 drain_from;     // queued writes are committed up to the block cycle before this
	u32 next_pc;
	u32 nop_cycles;
	s32 icount_adjust;
	u32 pending_count;
	drc_pending const *pending;
	u32 branch_count;
	drc_flight const *branches;
};

// an instruction run by the interpreter
struct tms320c674x_device::drc_call
{
	tms320c674x_device *cpu;
	insn i;
	u32 pce1;
	u32 next_pc;
	u32 pc;
	u32 cycle;
};

// circular addressing
struct tms320c674x_device::drc_address_call
{
	tms320c674x_device *cpu;
	insn i;
	u32 shift;
};

// queued writes to commit
struct tms320c674x_device::drc_drain
{
	tms320c674x_device *cpu;
	u32 from;
	u32 to;
};

// a store waiting for the end of its execute packet
struct tms320c674x_device::drc_pending_store
{
	u8 size;
	bool unaligned;
	s16 pred;
	u16 addr;
	u16 data_lo;
	u16 data_hi;
};

// the execute packet being compiled
struct tms320c674x_device::drc_packet
{
	u64 later_reads = 0;    // registers read by instructions later in the packet
	s16 pred = -1;          // condition of the current instruction
	std::vector<drc_pending_store> stores;
};

struct tms320c674x_device::drc_compiler
{
	struct stub
	{
		uml::code_label label;
		uml::code_label resume;
		drc_exit *exit;
		int code;
		bool irq;
		std::vector<s16> branch_preds;
	};

	u32 cycle = 0;
	std::vector<drc_pending> pending;     // in the order they become visible
	std::vector<drc_flight> branches;     // in the order they take effect
	u32 queue_until = MAX_DELAY + 1;      // queued writes may be due up to this block cycle
	u32 last_drain = 0;                   // queued writes are committed up to this block cycle
	bool irq_dirty = false;
	bool splx_clear = false;
	std::vector<stub> stubs;
	std::vector<std::pair<u32 const *, u32> > pages;

	uml::code_label label() { return m_next_label++; }

	u8 slot()
	{
		u8 const result = m_next_slot;
		m_next_slot = (m_next_slot + 1) % BRANCH_SLOTS;
		return result;
	}

	bool branch_taken() const
	{
		return std::any_of(branches.begin(), branches.end(), [] (drc_flight const &b) { return b.pred < 0; });
	}

	u16 tmp()
	{
		u16 const result = m_next_tmp;
		m_next_tmp = (m_next_tmp + 1) % DRC_TEMPS;
		return result;
	}

	s16 pred()
	{
		s16 const result = m_next_pred;
		m_next_pred = (m_next_pred + 1) % DRC_TEMPS;
		return result;
	}

	void add_pending(drc_pending const &p)
	{
		auto const pos = std::find_if(pending.begin(), pending.end(), [&p] (drc_pending const &q) { return q.cycle > p.cycle; });
		pending.insert(pos, p);
	}

	bool pending_to(u32 reg, u32 cycle) const
	{
		return std::any_of(pending.begin(), pending.end(), [reg, cycle] (drc_pending const &q) { return (q.reg == reg) && (q.cycle == cycle); });
	}

	void add_page(u32 const *gen, u32 value)
	{
		if (std::find_if(pages.begin(), pages.end(), [gen] (auto const &p) { return p.first == gen; }) == pages.end())
			pages.emplace_back(gen, value);
	}

private:
	uml::code_label m_next_label = 1;
	u8 m_next_slot = 0;
	u16 m_next_tmp = 0;
	s16 m_next_pred = 0;
};


//**************************************************************************
//  C CALLBACKS
//**************************************************************************

struct tms320c674x_device::drc_funcs
{
	static void execute(drc_call &c)
	{
		tms320c674x_device &cpu = *c.cpu;
		cpu.m_core->cycle = cpu.m_core->drc_base + c.cycle;
		cpu.m_core->pc = c.pc;
		if (cpu.condition(c.i))
			cpu.execute(c.i, c.pce1, c.next_pc);
	}

	static void address(drc_address_call &c)
	{
		tms320c674x_device &cpu = *c.cpu;
		u32 base;
		cpu.m_core->drc_arg0 = cpu.address(c.i, c.shift, base);
		cpu.m_core->drc_arg1 = base;
	}

	static void drain(drc_drain &d)
	{
		tms320c674x_device &cpu = *d.cpu;
		for (u32 x = d.from; cpu.m_core->wb_total && (x <= d.to); x++)
			cpu.commit_writes((cpu.m_core->drc_base + x) % WB_SLOTS);
	}

	static void leave(drc_exit &e)
	{
		tms320c674x_device &cpu = *e.cpu;
		internal_state &core = *cpu.m_core;

		// queued writes due by the boundary
		for (u32 x = e.drain_from; core.wb_total && (x <= e.boundary); x++)
			cpu.commit_writes((core.drc_base + x) % WB_SLOTS);

		// results due by the boundary are written, the rest are queued
		core.cycle = core.drc_base + e.boundary;
		for (u32 n = 0; n < e.pending_count; n++)
		{
			drc_pending const &p = e.pending[n];
			if ((p.pred >= 0) && !core.drc_pred[p.pred])
				continue;
			u32 const value = core.drc_tmp[p.tmp];
			if (p.cycle > e.boundary)
				cpu.write(p.reg, value, p.cycle - e.boundary - 1);
			else if (p.reg < 64)
				core.r[p.reg] = value;
			else if (p.reg == REG_ILC)
				core.ilc = value;
			else
				core.rilc = value;
		}

		core.pc = e.next_pc;
		core.nop_cycles = e.nop_cycles;
		core.icount += e.icount_adjust;

		for (u32 n = 0; n < e.branch_count; n++)
		{
			drc_flight const &b = e.branches[n];
			if ((b.pred >= 0) && !core.drc_pred[b.pred])
				continue;
			u32 const target = b.dynamic ? core.drc_btarget[b.slot] : b.target;
			if (b.landing <= e.boundary)
			{
				// taking effect at the boundary, cutting short any NOP cycles
				core.pc = target;
				core.nop_cycles = 0;
			}
			else
			{
				core.branch_when[core.branches] = core.drc_base + b.landing;
				core.branch_target[core.branches] = target;
				core.branches++;
			}
		}
	}

	static void poll_irq(tms320c674x_device &cpu)
	{
		// nothing changes until something sets irq_poll again
		bool const pending = cpu.pending_interrupt() >= 0;
		if (!pending)
			cpu.m_core->irq_poll = 0;
		cpu.m_core->drc_arg0 = pending ? 1 : 0;
	}

	static void b_irp(tms320c674x_device &cpu)
	{
		internal_state &core = *cpu.m_core;
		core.csr = (core.csr & ~1U) | BIT(core.itsr, 0);
		core.tsr = core.itsr & ~1U;
		core.irq_poll = 1;
	}

	static void b_nrp(tms320c674x_device &cpu)
	{
		internal_state &core = *cpu.m_core;
		core.ier |= 2; // NMIE
		core.csr = (core.csr & ~1U) | BIT(core.ntsr, 0);
		core.tsr = core.ntsr & ~1U;
		core.irq_poll = 1;
	}
};


//**************************************************************************
//  EXECUTION
//**************************************************************************

bool tms320c674x_device::drc_clean() const
{
	// recompiled code starts at an execute packet with no branch in flight
	return !m_core->idle && !m_core->nop_cycles && !m_core->branches && !m_lb_active;
}

void tms320c674x_device::execute_run_drc()
{
	// a changed memory map invalidates the RAM pointers
	if (m_drc_map_changed)
	{
		m_drc_map_changed = false;
		m_fast_ram.clear();
		drc_flush_cache();
	}

	while (m_core->icount > 0)
	{
		if (!drc_clean())
		{
			step();
			continue;
		}

		switch (m_drcuml->execute(*m_entry))
		{
		case EXECUTE_MISSING_CODE:
			drc_compile_block(m_core->pc);
			break;
		case EXECUTE_INTERPRET:
			step();
			break;
		case EXECUTE_UNMAPPED_CODE:
			throw emu_fatalerror("%s: attempted to execute unmapped code at %08x\n", tag(), m_core->pc);
		case EXECUTE_RESET_CACHE:
			drc_flush_cache();
			break;
		default:
			break;
		}
	}
}

void tms320c674x_device::drc_flush_cache()
{
	if (m_drcuml)
	{
		m_drcuml->reset();
		drc_generate_accessors();
	}
}

void tms320c674x_device::drc_generate_invariant()
{
	drcuml_block &block(m_drcuml->begin_invariant_block(16));

	m_entry = m_drcuml->handle_alloc("entry");
	m_nocode = m_drcuml->handle_alloc("nocode");

	UML_HANDLE(block, *m_entry);
	UML_SETFMOD(block, uml::ROUND_ROUND);
	UML_HASHJMP(block, 0, uml::mem(&m_core->pc), *m_nocode);

	UML_HANDLE(block, *m_nocode);
	UML_GETEXP(block, uml::I0);
	UML_MOV(block, uml::mem(&m_core->pc), uml::I0);
	UML_EXIT(block, EXECUTE_MISSING_CODE);

	block.end();

	m_read8 = m_drcuml->handle_alloc("read8");
	m_read16 = m_drcuml->handle_alloc("read16");
	m_read32 = m_drcuml->handle_alloc("read32");
	m_read32u = m_drcuml->handle_alloc("read32u");
	m_write8 = m_drcuml->handle_alloc("write8");
	m_write16 = m_drcuml->handle_alloc("write16");
	m_write32 = m_drcuml->handle_alloc("write32");
	m_write32u = m_drcuml->handle_alloc("write32u");
	drc_generate_accessors();
}

void tms320c674x_device::drc_find_fast_ram()
{
	// RAM and ROM the recompiled code reads and writes directly, before the write tap hides the pointers
	address_space &s = space(AS_PROGRAM);
	m_fast_ram.clear();
	for (u64 page = 0; page < 0x1'0000'0000U; page += 0x1000)
	{
		u32 const a = u32(page);
		u8 *const r = reinterpret_cast<u8 *>(s.get_read_ptr(a));
		if (!r || (reinterpret_cast<u8 *>(s.get_read_ptr(a + 0xffc)) != (r + 0xffc)))
			continue;
		u8 *const w = reinterpret_cast<u8 *>(s.get_write_ptr(a));
		bool const writable = (w == r) && (reinterpret_cast<u8 *>(s.get_write_ptr(a + 0xffc)) == (w + 0xffc));

		// contiguous pages in the same 16 MB region make a range
		if (!m_fast_ram.empty())
		{
			fast_ram &last = m_fast_ram.back();
			if (((last.start + last.size) == a) && ((last.base + last.size) == r) && (last.writable == writable) && (a & 0x00ff'ffff))
			{
				last.size += 0x1000;
				continue;
			}
		}
		m_fast_ram.push_back(fast_ram{ a, 0x1000, r, writable, nullptr });
	}

	// largest writable ranges first, global addresses before local ones
	std::stable_sort(m_fast_ram.begin(), m_fast_ram.end(), [] (fast_ram const &x, fast_ram const &y) { return (x.writable != y.writable) ? x.writable : (x.size != y.size) ? (x.size > y.size) : (x.start > y.start); });
	for (fast_ram &r : m_fast_ram)
	{
		if (r.writable)
		{
			code_gen(r.start);
			r.gen = m_code_gen[code_region(r.start)].get();
		}
		logerror("recompiler %s %08x-%08x\n", r.writable ? "RAM" : "ROM", r.start, r.start + r.size - 1);
	}
}

void tms320c674x_device::drc_generate_accessors()
{
	drcuml_block &block(m_drcuml->begin_block(2048));
	uml::code_label label = 1;
	drc_accessor(block, label, *m_read8, 1, false, false);
	drc_accessor(block, label, *m_read16, 2, false, false);
	drc_accessor(block, label, *m_read32, 4, false, false);
	drc_accessor(block, label, *m_read32u, 4, false, true);
	drc_accessor(block, label, *m_write8, 1, true, false);
	drc_accessor(block, label, *m_write16, 2, true, false);
	drc_accessor(block, label, *m_write32, 4, true, false);
	drc_accessor(block, label, *m_write32u, 4, true, true);
	block.end();
}

void tms320c674x_device::drc_accessor(drcuml_block &block, uml::code_label &label, uml::code_handle &handle, u32 bytes, bool write, bool unaligned)
{
	using namespace uml;

	// address in I0 (kept), data in I1; uses I2-I4
	operand_size const size = (bytes == 1) ? SIZE_BYTE : (bytes == 2) ? SIZE_WORD : SIZE_DWORD;
	UML_HANDLE(block, handle);

	std::vector<std::pair<code_label, fast_ram const *> > hits;
	for (fast_ram const &r : m_fast_ram)
	{
		if (write && !r.writable)
			continue;
		code_label const hit = label++;
		UML_SUB(block, I2, I0, r.start);
		UML_CMP(block, I2, r.size - bytes + 1);
		UML_JMPc(block, COND_B, hit);
		hits.emplace_back(hit, &r);
	}

	// anything else goes through the memory system, byte by byte if unaligned like the interpreter
	if (!unaligned)
	{
		if (write)
			UML_WRITE(block, I0, I1, size, SPACE_PROGRAM);
		else
			UML_READ(block, I1, I0, size, SPACE_PROGRAM);
	}
	else if (write)
	{
		for (u32 b = 0; b < 4; b++)
		{
			UML_ADD(block, I2, I0, b);
			UML_SHR(block, I3, I1, b * 8);
			UML_WRITE(block, I2, I3, SIZE_BYTE, SPACE_PROGRAM);
		}
	}
	else
	{
		UML_MOV(block, I1, 0);
		for (u32 b = 0; b < 4; b++)
		{
			UML_ADD(block, I2, I0, b);
			UML_READ(block, I3, I2, SIZE_BYTE, SPACE_PROGRAM);
			UML_SHL(block, I3, I3, b * 8);
			UML_OR(block, I1, I1, I3);
		}
	}
	UML_RET(block);

	for (auto const &[hit, r] : hits)
	{
		UML_LABEL(block, hit);
		if (!write)
		{
			UML_LOAD(block, I1, r->base, I2, size, SCALE_x1);
		}
		else
		{
			UML_STORE(block, r->base, I2, I1, size, SCALE_x1);

			// mark the code pages written, like the write tap
			for (u32 b = 0; b < (unaligned ? 2 : 1); b++)
			{
				UML_ADD(block, I3, I2, (r->start & 0x00ff'ffff) + (b ? (bytes - 1) : 0));
				UML_SHR(block, I3, I3, CODE_PAGE_SHIFT);
				UML_LOAD(block, I4, r->gen, I3, SIZE_DWORD, SCALE_x4);
				UML_ADD(block, I4, I4, 1);
				UML_STORE(block, r->gen, I3, I4, SIZE_DWORD, SCALE_x4);
			}
		}
		UML_RET(block);
	}
}


//**************************************************************************
//  BLOCKS
//**************************************************************************

bool tms320c674x_device::drc_compilable(ep_cache_entry const &e, drc_compiler const &c) const
{
	u32 branches = 0;
	for (u32 n = 0; n < e.count; n++)
	{
		switch (e.ep[n].op)
		{
		case dasm::OP_INVALID:
		case dasm::OP_IDLE:
		case dasm::OP_SPLOOP: case dasm::OP_SPLOOPD: case dasm::OP_SPLOOPW:
		case dasm::OP_SPKERNEL: case dasm::OP_SPKERNELR:
		case dasm::OP_SPMASK: case dasm::OP_SPMASKR:
		case dasm::OP_SWE: case dasm::OP_SWENR:
			return false;

		case dasm::OP_B: case dasm::OP_B_REG: case dasm::OP_B_IRP: case dasm::OP_B_NRP:
		case dasm::OP_BNOP: case dasm::OP_BNOP_REG: case dasm::OP_BDEC: case dasm::OP_BPOS:
		case dasm::OP_CALLP:
			branches++;
			break;

		default:
			break;
		}
	}

	// one branch per execute packet, and few enough in flight for the interpreter
	return (branches <= 1) && (!branches || (c.branches.size() < (BRANCH_SLOTS - 2)));
}

void tms320c674x_device::drc_compile_block(u32 pc)
{
	auto profile = g_profiler.start(PROFILER_DRC_COMPILE);

	for (;;)
	{
		try
		{
			drc_compiler c;
			drcuml_block &block(m_drcuml->begin_block(16384));
			uml::code_label const check = c.label();
			uml::code_label const body = c.label();
			uml::code_label const recompile = c.label();

			// code pages are checked first, then the block starts
			UML_HASH(block, 0, pc);
			UML_JMP(block, check);
			UML_LABEL(block, body);
			UML_DMOV(block, uml::mem(&m_core->drc_base), uml::mem(&m_core->cycle));

			ep_cache_entry e;
			u32 fetch = pc;
			for (u32 count = 0; ; count++)
			{
				decode_ep(fetch, e);
				c.add_page(e.gen[0], e.gen_value[0]);
				c.add_page(e.gen[1], e.gen_value[1]);

				// a new cycle begins
				drc_boundary(block, c, c.cycle);
				if (!count || (c.irq_dirty && !c.branch_taken()))
				{
					drc_irq_check(block, c, fetch);
					c.irq_dirty = false;
				}

				if (!drc_compilable(e, c))
				{
					// the interpreter runs this execute packet
					drc_exit *const exit = drc_make_exit(block, c, c.cycle, fetch, 0, 0);
					UML_CALLC(block, &drc_funcs::leave, exit);
					UML_EXIT(block, EXECUTE_INTERPRET);
					break;
				}
				if ((count >= MAX_BLOCK_PACKETS) && c.branches.empty())
				{
					drc_block_end(block, c, c.cycle, fetch);
					break;
				}

				drc_packet_body(block, c, e);

				// TSR.SPLX clears at the end of a cycle with no branch in flight
				if (!c.splx_clear && !c.branch_taken())
				{
					uml::code_label const skip = c.label();
					drc_test_branches(block, c, skip);
					UML_AND(block, uml::mem(&m_core->tsr), uml::mem(&m_core->tsr), ~TSR_SPLX);
					UML_LABEL(block, skip);
					c.splx_clear = c.branches.empty();
				}

				u32 nops = 0;
				for (u32 n = 0; n < e.count; n++)
					nops = std::max(nops, drc_ep_nops(e.ep[n]));
				u32 const start = c.cycle;
				u32 const end = start + 1 + nops;

				// branches take effect, cutting short the NOP cycles
				u32 from = start;
				bool ended = false;
				while (!c.branches.empty() && (c.branches.front().landing <= end))
				{
					u32 const landing = c.branches.front().landing;
					if (from == start)
						drc_count_cycles(block, c, landing - start, start + 1, e.next, nops, landing - start - 1);
					else
						drc_count_cycles(block, c, landing - from, from, e.next, end - from, landing - from);
					from = landing;
					c.cycle = landing;
					drc_boundary(block, c, landing);

					drc_flight const b = c.branches.front();
					uml::parameter const target = b.dynamic ? uml::parameter(uml::mem(&m_core->drc_btarget[b.slot])) : uml::parameter(b.target);
					uml::code_label const skip = c.label();
					if (b.pred >= 0)
					{
						UML_CMP(block, uml::mem(&m_core->drc_pred[b.pred]), 0);
						UML_JMPc(block, uml::COND_E, skip);
					}

					// the interpreter takes over if another branch is still in flight
					drc_exit *const exit = (c.branches.size() > 1) ? drc_make_exit(block, c, landing, e.next, 0, 0) : nullptr;
					c.branches.erase(c.branches.begin());
					if (exit)
						drc_test_branches(block, c, drc_exit_stub(block, c, exit, EXECUTE_CONTINUE));
					drc_block_end(block, c, landing, target);

					if (b.pred < 0)
					{
						ended = true;
						break;
					}
					UML_LABEL(block, skip);
				}
				if (ended)
					break;

				if (from == start)
					drc_count_cycles(block, c, end - start, start + 1, e.next, nops, nops);
				else if (end > from)
					drc_count_cycles(block, c, end - from, from, e.next, end - from, end - from);
				c.cycle = end;
				fetch = e.next;
			}

			// leaving the block
			for (drc_compiler::stub const &s : c.stubs)
			{
				UML_LABEL(block, s.label);
				if (s.irq)
				{
					// not while a branch is in flight
					for (s16 const pred : s.branch_preds)
					{
						UML_CMP(block, uml::mem(&m_core->drc_pred[pred]), 0);
						UML_JMPc(block, uml::COND_NE, s.resume);
					}
					UML_CALLC(block, &drc_funcs::poll_irq, this);
					UML_CMP(block, uml::mem(&m_core->drc_arg0), 0);
					UML_JMPc(block, uml::COND_E, s.resume);
				}
				UML_CALLC(block, &drc_funcs::leave, s.exit);
				UML_EXIT(block, s.code);
			}

			// recompile if the code was written
			UML_LABEL(block, check);
			for (auto const &page : c.pages)
			{
				UML_LOAD(block, uml::I0, page.first, 0, uml::SIZE_DWORD, uml::SCALE_x4);
				UML_CMP(block, uml::I0, page.second);
				UML_JMPc(block, uml::COND_NE, recompile);
			}
			UML_JMP(block, body);
			UML_LABEL(block, recompile);
			UML_MOV(block, uml::mem(&m_core->pc), pc);
			UML_EXIT(block, EXECUTE_MISSING_CODE);

			block.end();
			return;
		}
		catch (drcuml_block::abort_compilation &)
		{
			drc_flush_cache();
		}
	}
}

void tms320c674x_device::drc_boundary(drcuml_block &block, drc_compiler &c, u32 cycle)
{
	// queued writes due by now
	if ((c.last_drain < c.queue_until) && (cycle > c.last_drain))
	{
		drc_drain *const d = m_drcuml->cache_alloc_transient<drc_drain>();
		if (!d)
			block.abort();
		d->cpu = this;
		d->from = c.last_drain + 1;
		d->to = std::min(cycle, c.queue_until);

		uml::code_label const skip = c.label();
		UML_CMP(block, uml::mem(&m_core->wb_total), 0);
		UML_JMPc(block, uml::COND_E, skip);
		UML_CALLC(block, &drc_funcs::drain, d);
		UML_LABEL(block, skip);
	}
	c.last_drain = std::max(c.last_drain, cycle);

	drc_commit(block, c, cycle);
}

void tms320c674x_device::drc_commit(drcuml_block &block, drc_compiler &c, u32 cycle)
{
	// results that become visible by this cycle, in order
	auto it = c.pending.begin();
	for ( ; (it != c.pending.end()) && (it->cycle <= cycle); ++it)
	{
		if (it->pred < 0)
		{
			UML_MOV(block, drc_reg(it->reg), uml::mem(&m_core->drc_tmp[it->tmp]));
		}
		else
		{
			UML_CMP(block, uml::mem(&m_core->drc_pred[it->pred]), 0);
			UML_MOVc(block, uml::COND_NE, drc_reg(it->reg), uml::mem(&m_core->drc_tmp[it->tmp]));
		}
	}
	c.pending.erase(c.pending.begin(), it);
}

tms320c674x_device::drc_exit *tms320c674x_device::drc_make_exit(drcuml_block &block, drc_compiler &c, u32 boundary, u32 next_pc, u32 nop_cycles, s32 icount_adjust)
{
	drc_exit *const e = m_drcuml->cache_alloc_transient<drc_exit>();
	if (!e)
		block.abort();

	e->cpu = this;
	e->boundary = boundary;
	e->drain_from = ((c.last_drain < c.queue_until) && (boundary > c.last_drain)) ? (c.last_drain + 1) : (boundary + 1);
	e->next_pc = next_pc;
	e->nop_cycles = nop_cycles;
	e->icount_adjust = icount_adjust;
	e->pending_count = c.pending.size();
	e->pending = nullptr;
	if (!c.pending.empty())
	{
		drc_pending *const pending = m_drcuml->cache_alloc_transient<drc_pending []>(c.pending.size());
		if (!pending)
			block.abort();
		std::copy(c.pending.begin(), c.pending.end(), pending);
		e->pending = pending;
	}

	e->branch_count = 0;
	e->branches = nullptr;
	if (!c.branches.empty())
	{
		drc_flight *const branches = m_drcuml->cache_alloc_transient<drc_flight []>(c.branches.size());
		if (!branches)
			block.abort();
		for (drc_flight const &b : c.branches)
		{
			if (b.landing >= boundary)
				branches[e->branch_count++] = b;
		}
		e->branches = branches;
	}
	return e;
}

uml::code_label tms320c674x_device::drc_exit_stub(drcuml_block &block, drc_compiler &c, drc_exit *exit, int code)
{
	drc_compiler::stub &s = c.stubs.emplace_back();
	s.label = c.label();
	s.resume = 0;
	s.exit = exit;
	s.code = code;
	s.irq = false;
	return s.label;
}

void tms320c674x_device::drc_irq_check(drcuml_block &block, drc_compiler &c, u32 pc)
{
	// the interpreter takes an interrupt when one is pending at the start of a packet
	drc_exit *const exit = drc_make_exit(block, c, c.cycle, pc, 0, 0);
	uml::code_label const check = drc_exit_stub(block, c, exit, EXECUTE_INTERPRET);
	uml::code_label const resume = c.label();
	drc_compiler::stub &s = c.stubs.back();
	s.irq = true;
	s.resume = resume;
	for (drc_flight const &b : c.branches)
		s.branch_preds.push_back(b.pred);

	UML_CMP(block, uml::mem(&m_core->irq_poll), 0);
	UML_JMPc(block, uml::COND_NE, check);
	UML_LABEL(block, resume);
}

void tms320c674x_device::drc_count_cycles(drcuml_block &block, drc_compiler &c, u32 cycles, u32 boundary, u32 next_pc, u32 nop_cycles, s32 icount_adjust)
{
	// out of cycles: the interpreter finishes any NOP cycles
	UML_SUB(block, uml::mem(&m_core->icount), uml::mem(&m_core->icount), cycles);
	drc_exit *const exit = drc_make_exit(block, c, boundary, next_pc, nop_cycles, icount_adjust);
	UML_JMPc(block, uml::COND_LE, drc_exit_stub(block, c, exit, EXECUTE_OUT_OF_CYCLES));
}

void tms320c674x_device::drc_test_branches(drcuml_block &block, drc_compiler &c, uml::code_label taken)
{
	// jump if any branch in flight is taken
	for (drc_flight const &b : c.branches)
	{
		if (b.pred < 0)
		{
			UML_JMP(block, taken);
			return;
		}
		UML_CMP(block, uml::mem(&m_core->drc_pred[b.pred]), 0);
		UML_JMPc(block, uml::COND_NE, taken);
	}
}

void tms320c674x_device::drc_block_end(drcuml_block &block, drc_compiler &c, u32 boundary, uml::parameter target)
{
	// results still in flight go to the write queue
	if (!c.pending.empty())
	{
		drc_exit *const exit = drc_make_exit(block, c, boundary, target.is_immediate() ? u32(target.immediate()) : 0, 0, 0);
		exit->branch_count = 0;
		UML_CALLC(block, &drc_funcs::leave, exit);
	}
	else
	{
		UML_DADD(block, uml::mem(&m_core->cycle), uml::mem(&m_core->drc_base), boundary);
	}
	UML_HASHJMP(block, 0, target, *m_nocode);
}


//**************************************************************************
//  EXECUTE PACKETS
//**************************************************************************

u32 tms320c674x_device::drc_ep_nops(insn const &i)
{
	// NOP cycles after an execute packet, whether or not the instruction's condition is true
	switch (i.op)
	{
	case dasm::OP_NOP: return i.cst - 1;
	case dasm::OP_CALLP: return 5;
	default: return insn_nops(i);
	}
}

u64 tms320c674x_device::drc_reads(insn const &i)
{
	// registers an instruction reads, or all of them if it isn't known
	u64 r = i.creg ? reg_bit(i.creg - 1) : 0;
	u64 const a = (i.flags & dasm::F_A_CST) ? 0 : reg_bit(i.a);
	u64 const b = (i.flags & dasm::F_B_CST) ? 0 : reg_bit(i.b);

	switch (i.op)
	{
	case dasm::OP_NOP: case dasm::OP_MVK: case dasm::OP_ADDKPC:
	case dasm::OP_B: case dasm::OP_BNOP: case dasm::OP_CALLP: case dasm::OP_B_IRP: case dasm::OP_B_NRP:
		return r;

	case dasm::OP_MV: case dasm::OP_B_REG: case dasm::OP_BNOP_REG:
		return r | reg_bit(i.b);

	case dasm::OP_MVKH: case dasm::OP_ADDK: case dasm::OP_BDEC: case dasm::OP_BPOS:
		return r | reg_bit(i.dst);

	case dasm::OP_ADD: case dasm::OP_SUB: case dasm::OP_AND: case dasm::OP_ANDN: case dasm::OP_OR: case dasm::OP_XOR:
	case dasm::OP_ADDAB: case dasm::OP_ADDAH: case dasm::OP_ADDAW: case dasm::OP_ADDAD:
	case dasm::OP_SUBAB: case dasm::OP_SUBAH: case dasm::OP_SUBAW:
	case dasm::OP_CMPEQ: case dasm::OP_CMPGT: case dasm::OP_CMPGTU: case dasm::OP_CMPLT: case dasm::OP_CMPLTU:
	case dasm::OP_SHL: case dasm::OP_SHR: case dasm::OP_SHRU:
		return r | a | b;

	case dasm::OP_EXT: case dasm::OP_EXTU: case dasm::OP_SET: case dasm::OP_CLR:
		return r | reg_bit(i.a) | b;

	case dasm::OP_ADDSP: case dasm::OP_SUBSP: case dasm::OP_MPYSP:
	case dasm::OP_CMPEQSP: case dasm::OP_CMPGTSP: case dasm::OP_CMPLTSP:
	case dasm::OP_SMPY32: case dasm::OP_SADD:
		return r | a | b;

	case dasm::OP_ABSSP: case dasm::OP_INTSP: case dasm::OP_SPINT: case dasm::OP_SPTRUNC: case dasm::OP_SPDP: case dasm::OP_MVD:
		return r | reg_bit(i.b);

	case dasm::OP_DPSP:
		return r | reg_bit(i.b & ~1U) | reg_bit(i.b | 1U);

	case dasm::OP_ADDDP: case dasm::OP_SUBDP: case dasm::OP_MPYDP:
		return r | reg_bit(i.a & ~1U) | reg_bit(i.a | 1U) | reg_bit(i.b & ~1U) | reg_bit(i.b | 1U);

	case dasm::OP_MPYSPDP:
		return r | a | reg_bit(i.b & ~1U) | reg_bit(i.b | 1U);

	case dasm::OP_LDB: case dasm::OP_LDBU: case dasm::OP_LDH: case dasm::OP_LDHU:
	case dasm::OP_LDW: case dasm::OP_LDNW: case dasm::OP_LDDW: case dasm::OP_LDNDW:
		return r | reg_bit(i.base) | (i.offset_reg ? reg_bit(i.a) : 0);

	case dasm::OP_STB: case dasm::OP_STH: case dasm::OP_STW: case dasm::OP_STNW:
		return r | reg_bit(i.base) | (i.offset_reg ? reg_bit(i.a) : 0) | reg_bit(i.dst);

	case dasm::OP_STDW: case dasm::OP_STNDW:
		return r | reg_bit(i.base) | (i.offset_reg ? reg_bit(i.a) : 0) | reg_bit(i.dst & ~1U) | reg_bit(i.dst | 1U);

	default:
		return ~u64(0);
	}
}

uml::parameter tms320c674x_device::drc_reg(u32 r) const
{
	if (r < 64)
		return uml::mem(&m_core->r[r]);
	else if (r == REG_ILC)
		return uml::mem(&m_core->ilc);
	else
		return uml::mem(&m_core->rilc);
}

uml::parameter tms320c674x_device::drc_src_a(insn const &i) const
{
	return (i.flags & dasm::F_A_CST) ? uml::parameter(u32(i.cst)) : drc_reg(i.a);
}

uml::parameter tms320c674x_device::drc_src_b(insn const &i) const
{
	return (i.flags & dasm::F_B_CST) ? uml::parameter(u32(i.cst)) : drc_reg(i.b);
}

uml::parameter tms320c674x_device::drc_dest(drcuml_block &block, drc_compiler &c, drc_packet &p, u32 r, u32 delay)
{
	// a result visible next cycle that nothing later in the packet reads can be written now
	if (!delay && (r < 64) && !BIT(p.later_reads, r) && !c.pending_to(r, c.cycle + 1))
		return drc_reg(r);

	u16 const tmp = c.tmp();
	c.add_pending(drc_pending{ u8(r), tmp, p.pred, c.cycle + delay + 1 });
	return uml::mem(&m_core->drc_tmp[tmp]);
}

void tms320c674x_device::drc_packet_body(drcuml_block &block, drc_compiler &c, ep_cache_entry const &e)
{
	// instructions in the packet read their sources before any of them write
	u64 later[MAX_EP];
	u64 reads = 0;
	for (u32 n = e.count; n-- > 0; )
	{
		later[n] = reads;
		reads |= drc_reads(e.ep[n]);
	}

	drc_packet p;
	for (u32 n = 0; n < e.count; n++)
	{
		p.later_reads = later[n];
		p.pred = -1;
		if (!drc_insn(block, c, p, e.ep[n], e.pce1[n], e.next))
			drc_fallback(block, c, e.ep[n], e.pce1[n], e.next, e.pc);
	}

	// stores happen after the loads in the same cycle
	for (drc_pending_store const &s : p.stores)
	{
		uml::code_label const skip = c.label();
		if (s.pred >= 0)
		{
			UML_CMP(block, uml::mem(&m_core->drc_pred[s.pred]), 0);
			UML_JMPc(block, uml::COND_E, skip);
		}
		UML_MOV(block, uml::I0, uml::mem(&m_core->drc_tmp[s.addr]));
		UML_MOV(block, uml::I1, uml::mem(&m_core->drc_tmp[s.data_lo]));
		switch (s.size)
		{
		case 1:
			UML_CALLH(block, *m_write8);
			break;
		case 2:
			UML_AND(block, uml::I0, uml::I0, ~1U);
			UML_CALLH(block, *m_write16);
			break;
		case 4:
			if (!s.unaligned)
				UML_AND(block, uml::I0, uml::I0, ~3U);
			UML_CALLH(block, s.unaligned ? *m_write32u : *m_write32);
			break;
		case 8:
			if (!s.unaligned)
				UML_AND(block, uml::I0, uml::I0, ~7U);
			UML_CALLH(block, s.unaligned ? *m_write32u : *m_write32);
			UML_ADD(block, uml::I0, uml::I0, 4);
			UML_MOV(block, uml::I1, uml::mem(&m_core->drc_tmp[s.data_hi]));
			UML_CALLH(block, s.unaligned ? *m_write32u : *m_write32);
			break;
		}
		UML_LABEL(block, skip);
	}
}

void tms320c674x_device::drc_fallback(drcuml_block &block, drc_compiler &c, insn const &i, u32 pce1, u32 next_pc, u32 pc)
{
	drc_call *const call = m_drcuml->cache_alloc_transient<drc_call>();
	if (!call)
		block.abort();
	call->cpu = this;
	call->i = i;
	call->pce1 = pce1;
	call->next_pc = next_pc;
	call->pc = pc;
	call->cycle = c.cycle;
	UML_CALLC(block, &drc_funcs::execute, call);

	// its results are queued, and it may change what interrupts depend on
	c.queue_until = std::max(c.queue_until, c.cycle + MAX_DELAY + 1);
	c.irq_dirty = true;
}


//**************************************************************************
//  INSTRUCTIONS
//**************************************************************************

bool tms320c674x_device::drc_insn(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i, u32 pce1, u32 next_pc)
{
	using namespace uml;

	// instructions with generated code
	switch (i.op)
	{
	case dasm::OP_NOP:
	case dasm::OP_MVK: case dasm::OP_MVKH: case dasm::OP_MV: case dasm::OP_ADDK: case dasm::OP_ADDKPC:
	case dasm::OP_ADD: case dasm::OP_SUB: case dasm::OP_AND: case dasm::OP_ANDN: case dasm::OP_OR: case dasm::OP_XOR:
	case dasm::OP_ADDAB: case dasm::OP_ADDAH: case dasm::OP_ADDAW: case dasm::OP_ADDAD:
	case dasm::OP_SUBAB: case dasm::OP_SUBAH: case dasm::OP_SUBAW:
	case dasm::OP_CMPEQ: case dasm::OP_CMPGT: case dasm::OP_CMPGTU: case dasm::OP_CMPLT: case dasm::OP_CMPLTU:
	case dasm::OP_SHL: case dasm::OP_SHR: case dasm::OP_SHRU:
	case dasm::OP_LDB: case dasm::OP_LDBU: case dasm::OP_LDH: case dasm::OP_LDHU:
	case dasm::OP_LDW: case dasm::OP_LDNW: case dasm::OP_LDDW: case dasm::OP_LDNDW:
	case dasm::OP_STB: case dasm::OP_STH: case dasm::OP_STW: case dasm::OP_STNW: case dasm::OP_STDW: case dasm::OP_STNDW:
	case dasm::OP_ADDSP: case dasm::OP_SUBSP: case dasm::OP_MPYSP:
	case dasm::OP_CMPEQSP: case dasm::OP_CMPGTSP: case dasm::OP_CMPLTSP:
	case dasm::OP_ABSSP: case dasm::OP_INTSP: case dasm::OP_SPINT: case dasm::OP_SPTRUNC:
	case dasm::OP_SPDP: case dasm::OP_DPSP: case dasm::OP_ADDDP: case dasm::OP_SUBDP: case dasm::OP_MPYDP: case dasm::OP_MPYSPDP:
	case dasm::OP_SMPY32: case dasm::OP_SADD: case dasm::OP_MVD:
		break;
	case dasm::OP_EXT: case dasm::OP_EXTU: case dasm::OP_SET: case dasm::OP_CLR:
		if (!(i.flags & dasm::F_B_CST))
			return false;
		break;
	case dasm::OP_B: case dasm::OP_B_REG: case dasm::OP_B_IRP: case dasm::OP_B_NRP:
	case dasm::OP_BNOP: case dasm::OP_BNOP_REG: case dasm::OP_BDEC: case dasm::OP_BPOS:
	case dasm::OP_CALLP:
		drc_branch(block, c, p, i, next_pc);
		return true;
	default:
		return false;
	}

	if (i.op == dasm::OP_NOP)
		return true;

	// a false condition skips the instruction
	uml::code_label skip = 0;
	if (i.creg)
	{
		p.pred = c.pred();
		skip = c.label();
		UML_CMP(block, drc_reg(i.creg - 1), 0);
		UML_SETc(block, i.z ? COND_E : COND_NE, mem(&m_core->drc_pred[p.pred]));
		UML_JMPc(block, i.z ? COND_NE : COND_E, skip);
	}

	u32 const d = i.dst;
	switch (i.op)
	{
	case dasm::OP_MVK:
	case dasm::OP_ADDKPC:
		UML_MOV(block, drc_dest(block, c, p, d, 0), u32(i.cst));
		break;
	case dasm::OP_MVKH:
		UML_AND(block, I0, drc_reg(d), 0xffff);
		UML_OR(block, drc_dest(block, c, p, d, 0), I0, u32(i.cst) & 0xffff'0000);
		break;
	case dasm::OP_MV:
		UML_MOV(block, drc_dest(block, c, p, d, 0), drc_reg(i.b));
		break;
	case dasm::OP_ADDK:
		UML_ADD(block, drc_dest(block, c, p, d, 0), drc_reg(d), u32(i.cst));
		break;

	case dasm::OP_ADD: case dasm::OP_ADDAB:
		UML_ADD(block, drc_dest(block, c, p, d, 0), drc_src_a(i), drc_src_b(i));
		break;
	case dasm::OP_SUB: case dasm::OP_SUBAB:
		UML_SUB(block, drc_dest(block, c, p, d, 0), drc_src_a(i), drc_src_b(i));
		break;
	case dasm::OP_ADDAH: case dasm::OP_ADDAW: case dasm::OP_ADDAD:
		UML_SHL(block, I0, drc_src_b(i), (i.op == dasm::OP_ADDAH) ? 1 : (i.op == dasm::OP_ADDAW) ? 2 : 3);
		UML_ADD(block, drc_dest(block, c, p, d, 0), drc_src_a(i), I0);
		break;
	case dasm::OP_SUBAH: case dasm::OP_SUBAW:
		UML_SHL(block, I0, drc_src_b(i), (i.op == dasm::OP_SUBAH) ? 1 : 2);
		UML_SUB(block, drc_dest(block, c, p, d, 0), drc_src_a(i), I0);
		break;
	case dasm::OP_AND:
		UML_AND(block, drc_dest(block, c, p, d, 0), drc_src_a(i), drc_src_b(i));
		break;
	case dasm::OP_ANDN:
		UML_XOR(block, I0, drc_src_b(i), ~u32(0));
		UML_AND(block, drc_dest(block, c, p, d, 0), drc_src_a(i), I0);
		break;
	case dasm::OP_OR:
		UML_OR(block, drc_dest(block, c, p, d, 0), drc_src_a(i), drc_src_b(i));
		break;
	case dasm::OP_XOR:
		UML_XOR(block, drc_dest(block, c, p, d, 0), drc_src_a(i), drc_src_b(i));
		break;

	case dasm::OP_CMPEQ: case dasm::OP_CMPGT: case dasm::OP_CMPGTU: case dasm::OP_CMPLT: case dasm::OP_CMPLTU:
		{
			condition_t const cond =
					(i.op == dasm::OP_CMPEQ) ? COND_E :
					(i.op == dasm::OP_CMPGT) ? COND_G :
					(i.op == dasm::OP_CMPGTU) ? COND_A :
					(i.op == dasm::OP_CMPLT) ? COND_L : COND_B;
			UML_MOV(block, I0, drc_src_a(i));
			UML_CMP(block, I0, drc_src_b(i));
			UML_SETc(block, cond, drc_dest(block, c, p, d, 0));
		}
		break;

	case dasm::OP_SHL: case dasm::OP_SHRU:
		if (i.flags & dasm::F_B_CST)
		{
			u32 const n = i.cst & 0x3f;
			if (n >= 32)
				UML_MOV(block, drc_dest(block, c, p, d, 0), 0);
			else if (i.op == dasm::OP_SHL)
				UML_SHL(block, drc_dest(block, c, p, d, 0), drc_src_a(i), n);
			else
				UML_SHR(block, drc_dest(block, c, p, d, 0), drc_src_a(i), n);
		}
		else
		{
			UML_AND(block, I1, drc_src_b(i), 0x3f);
			if (i.op == dasm::OP_SHL)
				UML_SHL(block, I0, drc_src_a(i), I1);
			else
				UML_SHR(block, I0, drc_src_a(i), I1);
			UML_CMP(block, I1, 32);
			UML_MOVc(block, COND_AE, I0, 0);
			UML_MOV(block, drc_dest(block, c, p, d, 0), I0);
		}
		break;
	case dasm::OP_SHR:
		if (i.flags & dasm::F_B_CST)
		{
			UML_SAR(block, drc_dest(block, c, p, d, 0), drc_src_a(i), std::min<u32>(i.cst & 0x3f, 31));
		}
		else
		{
			UML_AND(block, I1, drc_src_b(i), 0x3f);
			UML_CMP(block, I1, 31);
			UML_MOVc(block, COND_A, I1, 31);
			UML_SAR(block, drc_dest(block, c, p, d, 0), drc_src_a(i), I1);
		}
		break;

	case dasm::OP_EXT: case dasm::OP_EXTU:
		UML_SHL(block, I0, drc_reg(i.a), u32(i.cst) & 0x1f);
		if (i.op == dasm::OP_EXT)
			UML_SAR(block, drc_dest(block, c, p, d, 0), I0, u32(i.cst2) & 0x1f);
		else
			UML_SHR(block, drc_dest(block, c, p, d, 0), I0, u32(i.cst2) & 0x1f);
		break;
	case dasm::OP_SET: case dasm::OP_CLR:
		{
			u32 const csta = i.cst, cstb = i.cst2;
			u32 mask = 0;
			if (cstb >= csta)
				mask = u32((u64(2) << (cstb - csta)) - 1) << csta;
			if (i.op == dasm::OP_SET)
				UML_OR(block, drc_dest(block, c, p, d, 0), drc_reg(i.a), mask);
			else
				UML_AND(block, drc_dest(block, c, p, d, 0), drc_reg(i.a), ~mask);
		}
		break;

	case dasm::OP_LDB: case dasm::OP_LDBU: case dasm::OP_LDH: case dasm::OP_LDHU:
	case dasm::OP_LDW: case dasm::OP_LDNW: case dasm::OP_LDDW: case dasm::OP_LDNDW:
		drc_load(block, c, p, i);
		break;

	case dasm::OP_STB: case dasm::OP_STH: case dasm::OP_STW: case dasm::OP_STNW: case dasm::OP_STDW: case dasm::OP_STNDW:
		drc_store(block, c, p, i);
		break;

	case dasm::OP_MVD:
		UML_MOV(block, drc_dest(block, c, p, d, 3), drc_reg(i.b));
		break;

	case dasm::OP_SADD:
		{
			code_label const done = c.label();
			UML_ADD(block, I0, drc_src_a(i), drc_src_b(i));
			UML_JMPc(block, COND_NV, done);
			UML_MOV(block, I1, drc_src_a(i));
			UML_SAR(block, I1, I1, 31);
			UML_XOR(block, I0, I1, 0x7fff'ffff);
			UML_OR(block, mem(&m_core->csr), mem(&m_core->csr), 1U << 9); // SAT
			UML_LABEL(block, done);
			UML_MOV(block, drc_dest(block, c, p, d, 0), I0);
		}
		break;

	case dasm::OP_SMPY32:
		{
			code_label const normal = c.label();
			code_label const done = c.label();
			UML_MOV(block, I0, drc_src_a(i));
			UML_MOV(block, I1, drc_src_b(i));
			UML_CMP(block, I0, 0x8000'0000);
			UML_JMPc(block, COND_NE, normal);
			UML_CMP(block, I1, 0x8000'0000);
			UML_JMPc(block, COND_NE, normal);
			UML_MOV(block, I2, 0x7fff'ffff);
			UML_OR(block, mem(&m_core->csr), mem(&m_core->csr), 1U << 9); // SAT
			UML_JMP(block, done);
			UML_LABEL(block, normal);
			UML_DSEXT(block, I0, I0, SIZE_DWORD);
			UML_DSEXT(block, I1, I1, SIZE_DWORD);
			UML_DMULSLW(block, I2, I0, I1);
			UML_DSAR(block, I2, I2, 31);
			UML_LABEL(block, done);
			UML_MOV(block, drc_dest(block, c, p, d, 3), I2);
		}
		break;

	case dasm::OP_ADDSP: case dasm::OP_SUBSP: case dasm::OP_MPYSP:
		drc_sp_in(block, I0, drc_src_a(i));
		drc_sp_in(block, I1, drc_src_b(i));
		UML_FSCOPYI(block, F0, I0);
		UML_FSCOPYI(block, F1, I1);
		if (i.op == dasm::OP_ADDSP)
			UML_FSADD(block, F0, F0, F1);
		else if (i.op == dasm::OP_SUBSP)
			UML_FSSUB(block, F0, F0, F1);
		else
			UML_FSMUL(block, F0, F0, F1);
		UML_ICOPYFS(block, I0, F0);
		drc_sp_out(block, I0);
		UML_MOV(block, drc_dest(block, c, p, d, 3), I0);
		break;

	case dasm::OP_CMPEQSP: case dasm::OP_CMPGTSP: case dasm::OP_CMPLTSP:
		drc_sp_in(block, I0, drc_src_a(i));
		drc_sp_in(block, I1, drc_src_b(i));
		UML_FSCOPYI(block, F0, I0);
		UML_FSCOPYI(block, F1, I1);
		// comparisons with NaN are false; FCMP clears Z and C when unordered
		if (i.op == dasm::OP_CMPLTSP)
			UML_FSCMP(block, F1, F0);
		else
			UML_FSCMP(block, F0, F1);
		UML_SETc(block, (i.op == dasm::OP_CMPEQSP) ? COND_E : COND_A, I0);
		UML_SETc(block, COND_NU, I1);
		UML_AND(block, drc_dest(block, c, p, d, 0), I0, I1);
		break;

	case dasm::OP_ABSSP:
		drc_sp_in(block, I0, drc_reg(i.b));
		UML_AND(block, I0, I0, 0x7fff'ffff);
		drc_sp_out(block, I0);
		UML_MOV(block, drc_dest(block, c, p, d, 0), I0);
		break;

	case dasm::OP_INTSP:
		UML_FSFRINT(block, F0, drc_reg(i.b), SIZE_DWORD);
		UML_ICOPYFS(block, I0, F0);
		drc_sp_out(block, I0);
		UML_MOV(block, drc_dest(block, c, p, d, 3), I0);
		break;

	case dasm::OP_SPINT: case dasm::OP_SPTRUNC:
		{
			// the host gives 0x8000'0000 for NaN and out of range, but positive overflow and NaN saturate high
			code_label const done = c.label();
			drc_sp_in(block, I1, drc_reg(i.b));
			UML_FSCOPYI(block, F0, I1);
			UML_FSTOINT(block, I0, F0, SIZE_DWORD, (i.op == dasm::OP_SPINT) ? ROUND_ROUND : ROUND_TRUNC);
			UML_CMP(block, I0, 0x8000'0000);
			UML_JMPc(block, COND_NE, done);
			UML_AND(block, I2, I1, 0x7fff'ffff);
			UML_CMP(block, I2, 0x7f80'0000);
			UML_MOVc(block, COND_A, I0, 0x7fff'ffff);
			UML_TEST(block, I1, 0x8000'0000);
			UML_MOVc(block, COND_Z, I0, 0x7fff'ffff);
			UML_LABEL(block, done);
			UML_MOV(block, drc_dest(block, c, p, d, 3), I0);
		}
		break;

	case dasm::OP_SPDP:
		drc_sp_in(block, I0, drc_reg(i.b));
		UML_FSCOPYI(block, F0, I0);
		UML_FDFRFLT(block, F1, F0, SIZE_SHORT);
		UML_ICOPYFD(block, I0, F1);
		drc_dp_out(block, I0);
		drc_dest_pair(block, c, p, d, 1, I0);
		break;

	case dasm::OP_DPSP:
		drc_dp_in(block, I0, i.b);
		UML_FDCOPYI(block, F0, I0);
		UML_FSFRFLT(block, F1, F0, SIZE_QWORD);
		UML_ICOPYFS(block, I0, F1);
		drc_sp_out(block, I0);
		UML_MOV(block, drc_dest(block, c, p, d, 3), I0);
		break;

	case dasm::OP_ADDDP: case dasm::OP_SUBDP: case dasm::OP_MPYDP:
		drc_dp_in(block, I0, i.a);
		drc_dp_in(block, I1, i.b);
		UML_FDCOPYI(block, F0, I0);
		UML_FDCOPYI(block, F1, I1);
		if (i.op == dasm::OP_ADDDP)
			UML_FDADD(block, F0, F0, F1);
		else if (i.op == dasm::OP_SUBDP)
			UML_FDSUB(block, F0, F0, F1);
		else
			UML_FDMUL(block, F0, F0, F1);
		UML_ICOPYFD(block, I0, F0);
		drc_dp_out(block, I0);
		drc_dest_pair(block, c, p, d, (i.op == dasm::OP_MPYDP) ? 9 : 6, I0);
		break;

	case dasm::OP_MPYSPDP:
		drc_sp_in(block, I0, drc_src_a(i));
		UML_FSCOPYI(block, F2, I0);
		UML_FDFRFLT(block, F0, F2, SIZE_SHORT);
		drc_dp_in(block, I1, i.b);
		UML_FDCOPYI(block, F1, I1);
		UML_FDMUL(block, F0, F0, F1);
		UML_ICOPYFD(block, I0, F0);
		drc_dp_out(block, I0);
		drc_dest_pair(block, c, p, d, 6, I0);
		break;

	default:
		break;
	}

	if (skip)
		UML_LABEL(block, skip);
	return true;
}

void tms320c674x_device::drc_dest_pair(drcuml_block &block, drc_compiler &c, drc_packet &p, u32 r, u32 delay, uml::parameter value)
{
	// a 64-bit value in a register pair
	UML_MOV(block, drc_dest(block, c, p, r & ~1U, delay), value);
	UML_DSHR(block, uml::I3, value, 32);
	UML_MOV(block, drc_dest(block, c, p, r | 1U, delay), uml::I3);
}

void tms320c674x_device::drc_sp_in(drcuml_block &block, uml::parameter dst, uml::parameter src)
{
	// a denormal operand counts as zero
	UML_MOV(block, dst, src);
	UML_AND(block, uml::I3, dst, 0x8000'0000);
	UML_TEST(block, dst, 0x7f80'0000);
	UML_MOVc(block, uml::COND_Z, dst, uml::I3);
}

void tms320c674x_device::drc_sp_out(drcuml_block &block, uml::parameter value)
{
	// a NaN result is all ones, a denormal one is zero
	UML_AND(block, uml::I3, value, 0x7fff'ffff);
	UML_CMP(block, uml::I3, 0x7f80'0000);
	UML_MOVc(block, uml::COND_A, value, 0x7fff'ffff);
	UML_AND(block, uml::I3, value, 0x8000'0000);
	UML_TEST(block, value, 0x7f80'0000);
	UML_MOVc(block, uml::COND_Z, value, uml::I3);
}

void tms320c674x_device::drc_dp_in(drcuml_block &block, uml::parameter dst, u32 r)
{
	UML_DMOV(block, dst, uml::mem(&m_core->r[r & ~1U]));
	UML_DAND(block, uml::I3, dst, 0x8000'0000'0000'0000U);
	UML_DTEST(block, dst, 0x7ff0'0000'0000'0000U);
	UML_DMOVc(block, uml::COND_Z, dst, uml::I3);
}

void tms320c674x_device::drc_dp_out(drcuml_block &block, uml::parameter value)
{
	UML_DAND(block, uml::I3, value, 0x7fff'ffff'ffff'ffffU);
	UML_DCMP(block, uml::I3, 0x7ff0'0000'0000'0000U);
	UML_DMOVc(block, uml::COND_A, value, 0x7fff'ffff'ffff'ffffU);
	UML_DAND(block, uml::I3, value, 0x8000'0000'0000'0000U);
	UML_DTEST(block, value, 0x7ff0'0000'0000'0000U);
	UML_DMOVc(block, uml::COND_Z, value, uml::I3);
}

void tms320c674x_device::drc_address(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i, u32 shift)
{
	using namespace uml;

	// address into I0, updated base register value into I1
	uml::code_label const circular = c.label();
	uml::code_label const done = c.label();
	u32 const n = i.base & 31;
	bool const may_be_circular = (n >= 4) && (n <= 7);
	if (may_be_circular)
	{
		UML_TEST(block, mem(&m_core->amr), 3U << (((i.base & 32) ? 8 : 0) + (n - 4) * 2));
		UML_JMPc(block, COND_NZ, circular);
	}

	uml::parameter offset = u32(i.scaled ? (u32(i.cst) << shift) : u32(i.cst));
	if (i.offset_reg)
	{
		if (i.scaled && shift)
		{
			UML_SHL(block, I2, drc_reg(i.a), shift);
			offset = I2;
		}
		else
		{
			offset = drc_reg(i.a);
		}
	}

	switch (i.mode)
	{
	case dasm::AM_NEG_OFFSET:
		UML_SUB(block, I0, drc_reg(i.base), offset);
		break;
	case dasm::AM_POS_OFFSET:
		UML_ADD(block, I0, drc_reg(i.base), offset);
		break;
	case dasm::AM_PREDEC:
		UML_SUB(block, I0, drc_reg(i.base), offset);
		UML_MOV(block, I1, I0);
		break;
	case dasm::AM_PREINC:
		UML_ADD(block, I0, drc_reg(i.base), offset);
		UML_MOV(block, I1, I0);
		break;
	case dasm::AM_POSTDEC:
		UML_MOV(block, I0, drc_reg(i.base));
		UML_SUB(block, I1, I0, offset);
		break;
	case dasm::AM_POSTINC:
		UML_MOV(block, I0, drc_reg(i.base));
		UML_ADD(block, I1, I0, offset);
		break;
	default:
		UML_MOV(block, I0, drc_reg(i.base));
		break;
	}

	if (may_be_circular)
	{
		drc_address_call *const call = m_drcuml->cache_alloc_transient<drc_address_call>();
		if (!call)
			block.abort();
		call->cpu = this;
		call->i = i;
		call->shift = shift;

		UML_JMP(block, done);
		UML_LABEL(block, circular);
		UML_CALLC(block, &drc_funcs::address, call);
		UML_MOV(block, I0, mem(&m_core->drc_arg0));
		UML_MOV(block, I1, mem(&m_core->drc_arg1));
		UML_LABEL(block, done);
	}
}

void tms320c674x_device::drc_load(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i)
{
	using namespace uml;

	u32 const shift =
			((i.op == dasm::OP_LDB) || (i.op == dasm::OP_LDBU)) ? 0 :
			((i.op == dasm::OP_LDH) || (i.op == dasm::OP_LDHU)) ? 1 :
			((i.op == dasm::OP_LDDW) || (i.op == dasm::OP_LDNDW)) ? 3 : 2;
	drc_address(block, c, p, i, shift);
	if (i.mode >= dasm::AM_PREDEC)
		UML_MOV(block, drc_dest(block, c, p, i.base, 0), I1);

	u32 const d = i.dst;
	switch (i.op)
	{
	case dasm::OP_LDB: case dasm::OP_LDBU:
		UML_CALLH(block, *m_read8);
		if (i.op == dasm::OP_LDB)
			UML_SEXT(block, I1, I1, SIZE_BYTE);
		UML_MOV(block, drc_dest(block, c, p, d, 4), I1);
		break;
	case dasm::OP_LDH: case dasm::OP_LDHU:
		UML_AND(block, I0, I0, ~1U);
		UML_CALLH(block, *m_read16);
		if (i.op == dasm::OP_LDH)
			UML_SEXT(block, I1, I1, SIZE_WORD);
		UML_MOV(block, drc_dest(block, c, p, d, 4), I1);
		break;
	case dasm::OP_LDW:
		UML_AND(block, I0, I0, ~3U);
		UML_CALLH(block, *m_read32);
		UML_MOV(block, drc_dest(block, c, p, d, 4), I1);
		break;
	case dasm::OP_LDNW:
		UML_CALLH(block, *m_read32u);
		UML_MOV(block, drc_dest(block, c, p, d, 4), I1);
		break;
	case dasm::OP_LDDW: case dasm::OP_LDNDW:
		if (i.op == dasm::OP_LDDW)
			UML_AND(block, I0, I0, ~7U);
		UML_CALLH(block, (i.op == dasm::OP_LDDW) ? *m_read32 : *m_read32u);
		UML_MOV(block, drc_dest(block, c, p, d & ~1U, 4), I1);
		UML_ADD(block, I0, I0, 4);
		UML_CALLH(block, (i.op == dasm::OP_LDDW) ? *m_read32 : *m_read32u);
		UML_MOV(block, drc_dest(block, c, p, d | 1U, 4), I1);
		break;
	default:
		break;
	}

	// the access may have been to a peripheral
	c.irq_dirty = true;
}

void tms320c674x_device::drc_store(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i)
{
	using namespace uml;

	u32 const shift =
			(i.op == dasm::OP_STB) ? 0 :
			(i.op == dasm::OP_STH) ? 1 :
			((i.op == dasm::OP_STDW) || (i.op == dasm::OP_STNDW)) ? 3 : 2;
	drc_address(block, c, p, i, shift);

	// address and data are read now, memory is written at the end of the packet
	drc_pending_store s;
	s.size = 1 << shift;
	s.unaligned = (i.op == dasm::OP_STNW) || (i.op == dasm::OP_STNDW);
	s.pred = p.pred;
	s.addr = c.tmp();
	s.data_lo = c.tmp();
	s.data_hi = c.tmp();
	UML_MOV(block, mem(&m_core->drc_tmp[s.addr]), I0);
	if (shift == 3)
	{
		UML_MOV(block, mem(&m_core->drc_tmp[s.data_lo]), drc_reg(i.dst & ~1U));
		UML_MOV(block, mem(&m_core->drc_tmp[s.data_hi]), drc_reg(i.dst | 1U));
	}
	else
	{
		UML_MOV(block, mem(&m_core->drc_tmp[s.data_lo]), drc_reg(i.dst));
	}
	p.stores.push_back(s);

	if (i.mode >= dasm::AM_PREDEC)
		UML_MOV(block, drc_dest(block, c, p, i.base, 0), I1);

	c.irq_dirty = true;
}

void tms320c674x_device::drc_branch(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i, u32 next_pc)
{
	using namespace uml;

	// the condition is tested when the branch is issued
	bool const counted = (i.op == dasm::OP_BDEC) || (i.op == dasm::OP_BPOS);
	s16 pred = -1;
	if (i.creg || counted)
	{
		pred = c.pred();
		if (counted)
		{
			UML_CMP(block, drc_reg(i.dst), 0);
			UML_SETc(block, COND_GE, mem(&m_core->drc_pred[pred]));
			if (i.creg)
			{
				UML_CMP(block, drc_reg(i.creg - 1), 0);
				UML_SETc(block, i.z ? COND_E : COND_NE, I0);
				UML_AND(block, mem(&m_core->drc_pred[pred]), mem(&m_core->drc_pred[pred]), I0);
			}
		}
		else
		{
			UML_CMP(block, drc_reg(i.creg - 1), 0);
			UML_SETc(block, i.z ? COND_E : COND_NE, mem(&m_core->drc_pred[pred]));
		}
	}

	drc_flight b;
	b.dynamic = false;
	b.pred = pred;
	b.slot = c.slot();
	b.target = i.cst;
	b.landing = c.cycle + 6;

	uml::code_label const skip = c.label();
	if (pred >= 0)
	{
		UML_CMP(block, mem(&m_core->drc_pred[pred]), 0);
		UML_JMPc(block, COND_E, skip);
	}
	p.pred = pred;

	switch (i.op)
	{
	case dasm::OP_B_REG: case dasm::OP_BNOP_REG:
		b.dynamic = true;
		UML_MOV(block, mem(&m_core->drc_btarget[b.slot]), drc_reg(i.b));
		break;
	case dasm::OP_B_IRP:
		b.dynamic = true;
		UML_MOV(block, mem(&m_core->drc_btarget[b.slot]), mem(&m_core->irp));
		UML_CALLC(block, &drc_funcs::b_irp, this);
		c.irq_dirty = true;
		break;
	case dasm::OP_B_NRP:
		b.dynamic = true;
		UML_MOV(block, mem(&m_core->drc_btarget[b.slot]), mem(&m_core->nrp));
		UML_CALLC(block, &drc_funcs::b_nrp, this);
		c.irq_dirty = true;
		break;
	case dasm::OP_CALLP:
		UML_MOV(block, drc_dest(block, c, p, i.dst, 0), next_pc);
		break;
	case dasm::OP_BDEC:
		UML_SUB(block, drc_dest(block, c, p, i.dst, 0), drc_reg(i.dst), 1);
		break;
	default:
		break;
	}

	UML_LABEL(block, skip);
	c.branches.push_back(b);
}
