// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C674X_H
#define MAME_CPU_TMS320C6X_TMS320C674X_H

#pragma once

#include "tms320c6x_dasm.h"

#include "cpu/drcuml.h"

class tms320c674x_device : public cpu_device
{
public:
	// interrupt inputs: INT4-INT15 use input lines 4-15, NMI is INPUT_LINE_NMI

protected:
	enum
	{
		C6X_PC = 1,
		C6X_A0,
		C6X_B0 = C6X_A0 + 32,
		C6X_AMR = C6X_B0 + 32,
		C6X_CSR,
		C6X_IFR,
		C6X_IER,
		C6X_ISTP,
		C6X_IRP,
		C6X_NRP,
		C6X_TSR,
		C6X_ITSR,
		C6X_NTSR,
		C6X_EFR,
		C6X_IERR,
		C6X_ILC,
		C6X_RILC,
		C6X_REP,
		C6X_SSR,
		C6X_GFPGFR,
		C6X_GPLYA,
		C6X_GPLYB,
		C6X_FADCR,
		C6X_FAUCR,
		C6X_FMCR
	};

	tms320c674x_device(machine_config const &mconfig, device_type type, char const *tag, device_t *owner, u32 clock, address_map_constructor internal_map, u32 istp_reset, u8 revision);

	// latch an interrupt from the megamodule interrupt controller (INT4-INT15)
	void interrupt_pulse(u32 line);

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_post_load() override ATTR_COLD;

	// device_execute_interface implementation
	virtual u32 execute_min_cycles() const noexcept override;
	virtual u32 execute_max_cycles() const noexcept override;
	virtual bool execute_input_edge_triggered(int inputnum) const noexcept override;
	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override;

	// device_memory_interface implementation
	virtual space_config_vector memory_space_config() const override ATTR_COLD;

	// device_disasm_interface implementation
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	using dasm = tms320c6x_disassembler;
	using insn = tms320c6x_disassembler::insn;

	// register numbers for delayed writes beyond the register files
	static constexpr u8 REG_ILC = 64;
	static constexpr u8 REG_RILC = 65;

	static constexpr u32 WB_SLOTS = 16;   // longer than the longest delay
	static constexpr u32 WB_DEPTH = 32;
	static constexpr u32 MAX_EP = 8;      // instructions in one execute packet (SPRUFE8B 3.5)
	static constexpr u32 LB_SIZE = 64;    // loop buffer instructions
	static constexpr u32 BRANCH_SLOTS = 8;
	static constexpr u32 MAX_DELAY = 9;   // MPYDP, MPYID

	// recompiler
	static constexpr size_t DRC_CACHE_SIZE = 64 * 1024 * 1024;
	static constexpr u32 DRC_TEMPS = 512; // more than the results that can be in flight

	struct store
	{
		u32 address;
		u8 size;
		bool unaligned;
		u64 data;
	};

	struct ep_cache_entry
	{
		u32 pc;
		u32 next;
		u8 count;
		u32 const *gen[2];  // write generations of the first and last code page
		u32 gen_value[2];
		u32 raw[MAX_EP];
		u32 hdr[MAX_EP];
		u32 pce1[MAX_EP];
		insn ep[MAX_EP];
	};

	static constexpr u32 EP_CACHE_BITS = 13;
	static constexpr u32 EP_CACHE_SIZE = 1U << EP_CACHE_BITS;

	// writes are counted per code page to detect stale decoded code
	static constexpr u32 CODE_PAGE_SHIFT = 8;
	static constexpr u32 CODE_PAGES = 1U << (24 - CODE_PAGE_SHIFT); // per 16 MB region

	struct internal_state
	{
		// register files
		u32 pc = 0;
		alignas(8) u32 r[64] = { };

		// control register file
		u32 amr = 0;
		u32 csr = 0;
		u32 ifr = 0;
		u32 ier = 0;
		u32 istp = 0;
		u32 irp = 0;
		u32 nrp = 0;

		// C64x+ control register file extensions
		u32 tsr = 0;
		u32 itsr = 0;
		u32 ntsr = 0;
		u32 efr = 0;
		u32 ierr = 0;
		u32 ilc = 0;
		u32 rilc = 0;
		u32 rep = 0;
		u32 ssr = 0;
		u32 gfpgfr = 0;
		u32 gplya = 0;
		u32 gplyb = 0;

		// C67x floating-point control register file extensions
		u32 fadcr = 0;
		u32 faucr = 0;
		u32 fmcr = 0;

