#ifndef NC4000_H_
#define NC4000_H_


#define OPMASK_BCOND 0x3000
#define OPMASK_BOFF  0x0FFF
#define OPMASK_RW    0x1000
#define OPMASK_ALU   0x0E00
#define OPMASK_Y     0x0180
#define OPMASK_IOS	 0x01C0
#define OPMASK_STEP  0x0100
#define OPMASK_CIN   0x0080
#define OPMASK_TN    0x0040
#define OPMASK_RET   0x0020
#define OPMASK_5BIT  0x001F
#define OPMASK_4BIT  0x000F
#define OPMASK_STACK 0x0010
#define OPMASK_SH32  0x0008
#define OPMASK_DIV   0x0004
#define OPMASK_SHDIR 0x0003

#define MASK_INTREQ  0x0100

#define BCOND_T0   ((op & OPMASK_BCOND) == 0x1000)
#define BCOND_NONE ((op & OPMASK_BCOND) == 0x2000)
#define BCOND_LOOP ((op & OPMASK_BCOND) == 0x3000)
#define OP_BOFF (op & OPMASK_BOFF)
#define BADDR(x) ((x & ~OPMASK_BOFF) | OP_BOFF)

#define IS_RET (op & OPMASK_RET)

#define OP_ALUOP (op & OPMASK_ALU)
#define ALUOP_T    0x0000
#define ALUOP_AND  0x0200
#define ALUOP_SUB  0x0400
#define ALUOP_OR   0x0600
#define ALUOP_ADD  0x0800
#define ALUOP_XOR  0x0A00
#define ALUOP_SUBY 0x0C00
#define ALUOP_Y    0x0E00
#define OP_YSRC (op & OPMASK_Y)
#define YSRC_N  0x0000
#define YSRC_NC 0x0080
#define YSRC_MD 0x0100
#define YSRC_SR 0x0180
#define OP_IOS (op & OPMASK_IOS)
#define OP_SHDIR (op & OPMASK_SHDIR)
#define SHDIR_NONE 0x0000
#define SHDIR_LSR  0x0001
#define SHDIR_SHL  0x0002
#define SHDIR_ASR  0x0003
#define OP_SH32  (op & OPMASK_SH32)

#define OP_TN (op & OPMASK_TN)
#define OP_SA (op & OPMASK_STACK)
#define SR_C 0

#define OP_DIV (op & OPMASK_DIV)
#define OP_STEP (op & OPMASK_STEP)

#define OP_5BIT (op & OPMASK_5BIT)
#define OP_4BIT (op & OPMASK_4BIT)

#define OP_CFLAG (op & OPMASK_CIN)
#define CIN (OP_CFLAG ? carry : 0)


union PAIR16
{
#ifdef LSB_FIRST
	struct { UINT16 l,h; } b;
	struct { INT8 l,h; } sb;
#else
	struct { UINT8 h,l; } b;
	struct { INT8 h,l; } sb;
#endif
	UINT16 w;
	INT16 sw;
};

enum {
	NC4000_PC,
	NC4000_T,
	NC4000_N,
	NC4000_I,
	NC4000_JK,
	NC4000_J,
	NC4000_K,
	NC4000_MD,
	NC4000_SR,
	NC4000_carry,
	NC4000_Bdata,
	NC4000_Bdir,
	NC4000_Bmask,
	NC4000_Btri,
	NC4000_Xdata,
	NC4000_Xdir,
	NC4000_Xmask,
	NC4000_Xtri
};

struct nc4000_interface
{
	devcb_read16	m_in_portb_cb;
	devcb_write16	m_out_portb_cb;
	devcb_read8		m_in_portx_cb;
	devcb_write8	m_out_portx_cb;
	devcb_read_line	m_in_irq_cb;
};

//void (*m_output_pins_changed)(nc4000_device &device, UINT32 pins);	// a change has occurred on an output pin

