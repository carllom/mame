/*
 * ns32016.h
 *
 *  Created on: 21 feb 2012
 *      Author: clo
 */

#ifndef NS32016_H_
#define NS32016_H_

enum {
	NS32016_PC,
	NS32016_SB,
	NS32016_FP,
	NS32016_SP1,
	NS32016_SP0,
	NS32016_INTBASE,
	NS32016_PSR,
	NS32016_MOD,
	NS32016_R0,
	NS32016_R1,
	NS32016_R2,
	NS32016_R3,
	NS32016_R4,
	NS32016_R5,
	NS32016_R6,
	NS32016_R7,
	NS32016_CFG
};

enum {
	NS32016_WIDTH_BYTE = 0,
	NS32016_WIDTH_WORD = 1,
	NS32016_WIDTH_DWORD = 3
};
enum {
	TRAP_DVZ, // Division by 0
	TRAP_ILL, // Illegal instruction
	TRAP_UND, // Undefined instruction (FPU or MMU instruction when not connected)
	TRAP_FPU, // Floating point error
	TRAP_SVC, // Supervisor trap (supervisor call)
	TRAP_FLG, // Flag trap (Flag instruction when F=1)
	TRAP_BPT, // Breakpoit trap
	TRAP_ABT  // Instruction abort (page fault)
};

struct ns32016_interface
{
//	devcb_read16	m_in_portb_cb;
//	devcb_write16	m_out_portb_cb;
//	devcb_read8		m_in_portx_cb;
//	devcb_write8	m_out_portx_cb;
//	devcb_read_line	m_in_irq_cb;
};

#define NS32016_INTERFACE(name) \
	const ns32016_interface (name) =

class ns32016_device;

typedef offs_t (ns32016_device::*pFmtMethod)();

class ns32016_device : public cpu_device, public ns32016_interface
{
public:
	ns32016_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);
	ns32016_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock, const char* shortname, const char* source);
	virtual ~ns32016_device();

	virtual void device_config_complete();

	static const int op_fmt[];
	static const pFmtMethod fnc_fmt[];

//	DECLARE_READ8_MEMBER( portx_r );
//    DECLARE_WRITE8_MEMBER( portx_w );
//    UINT8 portx_r() { return portx_r(*memory_nonspecific_space(machine()), 0); }
//    void portx_w(UINT8 data) { portx_w(*memory_nonspecific_space(machine()), 0, data); }
//
//	DECLARE_READ16_MEMBER( portb_r );
//    DECLARE_WRITE16_MEMBER( portb_w );
//    UINT16 portb_r() { return portb_r(*memory_nonspecific_space(machine()), 0); }
//    void portb_w(UINT16 data) { portb_w(*memory_nonspecific_space(machine()), 0, data); }
//
//    DECLARE_WRITE_LINE_MEMBER( int_w );


protected:
	// device_t
	virtual void device_start();
	virtual void device_reset();

	// device_execute_interface
	virtual void execute_run();
	int m_icount;

	// device_memory_interface
	virtual const address_space_config *memory_space_config(address_spacenum spacenum = AS_0) const;
	const address_space_config		m_program_config;
//	const address_space_config		m_datastack_config;
//	const address_space_config		m_retstack_config;
	address_space *m_program;
//	address_space *m_datastack;
//	address_space *m_retstack;

	// device_disasm_interface
	virtual offs_t disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options);
	virtual UINT32 disasm_min_opcode_bytes() const;
	virtual UINT32 disasm_max_opcode_bytes() const;



	// NC4000 device callbacks
//	devcb_resolved_read16	m_in_portb_func;
//	devcb_resolved_write16	m_out_portb_func;
//	devcb_resolved_read8	m_in_portx_func;
//	devcb_resolved_write8	m_out_portx_func;
//	devcb_resolved_read_line	m_in_irq_func;

	struct regs {
		UINT32 pc; // Program counter(current instruction)
		UINT32 sb; // Static base
		UINT32 fp; // Frame pointer
		UINT32 sp[2]; // 0 = Interrupt stack, 1 = User stack
		UINT32 intbase; // Interrupt base
		UINT16 psr; // Processor status register
		UINT16 mod; // Module register
		UINT32 r0; // General purpose register 0
		UINT32 r1; // "
		UINT32 r2; // "
		UINT32 r3; // "
		UINT32 r4; // "
		UINT32 r5; // "
		UINT32 r6; // "
		UINT32 r7; // General purpose register 7

		UINT32 cfg; // Configuration register
	};

	UINT32 m_ins;
	regs m_regs;
	UINT32 m_ppc;

//	UINT8 m_irq_state;
//	UINT8 m_irq_latch;
private:
	offs_t op_disp(INT32 &disp, int off);
	bool ins_cond(int cond);
	UINT32 ins_gen_r(int off, int gen, int width, offs_t &ic);
	void ins_gen_w(int off, int gen, int width, UINT32 value);
	offs_t ins_grp16();
	offs_t ins_grp24();
	offs_t ins_format_0(); // Branches
	offs_t ins_format_1(); // Subroutine jumps
	offs_t ins_format_2();
	offs_t ins_format_3();
	offs_t ins_format_4();
	offs_t ins_format_5();
	offs_t ins_format_6();
	offs_t ins_format_7();

	void areg_r(int areg, UINT32 &value);
	void areg_w(int areg, UINT32 value, int width);
	UINT32 gen_read(int gen, offs_t ic, int width);
	void gen_write(int gen, offs_t ic, UINT32 value, int width);
	UINT32 gen_addr(int gen, offs_t ic, int width);

	offs_t disp_size(offs_t offset);
	offs_t gen_size(int gen, offs_t offset, int width);

#ifdef DEBUG_MEM
	// Just use while logging reads/writes
	UINT32 MEM_R_DW(UINT32 addr);
	UINT16 MEM_R_W(UINT32 addr);
	UINT8 MEM_R_B(UINT32 addr);
#endif

	void trap(int trap);
	void wait();
};

extern const device_type NS32016;


#endif /* NS32016_H_ */
