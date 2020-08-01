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

class nc4000_device : public cpu_device
{
public:
	nc4000_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);
	virtual ~nc4000_device();

	auto in_portx_callback() { return m_in_portx_cb.bind(); }
	auto in_portb_callback() { return m_in_portx_cb.bind(); }
	auto out_portx_callback() { return m_out_portx_cb.bind(); }
	auto out_portb_callback() { return m_out_portb_cb.bind(); }
	auto in_irq_callback() { return m_in_irq_cb.bind(); }

	virtual void device_config_complete();

	uint8_t portx_r();
	void portx_w(uint8_t data);

	uint16_t portb_r();
	void portb_w(uint16_t data);

    //DECLARE_WRITE_LINE_MEMBER( int_w );

protected:
	// device_t
	virtual void device_start();
	virtual void device_reset();
	virtual void device_resolve_objects() override;

	// device_execute_interface
	virtual void execute_run();
	int m_icount;

	// device_memory_interface
	virtual space_config_vector memory_space_config() const override;
	const address_space_config		m_program_config;
	const address_space_config		m_datastack_config;
	const address_space_config		m_retstack_config;
	address_space *m_program;
	address_space *m_datastack;
	address_space *m_retstack;

	// device_disasm_interface
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

	// NC4000 device callbacks
	devcb_read16 m_in_portb_cb;
	devcb_write16 m_out_portb_cb;
	devcb_read8	m_in_portx_cb;
	devcb_write8 m_out_portx_cb;
	devcb_read_line	m_in_irq_cb;


	int DoAlu(uint16_t op);

	struct regs {
		uint16_t pc; // Program counter (next instruction)
		uint16_t t; // Top element of data stack
		uint16_t n; // 'Next' element in data stack
		uint16_t i; // Top element of return stack
		PAIR16 jk; // Stack index
		uint8_t j;
		uint8_t k;

		uint16_t md; // Multiplication/division register
		uint16_t sr; // Square root register

		uint16_t bdata;
		uint16_t bdir; // 0=in 1=out
		uint16_t bmask; // 1=write prohibited
		uint16_t btri;

		uint8_t xdata;
		uint8_t xdir; // 0=in 1=out
		uint8_t xmask; // 1=write prohibited
		uint8_t xtri;
	};

	regs m_regs;
	uint16_t m_ppc;

	uint8_t m_irq_state;
	uint8_t m_irq_latch;

	//
	// To stay compatible with old impl
	//
	bool intreq; // Interrupt request flag
	void interrupt(); // Performs interrupt if enabled
	uint16_t carry; // Carry bit. Should really be a flag register...
	bool timesmode; // CPU is executing a TIMES construct
	void ALU_Op(uint16_t op); // Execute ALU instruction
	int IO_LLI(uint16_t op, int store); // Execute LLI instruction
	void IO_Mem(uint16_t op, int store); // Execute memory instruction
	uint16_t DoALU(uint16_t Op_Y, uint16_t carry_in, uint16_t ALU_func); // Perform ALU work
	uint16_t int_read_reg(uint16_t reg);
	void int_write_reg(uint16_t reg, uint16_t data);
	uint16_t binput, xinput;

};

DECLARE_DEVICE_TYPE(NC4000, nc4000_device)

#endif /* NC4000_H_ */
