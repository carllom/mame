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
    runs what isn't compiled: IDLE, more branches in flight than it keeps
    track of, the rest of an execute packet the CPU was in the middle of
    when its time slice ended, and the rest of a software pipelined loop
    the recompiled code left (for an interrupt, for example).

    SPLOOP loops are compiled a cycle at a time with the interpreter's own
    loop buffer code; see SOFTWARE PIPELINED LOOPS below.

    Instructions without generated code are run by calling the
    interpreter. Their results go to the write queue.

***************************************************************************/

#include "emu.h"
#include "tms320c674x.h"
#include "tms320c674xlb.ipp"

#include "cpu/drcumlsh.h"

#include <algorithm>
#include <bitset>
#include <map>
#include <string>


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
	drc_lb_snapshot const *lb;  // loop buffer, if it's active
};

// the loop buffer for the interpreter when leaving a loop
struct tms320c674x_device::drc_lb_snapshot
{
	loop_buffer_state state;
	lb_entry const *entries;    // without the valid flags
	u64 valid;
	u64 old_valid;
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

	// a loop's code runs more than once, so temporaries in use are allocated by what's live
	bool live = false;
	bool full = false;
	u32 estimate = 0;       // more than the UML instructions generated so far
	std::map<std::string, lb_entry const *> layouts;

	void live_reset()
	{
		m_used_tmp.reset();
		m_used_pred.reset();
		m_used_slot = 0;
		for (drc_pending const &p : pending)
		{
			m_used_tmp.set(p.tmp);
			if (p.pred >= 0)
				m_used_pred.set(p.pred);
		}
		for (drc_flight const &b : branches)
		{
			if (b.pred >= 0)
				m_used_pred.set(b.pred);
			if (b.dynamic)
				m_used_slot |= 1U << b.slot;
		}
	}

	uml::code_label label() { return m_next_label++; }

	u8 slot()
	{
		if (live)
		{
			for (u8 n = 0; n < BRANCH_SLOTS; n++)
			{
				if (!BIT(m_used_slot, n))
				{
					m_used_slot |= 1U << n;
					return n;
				}
			}
			full = true;
			return 0;
		}
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
		if (live)
			return u16(allocate(m_used_tmp));
		u16 const result = m_next_tmp;
		m_next_tmp = (m_next_tmp + 1) % DRC_TEMPS;
		return result;
	}