#define NC4000_INTERFACE(name) \
	const nc4000_interface (name) =


class nc4000_device;

class nc4000_device : public cpu_device, public nc4000_interface
{
public:
	nc4000_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);
	virtual ~nc4000_device();

	virtual void device_config_complete();


	DECLARE_READ8_MEMBER( portx_r );
    DECLARE_WRITE8_MEMBER( portx_w );
//    UINT8 portx_r() { return portx_r(*memory_nonspecific_space(machine()), 0); }
//    void portx_w(UINT8 data) { portx_w(*memory_nonspecific_space(machine()), 0, data); }
    UINT8 portx_r() { return portx_r(*machine().memory().first_space(), 0); }
    void portx_w(UINT8 data) { portx_w(*machine().memory().first_space(), 0, data); }

	DECLARE_READ16_MEMBER( portb_r );
    DECLARE_WRITE16_MEMBER( portb_w );
//    UINT16 portb_r() { return portb_r(*memory_nonspecific_space(machine()), 0); }
//    void portb_w(UINT16 data) { portb_w(*memory_nonspecific_space(machine()), 0, data); }
    UINT16 portb_r() { return portb_r(*machine().memory().first_space(), 0); }
    void portb_w(UINT16 data) { portb_w(*machine().memory().first_space(), 0, data); }

    DECLARE_WRITE_LINE_MEMBER( int_w );


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
	const address_space_config		m_datastack_config;
	const address_space_config		m_retstack_config;
	address_space *m_program;
	address_space *m_datastack;
	address_space *m_retstack;

	// device_disasm_interface
	virtual offs_t disasm_disassemble(char *buffer, offs_t pc, const UINT8 *oprom, const UINT8 *opram, UINT32 options);
	virtual UINT32 disasm_min_opcode_bytes() const;
	virtual UINT32 disasm_max_opcode_bytes() const;

	// NC4000 device callbacks
	devcb_resolved_read16	m_in_portb_func;
	devcb_resolved_write16	m_out_portb_func;
	devcb_resolved_read8	m_in_portx_func;
	devcb_resolved_write8	m_out_portx_func;
	devcb_resolved_read_line	m_in_irq_func;


	int DoAlu(UINT16 op);

	struct regs {
		UINT16 pc; // Program counter (next instruction)
		UINT16 t; // Top element of data stack
		UINT16 n; // 'Next' element in data stack
		UINT16 i; // Top element of return stack
		PAIR16 jk; // Stack index
		UINT8 j;
		UINT8 k;

		UINT16 md; // Multiplication/division register
		UINT16 sr; // Square root register

		UINT16 bdata;
		UINT16 bdir; // 0=in 1=out
		UINT16 bmask; // 1=write prohibited
		UINT16 btri;

		UINT8 xdata;
		UINT8 xdir; // 0=in 1=out
		UINT8 xmask; // 1=write prohibited
		UINT8 xtri;
	};

	regs m_regs;
	UINT16 m_ppc;

	UINT8 m_irq_state;
	UINT8 m_irq_latch;

	//
	// To stay compatible with old impl
	//
	bool intreq; // Interrupt request flag
	void interrupt(); // Performs interrupt if enabled
	UINT16 carry; // Carry bit. Should really be a flag register...
	bool timesmode; // CPU is executing a TIMES construct
	void ALU_Op(UINT16 op); // Execute ALU instruction
	int IO_LLI(UINT16 op, int store); // Execute LLI instruction
	void IO_Mem(UINT16 op, int store); // Execute memory instruction
	UINT16 DoALU(UINT16 Op_Y, UINT16 carry_in, UINT16 ALU_func); // Perform ALU work
	UINT16 int_read_reg(UINT16 reg);
	void int_write_reg(UINT16 reg, UINT16 data);
	UINT16 binput, xinput;

};

extern const device_type NC4000;

#endif /* NC4000_H_ */