		// pipeline
		u64 cycle = 0;
		u32 wb_total = 0;   // queued writes in all slots
		u8 wb_reg[WB_SLOTS * WB_DEPTH] = { };
		u32 wb_value[WB_SLOTS * WB_DEPTH] = { };
		u8 wb_count[WB_SLOTS] = { };
		u64 branch_when[BRANCH_SLOTS] = { };
		u32 branch_target[BRANCH_SLOTS] = { };
		u8 branches = 0;
		u32 nop_cycles = 0;
		bool idle = false;
		u32 irq_poll = 0;   // something an interrupt depends on has changed

		int icount = 0;

		// recompiler
		u64 drc_base = 0;   // cycle at the start of the block
		u32 drc_btarget[BRANCH_SLOTS] = { };
		u32 drc_arg0 = 0;
		u32 drc_arg1 = 0;
		u32 drc_tmp[DRC_TEMPS] = { };   // results waiting for the end of their delay slots
		u32 drc_pred[DRC_TEMPS] = { };  // instruction conditions
		u32 drc_lb_hist = 0;            // SPLOOP condition history in a compiled loop
		u32 drc_shuffle[256] = { };     // for moving values between loop states
	};

	struct lb_entry
	{
		u32 opcode;
		u32 header;
		u32 pce1;
		u8 size;
		u8 slot;
		u8 loadcnt;
		bool valid;
		bool old_valid;    // for the previous invocation draining during a reload
	};

	struct loop_buffer_state
	{
		u8 count = 0;
		bool active = false;
		bool loading = false;
		bool draining = false;
		bool fetch = true;
		u8 kind = 0;            // OP_SPLOOP, OP_SPLOOPD or OP_SPLOOPW
		u8 ii = 1;
		u8 lbc = 0;
		u8 loadcnt = 0;
		u8 dynlen = 0;
		u8 drain = 0;
		u8 delay = 0;
		u8 stgcyc = 0;
		u32 cycles = 0;         // cycles since the SPLOOP instruction
		bool kernel = false;    // SPKERNEL seen
		bool initial_term = false;
		u8 creg = 0;
		bool z = false;
		u8 cond_history = 0;    // SPLOOPW condition for the last cycles, bit 0 = current
		u32 address = 0;
		bool int_drain = false; // draining to take an interrupt
		bool restart = false;   // restarted after an interrupt (TSR.SPLX was set)
		bool kernelr = false;   // loop body ended with SPKERNELR
		bool reload_pending = false; // reload condition true, waiting for the reload point
		bool reload_start = false;   // reload begins next cycle
		bool reload_branch = false;  // a taken branch disables fetch instead of idling the buffer
		bool reloading = false;
		u8 reloadcnt = 0;
		bool old_active = false; // previous invocation still draining during a reload
		u8 old_lbc = 0;
		u8 old_drain = 0;
	};

	struct loop_buffer : loop_buffer_state
	{
		lb_entry entry[LB_SIZE] = { };
		insn decoded[LB_SIZE];
	};

	// what happens to a program memory instruction while the loop buffer may be active
	enum
	{
		PM_EXECUTE,
		PM_SKIP,
		PM_SKIP_NOPS
	};

	// fetch and execute
	void step();
	ep_cache_entry const &fetch_ep(u32 pc);
	u32 decode_ep(u32 pc, ep_cache_entry &e);
	static u32 ep_index(u32 pc);
	static u32 code_region(u32 address);
	u32 const *code_gen(u32 address);
	void code_written(offs_t address);
	void flush_ep_cache();
	void execute(insn const &i, u32 pce1, u32 next_pc);
	void tick();
	bool condition(insn const &i) const;

	// register access
	u32 reg(u32 r) const { return m_core->r[r]; }
	u32 src_a(insn const &i) const { return (i.flags & dasm::F_A_CST) ? u32(i.cst) : m_core->r[i.a]; }
	u32 src_b(insn const &i) const { return (i.flags & dasm::F_B_CST) ? u32(i.cst) : m_core->r[i.b]; }
	u64 pair(u32 r) const { return (u64(m_core->r[r | 1]) << 32) | m_core->r[r & ~1]; }
	s64 long40(u32 r) const { return util::sext((u64(m_core->r[r | 1] & 0xff) << 32) | m_core->r[r & ~1], 40); }
	u64 ulong40(u32 r) const { return (u64(m_core->r[r | 1] & 0xff) << 32) | m_core->r[r & ~1]; }
	void write(u32 r, u32 value, u32 delay = 0);
	void write_pair(u32 r, u64 value, u32 delay = 0);
	void write_long(u32 r, u64 value, u32 delay = 0) { write_pair(r, value & 0xff'ffff'ffffU, delay); }
	void commit_writes(u32 slot);
	void flush_writes();
	void set_sat() { m_core->csr |= 1U << 9; }