	s16 pred()
	{
		if (live)
			return s16(allocate(m_used_pred));
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
	u32 allocate(std::bitset<DRC_TEMPS> &used)
	{
		for (u32 n = 0; n < DRC_TEMPS; n++)
		{
			if (!used[n])
			{
				used.set(n);
				return n;
			}
		}
		full = true;
		return 0;
	}

	uml::code_label m_next_label = 1;
	u8 m_next_slot = 0;
	u16 m_next_tmp = 0;
	s16 m_next_pred = 0;
	std::bitset<DRC_TEMPS> m_used_tmp;
	std::bitset<DRC_TEMPS> m_used_pred;
	u32 m_used_slot = 0;
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

		if (e.lb)
		{
			loop_buffer &lb = cpu.m_lb;
			static_cast<loop_buffer_state &>(lb) = e.lb->state;
			lb.cond_history = u8(core.drc_lb_hist);
			for (u32 n = 0; n < lb.count; n++)
			{
				lb.entry[n] = e.lb->entries[n];
				lb.entry[n].valid = BIT(e.lb->valid, n);
				lb.entry[n].old_valid = BIT(e.lb->old_valid, n);
			}
			cpu.lb_decode(lb);
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
	return !m_core->idle && !m_core->nop_cycles && !m_core->branches && !m_lb.active;
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

bool tms320c674x_device::drc_has_sploop(ep_cache_entry const &e)
{
	for (u32 n = 0; n < e.count; n++)
	{
		if ((e.ep[n].op == dasm::OP_SPLOOP) || (e.ep[n].op == dasm::OP_SPLOOPD) || (e.ep[n].op == dasm::OP_SPLOOPW))
			return true;
	}
	return false;
}

void tms320c674x_device::drc_compile_block(u32 pc)
{
	// a software pipelined loop is compiled from its SPLOOP execute packet
	{
		ep_cache_entry e;
		decode_ep(pc, e);
		if (drc_has_sploop(e) && drc_compile_sploop(pc))
			return;
	}

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

				if (count && c.branches.empty() && drc_has_sploop(e))
				{
					// the loop has a block of its own
					drc_block_end(block, c, c.cycle, fetch);
					break;
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
	e->lb = nullptr;
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
//  SOFTWARE PIPELINED LOOPS
//**************************************************************************

/*
    A loop is compiled a cycle at a time by running the interpreter's
    loop buffer code on a copy of the loop buffer. Each cycle is a node;
    where the loop buffer depends on a run-time value (ILC, the SPLOOP
    condition, a branch's condition) the generated code tests it and
    continues with the node for each outcome. A cycle in the same state
    as an earlier one jumps to its code, so the kernel and an outer
    reload loop become loops in the generated code. Rare events (an
    interrupt, resuming an interrupted loop, RILC 0) leave before the
    cycle, and the interpreter carries on from the same state.
*/

namespace {

constexpr u32 MAX_LOOP_NODES = 20000;
constexpr u32 LOOP_BLOCK_SIZE = 262144;

// what the loop buffer changes elsewhere in a cycle
enum : u8
{
	LB_ILC_DEC,
	LB_ILC_FROM_RILC,
	LB_COND_RESET,
	LB_COND_SHIFT,
	LB_SPLX_SET,
	LB_SPLX_CLEAR
};

} // anonymous namespace

// a run-time value the loop buffer depends on
struct tms320c674x_device::drc_lb_decision
{
	enum : u8 { ILC_ZERO, COND, BRANCH };

	u8 kind;
	bool from_rilc;     // ILC_ZERO: ILC was loaded from RILC this cycle
	u8 sub;             // ILC_ZERO: decrements this cycle before the test; COND: history bit
	u8 shifts;          // COND: condition shifts this cycle before the test
	s16 pred;           // BRANCH: the branch's condition

	bool operator==(drc_lb_decision const &d) const
	{
		return (kind == d.kind) && (from_rilc == d.from_rilc) && (sub == d.sub) && (shifts == d.shifts) && (pred == d.pred);
	}
};

// a reason to let the interpreter run the cycle
struct tms320c674x_device::drc_lb_check
{
	enum : u8 { SPLX, RILC_ZERO, INTERRUPT };

	u8 kind;
	bool check_ilc;     // INTERRUPT: only with ILC at least the threshold
	bool from_rilc;
	u8 sub;
	u32 threshold;

	bool operator==(drc_lb_check const &c) const
	{
		return (kind == c.kind) && (check_ilc == c.check_ilc) && (from_rilc == c.from_rilc) && (sub == c.sub) && (threshold == c.threshold);
	}
};

// the loop buffer's environment while compiling: decisions come from a list, everything else is recorded
struct tms320c674x_device::drc_lb_env
{
	std::vector<bool> const &answers;
	std::vector<drc_lb_decision> decisions;
	std::vector<drc_lb_check> checks;
	std::vector<u8> actions;
	bool from_rilc = false;
	u8 sub = 0;
	u8 shifts = 0;
	bool reset = false;
	bool failed = false;

	drc_lb_env(std::vector<bool> const &a) : answers(a) { }

	template <typename... Params> void log(Params &&... args) { }
	template <typename... Params> void error(Params &&... args) { failed = true; }
	u32 ilc() const { return 0; }

	bool ask(drc_lb_decision const &d)
	{
		size_t const n = decisions.size();
		decisions.push_back(d);
		return (n < answers.size()) && answers[n];
	}

	bool splx() { checks.push_back(drc_lb_check{ drc_lb_check::SPLX, false, false, 0, 0 }); return false; }
	void set_splx(bool state) { actions.push_back(state ? LB_SPLX_SET : LB_SPLX_CLEAR); }
	bool ilc_zero() { return ask(drc_lb_decision{ drc_lb_decision::ILC_ZERO, from_rilc, sub, 0, -1 }); }
	void ilc_decrement() { actions.push_back(LB_ILC_DEC); sub++; }
	bool rilc_zero() { checks.push_back(drc_lb_check{ drc_lb_check::RILC_ZERO, false, false, 0, 0 }); return false; }
	void ilc_from_rilc() { actions.push_back(LB_ILC_FROM_RILC); from_rilc = true; sub = 0; }

	void cond_reset(loop_buffer &lb) { actions.push_back(LB_COND_RESET); reset = true; }
	void cond_shift(loop_buffer &lb) { actions.push_back(LB_COND_SHIFT); shifts++; failed = failed || (shifts > 1); }
	bool cond_bit(loop_buffer const &lb, u32 bit)
	{
		failed = failed || reset;
		return ask(drc_lb_decision{ drc_lb_decision::COND, false, u8(bit), shifts, -1 });
	}

	bool interrupt_takeable(bool check_ilc, u32 threshold)
	{
		checks.push_back(drc_lb_check{ drc_lb_check::INTERRUPT, check_ilc, from_rilc, sub, threshold });
		return false;
	}
	void take_pending_interrupt(u32 address) { failed = true; }
};

// the state at the start of a cycle in a loop
struct tms320c674x_device::drc_lb_state
{
	loop_buffer lb;
	u32 pc = 0;
	s16 pc_slot = -1;       // the PC is a branch target in drc_btarget
	u32 nop_cycles = 0;
	std::vector<drc_pending> pending;   // block cycles relative to this one
	std::vector<drc_flight> branches;

	bool clean() const { return !lb.active && !nop_cycles && branches.empty(); }

	std::string key() const
	{
		// everything that decides what happens next; the condition history and ILC are in the CPU
		std::string k;
		auto const put = [&k] (auto const &v) { k.append(reinterpret_cast<char const *>(&v), sizeof(v)); };
		loop_buffer_state s = lb;
		s.cond_history = 0;
		s.cycles = std::min<u32>(s.cycles, 4); // only the first cycles matter
		put(s.count); put(s.active); put(s.loading); put(s.draining); put(s.fetch); put(s.kind); put(s.ii); put(s.lbc);
		put(s.loadcnt); put(s.dynlen); put(s.drain); put(s.delay); put(s.stgcyc); put(s.cycles); put(s.kernel); put(s.initial_term);
		put(s.creg); put(s.z); put(s.address); put(s.int_drain); put(s.restart); put(s.kernelr); put(s.reload_pending);
		put(s.reload_start); put(s.reload_branch); put(s.reloading); put(s.reloadcnt); put(s.old_active); put(s.old_lbc); put(s.old_drain);
		for (u32 n = 0; n < lb.count; n++)
		{
			lb_entry const &e = lb.entry[n];
			put(e.opcode); put(e.header); put(e.pce1); put(e.size); put(e.slot); put(e.loadcnt); put(e.valid); put(e.old_valid);
		}
		put(pc);
		put(nop_cycles);

		// conditions shared by several results or branches must stay shared
		std::vector<s16> preds;
		auto const pred = [&preds] (s16 p) -> s16
		{
			if (p < 0)
				return -1;
			auto const it = std::find(preds.begin(), preds.end(), p);
			if (it != preds.end())
				return s16(it - preds.begin());
			preds.push_back(p);
			return s16(preds.size() - 1);
		};
		put(u32(pending.size()));
		for (drc_pending const &p : pending)
		{
			put(p.reg); put(p.cycle); put(pred(p.pred));
		}
		put(u32(branches.size()));
		for (drc_flight const &b : branches)
		{
			put(b.dynamic); put(b.target); put(b.landing); put(pred(b.pred));
		}
		return k;
	}
};

// what a cycle runs, the same whatever the loop buffer decides
struct tms320c674x_device::drc_lb_plan
{
	bool fetched = false;
	bool splx_clear = false;    // TSR.SPLX clears unless a branch is in flight
	u32 next = 0;               // the execute packet's next PC
	std::vector<insn> insns;
	std::vector<u32> pce1;
};

// the cycles of a loop compiled so far
struct tms320c674x_device::drc_lb_graph
{
	struct node
	{
		uml::code_label label;
		drc_lb_state state;
	};

	std::vector<node> nodes;
	std::map<std::string, u32> index;
	std::vector<u32> work;
};

void tms320c674x_device::drc_lb_cycle(drc_lb_state const &s, drc_lb_env &env, drc_lb_state &next, drc_lb_plan &plan, std::vector<u32> &landed, drc_compiler &c)
{
	// one cycle of step() with the loop buffer active
	next.lb = s.lb;
	next.pc = s.pc;
	next.pc_slot = -1;
	next.nop_cycles = s.nop_cycles;
	loop_buffer &lb = next.lb;

	if (lb.reload_start)
		sploop_reload(lb, env);

	ep_cache_entry e;
	e.count = 0;
	u32 nextpc = s.pc;
	if (next.nop_cycles)
	{
		next.nop_cycles--;
	}
	else if (!lb.active || lb.fetch)
	{
		decode_ep(s.pc, e);
		c.add_page(e.gen[0], e.gen_value[0]);
		c.add_page(e.gen[1], e.gen_value[1]);
		nextpc = e.next;
		plan.fetched = true;
	}

	u32 mask;
	bool kernel, restart;
	lb_packet(lb, env, e.ep, e.count, mask, kernel, restart);

	bool const lb_was_active = lb.active;
	bool const loading = lb.active && lb.loading;
	bool const resuming = loading && lb.restart;
	u8 exec[LB_SIZE];
	u32 const lb_count = lb.active ? lb_select(lb, mask, resuming, exec) : 0;

	u32 nops = 0;
	bool ep_sploop = false;
	for (u32 n = 0; n < e.count; n++)
	{
		insn const &i = e.ep[n];
		int const action = lb_pm_action(lb, i, mask, restart, loading, resuming);
		if (action == PM_SKIP)
			continue;
		if (action == PM_SKIP_NOPS)
		{
			nops = std::max(nops, insn_nops(i));
			continue;
		}
		switch (i.op)
		{
		case dasm::OP_SPLOOP: case dasm::OP_SPLOOPD: case dasm::OP_SPLOOPW:
			sploop_start(lb, env, i, s.pc);
			ep_sploop = true;
			break;
		case dasm::OP_SPMASK: case dasm::OP_SPMASKR: case dasm::OP_SPKERNEL: case dasm::OP_SPKERNELR: case dasm::OP_NOP:
			break;
		default:
			plan.insns.push_back(i);
			plan.pce1.push_back(e.pce1[n]);
			break;
		}
		nops = std::max(nops, drc_ep_nops(i));
	}
	for (u32 n = 0; n < lb_count; n++)
	{
		plan.insns.push_back(lb.decoded[exec[n]]);
		plan.pce1.push_back(lb.entry[exec[n]].pce1);
	}

	if (loading && plan.fetched && !ep_sploop)
		sploop_load(lb, env, e.ep, e.raw, e.hdr, e.pce1, e.count, mask, s.pc);

	plan.next = nextpc;
	if (plan.fetched)
	{
		plan.splx_clear = !lb.active && !restart;
		next.pc = nextpc;
		next.nop_cycles = nops;
	}

	if (lb_was_active && lb.active)
		lb_end_packet(lb, env, kernel);

	// branches taking effect at the end of the cycle
	for (u32 n = 0; n < s.branches.size(); n++)
	{
		drc_flight const &b = s.branches[n];
		if (b.landing != 1)
			continue;
		if ((b.pred >= 0) && !env.ask(drc_lb_decision{ drc_lb_decision::BRANCH, false, 0, 0, b.pred }))
			continue;
		landed.push_back(n);
		next.pc = b.target;
		next.pc_slot = b.dynamic ? b.slot : -1;
		next.nop_cycles = 0;
		if (lb.active)
			lb_branch_landed(lb, env);
	}
}

tms320c674x_device::drc_exit *tms320c674x_device::drc_lb_exit(drcuml_block &block, drc_compiler &c, drc_lb_state const &s, std::vector<drc_flight> const &branches)
{
	// the state at the start of a cycle, for the interpreter
	std::vector<drc_pending> const pending = std::exchange(c.pending, s.pending);
	std::vector<drc_flight> const flights = std::exchange(c.branches, branches);
	drc_exit *const exit = drc_make_exit(block, c, 0, s.pc, s.nop_cycles, 0);
	c.pending = pending;
	c.branches = flights;
	exit->drain_from = 0;

	if (s.lb.active)
	{
		std::string layout(reinterpret_cast<char const *>(s.lb.entry), sizeof(lb_entry) * s.lb.count);
		auto it = c.layouts.find(layout);
		if (it == c.layouts.end())
		{
			lb_entry *const entries = m_drcuml->cache_alloc_transient<lb_entry []>(std::max<u32>(s.lb.count, 1));
			if (!entries)
				block.abort();
			std::copy_n(s.lb.entry, s.lb.count, entries);
			it = c.layouts.emplace(std::move(layout), entries).first;
		}

		drc_lb_snapshot *const snapshot = m_drcuml->cache_alloc_transient<drc_lb_snapshot>();
		if (!snapshot)
			block.abort();
		snapshot->state = s.lb;
		snapshot->entries = it->second;
		snapshot->valid = 0;
		snapshot->old_valid = 0;
		for (u32 n = 0; n < s.lb.count; n++)
		{
			snapshot->valid |= u64(s.lb.entry[n].valid ? 1 : 0) << n;
			snapshot->old_valid |= u64(s.lb.entry[n].old_valid ? 1 : 0) << n;
		}
		exit->lb = snapshot;
	}
	return exit;
}

bool tms320c674x_device::drc_compile_sploop(u32 pc)
{
	auto profile = g_profiler.start(PROFILER_DRC_COMPILE);

	for (;;)
	{
		bool failed = false;
		try
		{
			drc_compiler c;
			c.live = true;
			drcuml_block &block(m_drcuml->begin_block(LOOP_BLOCK_SIZE));
			uml::code_label const check = c.label();
			uml::code_label const body = c.label();
			uml::code_label const recompile = c.label();

			UML_HASH(block, 0, pc);
			UML_JMP(block, check);
			UML_LABEL(block, body);
			UML_DMOV(block, uml::mem(&m_core->drc_base), uml::mem(&m_core->cycle));

			// interrupts are taken before the SPLOOP execute packet, as at the start of any block
			drc_irq_check(block, c, pc);

			drc_lb_graph g;
			drc_lb_state start;
			start.pc = pc;
			g.nodes.push_back(drc_lb_graph::node{ c.label(), start });
			g.work.push_back(0);
			while (!g.work.empty())
			{
				if ((g.nodes.size() > MAX_LOOP_NODES) || (c.estimate > (LOOP_BLOCK_SIZE - (LOOP_BLOCK_SIZE / 16) - (c.pages.size() * 3))))
					failed = true;
				else
				{
					u32 const n = g.work.front();
					g.work.erase(g.work.begin());
					failed = !drc_lb_node(block, c, g, n);
				}
				if (failed)
					block.abort();
			}

			// leaving the loop
			for (drc_compiler::stub const &s : c.stubs)
			{
				UML_LABEL(block, s.label);
				if (s.irq)
				{
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
			return true;
		}
		catch (drcuml_block::abort_compilation &)
		{
			// a loop that can't be compiled is left to the interpreter
			if (failed)
				return false;
			drc_flush_cache();
		}
	}
}

bool tms320c674x_device::drc_lb_node(drcuml_block &block, drc_compiler &c, drc_lb_graph &g, u32 index)
{
	using namespace uml;

	drc_lb_state const s = g.nodes[index].state;
	UML_LABEL(block, g.nodes[index].label);
	c.pending = s.pending;
	c.branches = s.branches;
	c.cycle = 0;
	c.live_reset();
	c.estimate += 50 + (3 * c.pending.size());
	// out of cycles, or the interpreter has to run this cycle
	drc_exit *const exit = drc_lb_exit(block, c, s, s.branches);
	code_label const out = drc_exit_stub(block, c, exit, EXECUTE_OUT_OF_CYCLES);
	code_label const leave = drc_exit_stub(block, c, exit, EXECUTE_INTERPRET);
	UML_CMP(block, mem(&m_core->icount), 0);
	UML_JMPc(block, COND_LE, out);

	// every outcome of what the loop buffer depends on
	struct leaf
	{
		std::vector<bool> answers;
		std::vector<drc_lb_decision> decisions;
		std::vector<drc_lb_check> checks;
		std::vector<u8> actions;
		drc_lb_state next;
		std::vector<u32> landed;
	};
	std::vector<leaf> leaves;
	drc_lb_plan plan;
	std::vector<bool> answers;
	for (;;)
	{
		drc_lb_env env(answers);
		leaf l;
		drc_lb_plan p;
		drc_lb_cycle(s, env, l.next, p, l.landed, c);
		if (env.failed)
			return false;
		if (leaves.empty())
			plan = p;
		else if ((p.insns.size() != plan.insns.size()) || (p.fetched != plan.fetched) || (p.splx_clear != plan.splx_clear) || (p.pce1 != plan.pce1))
			return false; // the instructions don't depend on the outcome
		l.answers = answers;
		l.answers.resize(env.decisions.size(), false);
		l.decisions = std::move(env.decisions);
		l.checks = std::move(env.checks);
		l.actions = std::move(env.actions);
		leaves.push_back(std::move(l));

		// the next combination of outcomes
		std::vector<bool> const &last = leaves.back().answers;
		size_t n = last.size();
		while (n && last[n - 1])
			n--;
		if (!n)
			break;
		answers.assign(last.begin(), last.begin() + n);
		answers.back() = true;
	}

	for (insn const &i : plan.insns)
	{
		// the generated code changes ILC and TSR after the instructions
		if ((i.op == dasm::OP_MVC_FROM) && ((i.cst == 0x0d) || (i.cst == 0x0e) || (i.cst == 0x1a)))
			return false;
		if ((i.op == dasm::OP_IDLE) || (i.op == dasm::OP_B_IRP) || (i.op == dasm::OP_B_NRP) || (i.op == dasm::OP_INVALID) || (i.op == dasm::OP_SWE) || (i.op == dasm::OP_SWENR))
			return false;
	}

	// queued writes due now
	drc_drain *const d = m_drcuml->cache_alloc_transient<drc_drain>();
	if (!d)
		block.abort();
	d->cpu = this;
	d->from = 0;
	d->to = 0;
	code_label const drained = c.label();
	UML_CMP(block, mem(&m_core->wb_total), 0);
	UML_JMPc(block, COND_E, drained);
	UML_CALLC(block, &drc_funcs::drain, d);
	UML_LABEL(block, drained);
	drc_commit(block, c, 0);

	// leave if the interpreter has to run this cycle
	std::vector<drc_lb_check> checks;
	for (leaf const &l : leaves)
	{
		for (drc_lb_check const &k : l.checks)
		{
			if (std::find(checks.begin(), checks.end(), k) == checks.end())
				checks.push_back(k);
		}
	}
	for (drc_lb_check const &k : checks)
	{
		switch (k.kind)
		{
		case drc_lb_check::SPLX:
			UML_TEST(block, mem(&m_core->tsr), TSR_SPLX);
			UML_JMPc(block, COND_NZ, leave);
			break;
		case drc_lb_check::RILC_ZERO:
			UML_CMP(block, mem(&m_core->rilc), 0);
			UML_JMPc(block, COND_E, leave);
			break;
		case drc_lb_check::INTERRUPT:
			{
				code_label const skip = c.label();
				UML_CMP(block, mem(&m_core->irq_poll), 0);
				UML_JMPc(block, COND_E, skip);
				if (k.check_ilc)
				{
					UML_SUB(block, I0, mem(k.from_rilc ? &m_core->rilc : &m_core->ilc), k.sub + (k.from_rilc ? 1 : 0));
					UML_CMP(block, I0, k.threshold);
					UML_JMPc(block, COND_B, skip);
				}
				UML_CALLC(block, &drc_funcs::poll_irq, this);
				UML_CMP(block, mem(&m_core->drc_arg0), 0);
				UML_JMPc(block, COND_NE, leave);
				UML_LABEL(block, skip);
			}
			break;
		}
	}

	// the values decided on, read before the instructions write
	bool const shifts = std::any_of(leaves.begin(), leaves.end(), [] (leaf const &l) { return std::find(l.actions.begin(), l.actions.end(), LB_COND_SHIFT) != l.actions.end(); });
	s16 cond_now = -1;
	if (shifts && s.lb.creg)
	{
		cond_now = c.pred();
		UML_CMP(block, drc_reg(s.lb.creg - 1), 0);
		UML_SETc(block, s.lb.z ? COND_E : COND_NE, mem(&m_core->drc_pred[cond_now]));
	}
	std::vector<std::pair<drc_lb_decision, s16> > values;
	for (leaf const &l : leaves)
	{
		for (drc_lb_decision const &k : l.decisions)
		{
			if (std::find_if(values.begin(), values.end(), [&k] (auto const &v) { return v.first == k; }) != values.end())
				continue;
			s16 p = k.pred;
			switch (k.kind)
			{
			case drc_lb_decision::ILC_ZERO:
				p = c.pred();
				UML_CMP(block, mem(k.from_rilc ? &m_core->rilc : &m_core->ilc), k.sub + (k.from_rilc ? 1 : 0));
				UML_SETc(block, COND_E, mem(&m_core->drc_pred[p]));
				break;
			case drc_lb_decision::COND:
				p = c.pred();
				if (k.shifts && !k.sub)
				{
					if (cond_now >= 0)
						UML_MOV(block, mem(&m_core->drc_pred[p]), mem(&m_core->drc_pred[cond_now]));
					else
						UML_MOV(block, mem(&m_core->drc_pred[p]), 1);
				}
				else
				{
					UML_TEST(block, mem(&m_core->drc_lb_hist), 1U << (k.sub - (k.shifts ? 1 : 0)));
					UML_SETc(block, COND_NZ, mem(&m_core->drc_pred[p]));
				}
				break;
			default:
				break;
			}
			values.emplace_back(k, p);
		}
	}

	// the instructions, at most about 60 UML instructions each
	c.estimate += (60 * plan.insns.size()) + (12 * checks.size()) + (6 * values.size());
	drc_packet_list(block, c, plan.insns.data(), plan.pce1.data(), plan.insns.size(), plan.next, s.pc);
	if (c.full)
		return false;
	if (plan.splx_clear)
	{
		code_label const skip = c.label();
		drc_test_branches(block, c, skip);
		UML_AND(block, mem(&m_core->tsr), mem(&m_core->tsr), ~TSR_SPLX);
		UML_LABEL(block, skip);
	}
	UML_SUB(block, mem(&m_core->icount), mem(&m_core->icount), 1);
	UML_DADD(block, mem(&m_core->drc_base), mem(&m_core->drc_base), 1);

	// on to the next cycle, by the outcome
	std::vector<drc_pending> pending = c.pending;
	for (drc_pending &p : pending)
		p.cycle--;
	auto const value = [&values] (drc_lb_decision const &k) { return std::find_if(values.begin(), values.end(), [&k] (auto const &v) { return v.first == k; })->second; };
	auto const emit = [&] (auto &self, u32 first, u32 last, u32 depth) -> bool
	{
		leaf const &l = leaves[first];
		if (depth == l.answers.size())
			return drc_lb_successor(block, c, g, l.next, l.actions, l.landed, pending, cond_now);

		// leaves are in order, with the outcomes false before true
		u32 split = first;
		while ((split < last) && !leaves[split].answers[depth])
			split++;
		code_label const taken = c.label();
		UML_CMP(block, mem(&m_core->drc_pred[value(l.decisions[depth])]), 0);
		UML_JMPc(block, COND_NE, taken);
		if ((split > first) && !self(self, first, split, depth + 1))
			return false;
		UML_LABEL(block, taken);
		return (split < last) ? self(self, split, last, depth + 1) : true;
	};
	return emit(emit, 0, leaves.size(), 0);
}

bool tms320c674x_device::drc_lb_successor(drcuml_block &block, drc_compiler &c, drc_lb_graph &g, drc_lb_state next, std::vector<u8> const &actions, std::vector<u32> const &landed, std::vector<drc_pending> const &pending, s16 cond_now)
{
	using namespace uml;

	c.estimate += 30 + (4 * actions.size()) + (4 * (pending.size() + c.branches.size()));

	// what the loop buffer changes
	for (u8 a : actions)
	{
		switch (a)
		{
		case LB_ILC_DEC:
			UML_SUB(block, mem(&m_core->ilc), mem(&m_core->ilc), 1);
			break;
		case LB_ILC_FROM_RILC:
			UML_SUB(block, mem(&m_core->ilc), mem(&m_core->rilc), 1);
			break;
		case LB_COND_RESET:
			UML_MOV(block, mem(&m_core->drc_lb_hist), 0);
			break;
		case LB_COND_SHIFT:
			UML_SHL(block, I0, mem(&m_core->drc_lb_hist), 1);
			if (cond_now >= 0)
				UML_OR(block, I0, I0, mem(&m_core->drc_pred[cond_now]));
			else
				UML_OR(block, I0, I0, 1);
			UML_AND(block, mem(&m_core->drc_lb_hist), I0, 0xff);
			break;
		case LB_SPLX_SET:
			UML_OR(block, mem(&m_core->tsr), mem(&m_core->tsr), TSR_SPLX);
			break;
		case LB_SPLX_CLEAR:
			UML_AND(block, mem(&m_core->tsr), mem(&m_core->tsr), ~TSR_SPLX);
			break;
		}
	}

	// results and branches still in flight
	next.pending = pending;
	next.branches.clear();
	for (u32 n = 0; n < c.branches.size(); n++)
	{
		if (std::find(landed.begin(), landed.end(), n) == landed.end())
		{
			drc_flight b = c.branches[n];
			if (b.landing > 1)
			{
				b.landing--;
				next.branches.push_back(b);
			}
		}
	}

	if (next.clean())
	{
		// out of the loop with no branch in flight: an ordinary block follows
		drc_lb_state done = next;
		done.pc_slot = -1;
		drc_exit *const exit = drc_lb_exit(block, c, done, std::vector<drc_flight>());
		exit->next_pc = next.pc;
		UML_CALLC(block, &drc_funcs::leave, exit);
		if (next.pc_slot >= 0)
			UML_HASHJMP(block, 0, mem(&m_core->drc_btarget[next.pc_slot]), *m_nocode);
		else
			UML_HASHJMP(block, 0, next.pc, *m_nocode);
		return true;
	}

	if (next.pc_slot >= 0)
	{
		// a branch to a register target: the interpreter carries on
		std::vector<drc_flight> branches = next.branches;
		branches.insert(branches.begin(), drc_flight{ true, -1, u8(next.pc_slot), 0, 0 });
		drc_lb_state const at = next;
		drc_exit *const exit = drc_lb_exit(block, c, at, branches);
		UML_CALLC(block, &drc_funcs::leave, exit);
		UML_EXIT(block, EXECUTE_CONTINUE);
		return true;
	}

	std::string const key = next.key();
	auto const it = g.index.find(key);
	if (it == g.index.end())
	{
		// a new state; its values stay where they are
		u32 const n = g.nodes.size();
		g.nodes.push_back(drc_lb_graph::node{ c.label(), next });
		g.index.emplace(key, n);
		g.work.push_back(n);
		UML_JMP(block, g.nodes[n].label);
		return true;
	}

	// a state seen before: move the values to where its code expects them
	drc_lb_state const &to = g.nodes[it->second].state;
	std::vector<std::pair<u32 *, u32 *> > moves;
	auto const move = [&moves] (u32 *src, u32 *dst)
	{
		if ((src != dst) && (std::find_if(moves.begin(), moves.end(), [dst] (auto const &m) { return m.second == dst; }) == moves.end()))
			moves.emplace_back(src, dst);
	};
	for (u32 n = 0; n < next.pending.size(); n++)
	{
		move(&m_core->drc_tmp[next.pending[n].tmp], &m_core->drc_tmp[to.pending[n].tmp]);
		if (next.pending[n].pred >= 0)
			move(&m_core->drc_pred[next.pending[n].pred], &m_core->drc_pred[to.pending[n].pred]);
	}
	for (u32 n = 0; n < next.branches.size(); n++)
	{
		if (next.branches[n].pred >= 0)
			move(&m_core->drc_pred[next.branches[n].pred], &m_core->drc_pred[to.branches[n].pred]);
		if (next.branches[n].dynamic)
			move(&m_core->drc_btarget[next.branches[n].slot], &m_core->drc_btarget[to.branches[n].slot]);
	}
	if (moves.size() > std::size(m_core->drc_shuffle))
		return false;
	bool const overlap = std::any_of(moves.begin(), moves.end(), [&moves] (auto const &m) { return std::find_if(moves.begin(), moves.end(), [&m] (auto const &o) { return o.first == m.second; }) != moves.end(); });
	for (u32 n = 0; n < moves.size(); n++)
		UML_MOV(block, mem(overlap ? &m_core->drc_shuffle[n] : moves[n].second), mem(moves[n].first));
	if (overlap)
	{
		for (u32 n = 0; n < moves.size(); n++)
			UML_MOV(block, mem(moves[n].second), mem(&m_core->drc_shuffle[n]));
	}
	UML_JMP(block, g.nodes[it->second].label);
	return true;
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
	drc_packet_list(block, c, e.ep, e.pce1, e.count, e.next, e.pc);
}

void tms320c674x_device::drc_packet_list(drcuml_block &block, drc_compiler &c, insn const *ep, u32 const *pce1, u32 count, u32 next_pc, u32 pc)
{
	// instructions in the cycle read their sources before any of them write
	std::vector<u64> later(count);
	u64 reads = 0;
	for (u32 n = count; n-- > 0; )
	{
		later[n] = reads;
		reads |= drc_reads(ep[n]);
	}

	drc_packet p;
	for (u32 n = 0; n < count; n++)
	{
		p.later_reads = later[n];
		p.pred = -1;
		if (!drc_insn(block, c, p, ep[n], pce1[n], next_pc))
			drc_fallback(block, c, ep[n], pce1[n], next_pc, pc);
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
