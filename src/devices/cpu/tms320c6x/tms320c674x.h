// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_CPU_TMS320C6X_TMS320C674X_H
#define MAME_CPU_TMS320C6X_TMS320C674X_H

#pragma once

class tms320c674x_device : public cpu_device
{
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

	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_execute_interface implementation
	virtual u32 execute_min_cycles() const noexcept override { return 1; }
	virtual u32 execute_max_cycles() const noexcept override { return 1; }
	virtual void execute_run() override;

	// device_memory_interface implementation
	virtual space_config_vector memory_space_config() const override ATTR_COLD;

	// device_disasm_interface implementation
	virtual std::unique_ptr<util::disasm_interface> create_disassembler() override;

private:
	address_space_config m_program_config;
	memory_access<32, 2, 0, ENDIANNESS_LITTLE>::cache m_cache;
	memory_access<32, 2, 0, ENDIANNESS_LITTLE>::specific m_program;

	u32 const m_istp_reset;
	u8 const m_revision;

	// register files
	u32 m_pc;
	u32 m_a[32];
	u32 m_b[32];

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

	// C67x floating-point control register file extensions
	u32 m_fadcr;
	u32 m_faucr;
	u32 m_fmcr;

	int m_icount;
};

#endif // MAME_CPU_TMS320C6X_TMS320C674X_H