	// control registers
	u32 read_control(u32 crlo, u32 pce1);
	void write_control(u32 crlo, u32 value);

	// memory
	u32 address(insn const &i, u32 shift);
	u32 address(insn const &i, u32 shift, u32 &base);
	u64 load(insn const &i, u32 addr);

	// branches and interrupts
	void branch(u32 target);
	int pending_interrupt() const;
	void check_interrupts();
	void take_interrupt(int number, u32 return_pc, bool splx);

	// loop buffer, shared with the recompiler (tms320c674xlb.ipp)
	struct lb_cpu_env;
	template <typename Env> void lb_packet(loop_buffer &lb, Env &env, insn const *ep, u32 count, u32 &mask, bool &kernel, bool &restart);
	static u32 lb_select(loop_buffer &lb, u32 mask, bool resuming, u8 *exec);
	static int lb_pm_action(loop_buffer const &lb, insn const &i, u32 mask, bool restart, bool loading, bool resuming);
	template <typename Env> void lb_end_packet(loop_buffer &lb, Env &env, bool kernel);
	template <typename Env> void lb_branch_landed(loop_buffer &lb, Env &env);
	template <typename Env> void sploop_start(loop_buffer &lb, Env &env, insn const &i, u32 address);
	template <typename Env> void sploop_load(loop_buffer &lb, Env &env, insn const *ep, u32 const *raw, u32 const *hdr, u32 const *pce1, u32 count, u32 mask, u32 pc);
	template <typename Env> void sploop_end_cycle(loop_buffer &lb, Env &env);
	template <typename Env> void sploop_stop(loop_buffer &lb, Env &env);
	template <typename Env> void sploop_reload(loop_buffer &lb, Env &env);
	template <typename Env> bool sploop_terminate(loop_buffer const &lb, Env &env);
	template <typename Env> bool sploop_reload_condition(loop_buffer const &lb, Env &env);
	template <typename Env> bool sploop_interruptible(loop_buffer const &lb, Env &env);
	static u32 unit_mask(u8 unit);
	static u32 insn_nops(insn const &i);
	static void lb_decode(loop_buffer &lb);

	// recompiler
	struct drc_funcs;
	struct drc_pending;
	struct drc_flight;
	struct drc_exit;
	struct drc_call;
	struct drc_address_call;
	struct drc_drain;
	struct drc_pending_store;
	struct drc_packet;
	struct drc_compiler;
	struct drc_lb_decision;
	struct drc_lb_check;
	struct drc_lb_env;
	struct drc_lb_state;
	struct drc_lb_snapshot;
	struct drc_lb_plan;
	struct drc_lb_graph;

