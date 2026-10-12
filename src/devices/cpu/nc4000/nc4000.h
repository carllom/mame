// license:BSD-3-Clause
// copyright-holders:Carl Lom
/*****************************************************************************

    Novix NC4000 / NC4016

*****************************************************************************/

#ifndef MAME_CPU_NC4000_NC4000_H
#define MAME_CPU_NC4000_NC4000_H

#pragma once

class nc4000_device : public cpu_device
{
public:
	// INT pin, active low: the falling edge (ASSERT_LINE) sets the interrupt request flip-flop
	static constexpr int INT_LINE = 0;

	enum
	{
		NC4000_PC = 1,
		NC4000_T,
		NC4000_N,
		NC4000_I,
		NC4000_J,
		NC4000_K,
		NC4000_MD,
		NC4000_SR,
		NC4000_CARRY,
		NC4000_BDATA,
		NC4000_BMASK,
		NC4000_BDIR,
		NC4000_BTRI,
		NC4000_XDATA,
		NC4000_XMASK,
		NC4000_XDIR,
		NC4000_XTRI
	};

	nc4000_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	// B port is 16 bits, X port is 5 bits; reads return the pin levels, writes drive the output pins
	auto in_portb_callback() { return m_in_portb_cb.bind(); }
	auto out_portb_callback() { return m_out_portb_cb.bind(); }
	auto in_portx_callback() { return m_in_portx_cb.bind(); }
	auto out_portx_callback() { return m_out_portx_cb.bind(); }

	// the multiply step ignores bit 0 of MD on the NC4000P and NC4016
	void set_multiply_bug(bool bug) { m_multiply_bug = bug; }

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface implementation
	virtual u32 execute_min_cycles() const noexcept override { return 1; }
	virtual u32 execute_max_cycles() const noexcept override { return 2; }
	virtual void execute_run() override;
	virtual void execute_set_input(int inputnum, int state) override;

	// device_memory_interface implementation
	virtual space_config_vector memory_space_config() const override ATTR_COLD;

	// device_disasm_interface implementation
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	void push_s();
	void pop_s();
	void push_r(uint16_t data);
	uint16_t pop_r();
	void do_return();

	uint16_t alu(int func, uint16_t t, uint16_t y, int cin, bool update_carry);
	void alu_op(uint16_t op);
	int fetch_op(uint16_t op);
	int store_op(uint16_t op);
	int memory_fetch_op(uint16_t op);
	int memory_store_op(uint16_t op);

	uint16_t read_reg(int reg);
	void write_reg(int reg, uint16_t data);
	uint16_t portb_r();
	uint8_t portx_r();
	void portb_w(uint16_t data);
	void portx_w(uint8_t data);

	const address_space_config m_program_config;
	const address_space_config m_datastack_config;
	const address_space_config m_retstack_config;

	memory_access<16, 1, -1, ENDIANNESS_BIG>::cache m_cache;
	memory_access<16, 1, -1, ENDIANNESS_BIG>::specific m_program;
	memory_access<8, 1, -1, ENDIANNESS_BIG>::specific m_datastack;
	memory_access<8, 1, -1, ENDIANNESS_BIG>::specific m_retstack;

	devcb_read16 m_in_portb_cb;
	devcb_write16 m_out_portb_cb;
	devcb_read8 m_in_portx_cb;
	devcb_write8 m_out_portx_cb;

	bool m_multiply_bug;
	int m_icount;

	// registers
	uint16_t m_pc;
	uint16_t m_ppc;
	uint16_t m_t;
	uint16_t m_n;
	uint16_t m_i;
	uint8_t m_j; // return stack pointer
	uint8_t m_k; // data stack pointer
	uint16_t m_md;
	uint16_t m_sr;
	uint8_t m_carry;

	uint16_t m_bdata;
	uint16_t m_bmask;
	uint16_t m_bdir;
	uint16_t m_btri;
	uint8_t m_xdata;
	uint8_t m_xmask;
	uint8_t m_xdir;
	uint16_t m_xtri; // bit 8 is the interrupt enable

	// TIMES: the latched instruction repeats until I has counted down, plus one cycle
	uint16_t m_times_op;
	bool m_times;
	bool m_times_first;
	bool m_times_last;

	// interrupt request flip-flop, and the return stack depth while it is being serviced
	bool m_int_line;
	bool m_int_request;
	bool m_int_active;
	uint8_t m_int_j;
};

DECLARE_DEVICE_TYPE(NC4000, nc4000_device)

#endif // MAME_CPU_NC4000_NC4000_H
