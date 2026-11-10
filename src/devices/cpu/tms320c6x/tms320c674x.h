// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C674X_H
#define MAME_CPU_TMS320C6X_TMS320C674X_H

#pragma once

#include "tms320c6x_dasm.h"

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
	void interrupt_pulse(unsigned line);

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;
	virtual void device_post_load() override ATTR_COLD;

	// device_execute_interface implementation
	virtual u32 execute_min_cycles() const noexcept override { return 1; }
	virtual u32 execute_max_cycles() const noexcept override { return 1; }
	virtual bool execute_input_edge_triggered(int inputnum) const noexcept override { return true; }
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
	enum : u8
	{
		REG_ILC = 64,
		REG_RILC = 65
	};

	static constexpr unsigned WB_SLOTS = 16;   // longer than the longest delay
	static constexpr unsigned WB_DEPTH = 32;
	static constexpr unsigned MAX_EP = 16;     // instructions in one execute packet
	static constexpr unsigned LB_SIZE = 64;    // loop buffer instructions
	static constexpr unsigned BRANCH_SLOTS = 8;

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
		u32 addr[MAX_EP];
		u32 raw[MAX_EP];
		u32 hdr[MAX_EP];
		u32 pce1[MAX_EP];
		insn ep[MAX_EP];
	};

	static constexpr unsigned EP_CACHE_SIZE = 8192;

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

	// fetch and execute
	void step();
	ep_cache_entry const &fetch_ep(u32 pc);
	unsigned decode_ep(u32 pc, ep_cache_entry &e);
	void execute(insn const &i, u32 pce1, u32 next_pc);
	void tick();
	bool condition(insn const &i) const;

	// register access
	u32 reg(unsigned r) const { return m_r[r]; }
	u32 src_a(insn const &i) const { return (i.flags & dasm::F_A_CST) ? u32(i.cst) : m_r[i.a]; }
	u32 src_b(insn const &i) const { return (i.flags & dasm::F_B_CST) ? u32(i.cst) : m_r[i.b]; }
	u64 pair(unsigned r) const { return (u64(m_r[r | 1]) << 32) | m_r[r & ~1]; }
	s64 long40(unsigned r) const { return util::sext((u64(m_r[r | 1] & 0xff) << 32) | m_r[r & ~1], 40); }
	u64 ulong40(unsigned r) const { return (u64(m_r[r | 1] & 0xff) << 32) | m_r[r & ~1]; }
	void write(unsigned r, u32 value, unsigned delay = 0);
	void write_pair(unsigned r, u64 value, unsigned delay = 0);
	void write_long(unsigned r, u64 value, unsigned delay = 0) { write_pair(r, value & 0xff'ffff'ffffU, delay); }
	void flush_writes();
	void set_sat() { m_csr |= 1U << 9; }

	// control registers
	u32 read_control(unsigned crlo, u32 pce1);
	void write_control(unsigned crlo, u32 value);

	// memory
	u32 address(insn const &i, unsigned shift);
	u64 load(insn const &i, u32 addr);

	// branches and interrupts
	void branch(u32 target);
	int pending_interrupt() const;
	void check_interrupts();
	void take_interrupt(int number, u32 return_pc, bool splx);

	// loop buffer
	void sploop_start(insn const &i, u32 address);
	void sploop_load(insn const *ep, u32 const *raw, u32 const *hdr, u32 const *pce1, unsigned count, u32 mask);
	void sploop_end_cycle();
	void sploop_stop();
	void sploop_reload();
	bool sploop_terminate() const;
	bool sploop_reload_condition() const;
	bool sploop_interruptible() const;
	static u32 unit_mask(u8 unit);
	static u32 insn_nops(insn const &i);

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

	// register files
	u32 m_pc;
	u32 m_r[64];

	// control register file
	u32 m_amr;
	u32 m_csr;
	u32 m_ifr;
	u32 m_ier;
	u32 m_istp;
	u32 m_irp;
	u32 m_nrp;

	// C64x+ control register file extensions
	u32 m_tsr;
	u32 m_itsr;
	u32 m_ntsr;
	u32 m_efr;
	u32 m_ierr;
	u32 m_ilc;
	u32 m_rilc;
	u32 m_rep;
	u32 m_ssr;
	u32 m_gfpgfr;
	u32 m_gplya;
	u32 m_gplyb;
	u64 m_tsc_start;
	u32 m_tsch;
	bool m_tsc_running;

	// C67x floating-point control register file extensions
	u32 m_fadcr;
	u32 m_faucr;
	u32 m_fmcr;

	// pipeline
	u64 m_cycle;
	u8 m_wb_reg[WB_SLOTS * WB_DEPTH];
	u32 m_wb_value[WB_SLOTS * WB_DEPTH];
	u8 m_wb_count[WB_SLOTS];
	u64 m_branch_when[BRANCH_SLOTS];
	u32 m_branch_target[BRANCH_SLOTS];
	u8 m_branches;
	u32 m_nop_cycles;
	bool m_idle;
	int m_int_lines;

	// decoded execute packets, checked against memory before use
	std::unique_ptr<ep_cache_entry []> m_ep_cache;

	// current execute packet
	store m_stores[MAX_EP * 2];
	unsigned m_store_count;
	u32 m_ep_nops;
	bool m_ep_sploop;

	// loop buffer
	lb_entry m_lb[LB_SIZE];
	insn m_lb_insn[LB_SIZE];
	u8 m_lb_count;
	bool m_lb_active;
	bool m_lb_loading;
	bool m_lb_draining;
	bool m_lb_fetch;
	u8 m_lb_kind;          // OP_SPLOOP, OP_SPLOOPD or OP_SPLOOPW
	u8 m_lb_ii;
	u8 m_lb_lbc;
	u8 m_lb_loadcnt;
	u8 m_lb_dynlen;
	u8 m_lb_drain;
	u8 m_lb_delay;
	u8 m_lb_stgcyc;
	u32 m_lb_cycles;       // cycles since the SPLOOP instruction
	bool m_lb_kernel;      // SPKERNEL seen
	bool m_lb_initial_term;
	u8 m_lb_creg;
	bool m_lb_z;
	u8 m_lb_cond_history;  // SPLOOPW condition for the last cycles, bit 0 = current
	u32 m_lb_address;
	bool m_lb_int_drain;   // draining to take an interrupt
	bool m_lb_restart;     // restarted after an interrupt (TSR.SPLX was set)
	bool m_lb_kernelr;     // loop body ended with SPKERNELR
	bool m_lb_reload_pending; // reload condition true, waiting for the reload point
	bool m_lb_reload_start;   // reload begins next cycle
	bool m_lb_reload_branch;  // a taken branch disables fetch instead of idling the buffer
	bool m_lb_reloading;
	u8 m_lb_reloadcnt;
	bool m_lb_old_active;  // previous invocation still draining during a reload
	u8 m_lb_old_lbc;
	u8 m_lb_old_drain;

	int m_icount;
};

#endif // MAME_CPU_TMS320C6X_TMS320C674X_H