	void execute_run_drc();
	bool drc_clean() const;
	void drc_flush_cache();
	void drc_generate_invariant();
	void drc_find_fast_ram();
	void drc_generate_accessors();
	void drc_accessor(drcuml_block &block, uml::code_label &label, uml::code_handle &handle, u32 bytes, bool write, bool unaligned);
	void drc_compile_block(u32 pc);
	bool drc_compilable(ep_cache_entry const &e, drc_compiler const &c) const;
	void drc_boundary(drcuml_block &block, drc_compiler &c, u32 cycle);
	void drc_commit(drcuml_block &block, drc_compiler &c, u32 cycle);
	drc_exit *drc_make_exit(drcuml_block &block, drc_compiler &c, u32 boundary, u32 next_pc, u32 nop_cycles, s32 icount_adjust);
	uml::code_label drc_exit_stub(drcuml_block &block, drc_compiler &c, drc_exit *exit, int code);
	void drc_irq_check(drcuml_block &block, drc_compiler &c, u32 pc);
	void drc_count_cycles(drcuml_block &block, drc_compiler &c, u32 cycles, u32 boundary, u32 next_pc, u32 nop_cycles, s32 icount_adjust);
	void drc_test_branches(drcuml_block &block, drc_compiler &c, uml::code_label taken);
	void drc_block_end(drcuml_block &block, drc_compiler &c, u32 boundary, uml::parameter target);
	void drc_packet_body(drcuml_block &block, drc_compiler &c, ep_cache_entry const &e);
	void drc_packet_list(drcuml_block &block, drc_compiler &c, insn const *ep, u32 const *pce1, u32 count, u32 next_pc, u32 pc);
	static bool drc_has_sploop(ep_cache_entry const &e);
	bool drc_compile_sploop(u32 pc);
	void drc_lb_cycle(drc_lb_state const &s, drc_lb_env &env, drc_lb_state &next, drc_lb_plan &plan, std::vector<u32> &landed, drc_compiler &c);
	drc_exit *drc_lb_exit(drcuml_block &block, drc_compiler &c, drc_lb_state const &s, std::vector<drc_flight> const &branches);
	bool drc_lb_node(drcuml_block &block, drc_compiler &c, drc_lb_graph &g, u32 index);
	bool drc_lb_successor(drcuml_block &block, drc_compiler &c, drc_lb_graph &g, drc_lb_state next, std::vector<u8> const &actions, std::vector<u32> const &landed, std::vector<drc_pending> const &pending, s16 cond_now);
	bool drc_insn(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i, u32 pce1, u32 next_pc);
	void drc_fallback(drcuml_block &block, drc_compiler &c, insn const &i, u32 pce1, u32 next_pc, u32 pc);
	uml::parameter drc_dest(drcuml_block &block, drc_compiler &c, drc_packet &p, u32 r, u32 delay);
	uml::parameter drc_reg(u32 r) const;
	uml::parameter drc_src_a(insn const &i) const;
	uml::parameter drc_src_b(insn const &i) const;
	void drc_dest_pair(drcuml_block &block, drc_compiler &c, drc_packet &p, u32 r, u32 delay, uml::parameter value);
	void drc_sp_in(drcuml_block &block, uml::parameter dst, uml::parameter src);
	void drc_sp_out(drcuml_block &block, uml::parameter value);
	void drc_dp_in(drcuml_block &block, uml::parameter dst, u32 r);
	void drc_dp_out(drcuml_block &block, uml::parameter value);
	void drc_address(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i, u32 shift);
	void drc_load(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i);
	void drc_store(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i);
	void drc_branch(drcuml_block &block, drc_compiler &c, drc_packet &p, insn const &i, u32 next_pc);
	static u32 drc_ep_nops(insn const &i);
	static u64 drc_reads(insn const &i);

	// floating point
	u32 sp_result(float value);
	u64 dp_result(double value);
	static float sp_in(u32 value);
	static double dp_in(u64 value);

	address_space_config m_program_config;
	memory_access<32, 2, 0, ENDIANNESS_LITTLE>::cache m_cache;
	memory_access<32, 2, 0, ENDIANNESS_LITTLE>::specific m_program;

	u32 const m_istp_reset;
	u8 const m_revision;

	// state shared by the interpreter and the recompiler, in the recompiler's near cache when it's enabled
	internal_state *m_core;
	internal_state m_local_core;

	// C64x+ time stamp counter
	u64 m_tsc_start;
	u32 m_tsch;
	bool m_tsc_running;

	u16 m_int_lines;

	// recompiler
	bool m_enable_drc;
	std::unique_ptr<drcuml_state> m_drcuml;
	uml::code_handle *m_entry;
	uml::code_handle *m_nocode;
	uml::code_handle *m_read8;
	uml::code_handle *m_read16;
	uml::code_handle *m_read32;
	uml::code_handle *m_read32u;
	uml::code_handle *m_write8;
	uml::code_handle *m_write16;
	uml::code_handle *m_write32;
	uml::code_handle *m_write32u;
	bool m_drc_map_changed;

	// memory the recompiled code accesses directly
	struct fast_ram
	{
		u32 start;
		u32 size;
		u8 *base;
		bool writable;
		u32 *gen;      // code page write generations for its region
	};
	std::vector<fast_ram> m_fast_ram;

	// decoded execute packets, checked against the code page write generations before use
	std::unique_ptr<ep_cache_entry []> m_ep_cache;
	std::unique_ptr<u32 []> m_code_gen[256]; // allocated for a region when code is first decoded there
	memory_passthrough_handler m_code_tap;

	// current execute packet
	store m_stores[MAX_EP * 2];
	u32 m_store_count;
	u32 m_ep_nops;
	bool m_ep_sploop;

	// loop buffer
	loop_buffer m_lb;

};

#endif // MAME_CPU_TMS320C6X_TMS320C674X_H
