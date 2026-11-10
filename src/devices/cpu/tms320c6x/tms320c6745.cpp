// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Texas Instruments TMS320C6745 fixed- and floating-point DSP

    C674x megamodule with 32 KB L1P, 32 KB L1D and 256 KB L2 RAM, an L2
    boot ROM, and a peripheral set shared with the C6747 and OMAP-L137
    ("Primus"). 176-pin HLQFP (PTP).

    References:
    - SPRS377F: TMS320C6745/C6747 data manual
    - SPRUH91D: TMS320C6745/C6747 Technical Reference Manual
    - SPRAB04G: Using the OMAP-L1x7 Bootloader (AIS)

    The DSP boots from the L2 ROM at its reset vector (HOST1CFG
    DSP_ISTP_RST_VAL = 0x0070_0000). The ROM bootloader reads the BOOT[x]
    straps and loads an AIS image from the selected peripheral. It isn't
    dumped; SPRAB04G says it can be read at 0x1170_0000 through a JTAG
    debugger.

    The internal memories have local addresses (0x00xx_xxxx) that alias
    the global addresses (0x11xx_xxxx).

    Peripherals:

    0x0180_0000  DSP interrupt controller (event combiner, 128 events
                 onto INT4-INT15); exception combiner not emulated
    0x0181_0000  powerdown controller
    0x0182_0000  EMC, 0x0184_0000 memory system (cache control)
    0x01c0_0000  EDMA3 channel controller (transfers complete at once),
                 0x01c0_8000/0x01c0_8400 TC0/TC1
    0x01c1_0000  PSC0 (minimal)
    0x01c1_1000  PLL0 controller
    0x01c1_4000  SYSCFG (pin multiplexing, CFGCHIPn, HOST1CFG); storage only
    0x01c2_0000  Timer64P0, 0x01c2_1000 Timer64P1 (general-purpose modes)
    0x01c2_2000  I2C0 (master mode)
    0x01c2_3000  RTC
    0x01c4_1000  SPI0 (master mode)
    0x01c4_2000  UART0
    0x01d0_0000  McASP0 control (frame timing, no audio), 0x01d0_1000 AFIFO,
                 0x01d0_2000 data port
    0x01d0_4000  McASP1 control (frame timing, no audio), 0x01d0_5000 AFIFO,
                 0x01d0_6000 data port
    0x01d0_c000  UART1, 0x01d0_d000 UART2
    0x01e1_2000  SPI1 (master mode)
    0x01e1_4000  MPU1, 0x01e1_5000 MPU2
    0x01e2_6000  GPIO (banks 0-7), no interrupts yet
    0x01e2_7000  PSC1 (minimal)
    0x01e2_8000  I2C1 (master mode)
    0x01f0_0000  eHRPWM0-2, eCAP0-2, eQEP0-1
    0x6000_0000  EMIFA async CS2-CS5, 0x6800_0000 EMIFA control
    0xb000_0000  EMIFB control; SDRAM at 0xc000_0000 is on the host map

    TODO:
    - everything in the list above
    - PLL: the device clock is OSCIN, and the core runs at a fixed
      OSCIN x 25 / 2 (300 MHz, the PLL setting the boot image programs)
    - BOOT[15:0] strap input
    - dump the boot ROM, or HLE the AIS loader

***************************************************************************/

#include "emu.h"
#include "tms320c6745.h"


DEFINE_DEVICE_TYPE(TMS320C6745, tms320c6745_device, "tms320c6745", "Texas Instruments TMS320C6745")


tms320c6745_device::tms320c6745_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	tms320c674x_device(mconfig, TMS320C6745, tag, owner, clock, address_map_constructor(FUNC(tms320c6745_device::internal_map), this), 0x0070'0000, 0x00),
	m_psc(*this, "psc%u", 0U),
	m_timer(*this, "timer%u", 0U),
	m_spi(*this, "spi%u", 0U),
	m_gpio(*this, "gpio"),
	m_i2c(*this, "i2c%u", 0U),
	m_mcasp(*this, "mcasp%u", 0U),
	m_edma(*this, "edma"),
	m_syscfg{},
	m_evtflag{},
	m_evtmask{},
	m_intmux{},
	m_combined(0)
{
}

void tms320c6745_device::device_add_mconfig(machine_config &config)
{
	TMS320C6745_PSC(config, m_psc[0], 0);
	TMS320C6745_PSC(config, m_psc[1], 0);

	// the timers count AUXCLK, which is OSCIN
	TMS320C6745_TIMER(config, m_timer[0], clock());
	m_timer[0]->tint12().set(FUNC(tms320c6745_device::event_w<4>));
	m_timer[0]->tint34().set(FUNC(tms320c6745_device::event_w<64>));
	TMS320C6745_TIMER(config, m_timer[1], clock());
	m_timer[1]->tint12().set(FUNC(tms320c6745_device::event_w<40>));
	m_timer[1]->tint34().set(FUNC(tms320c6745_device::event_w<48>));

	TMS320C6745_SPI(config, m_spi[0]);
	m_spi[0]->irq().set(FUNC(tms320c6745_device::event_w<37>));
	TMS320C6745_SPI(config, m_spi[1]);
	m_spi[1]->irq().set(FUNC(tms320c6745_device::event_w<43>));

	TMS320C6745_GPIO(config, m_gpio);

	TMS320C6745_I2C(config, m_i2c[0]);
	m_i2c[0]->irq().set(FUNC(tms320c6745_device::event_w<36>));
	TMS320C6745_I2C(config, m_i2c[1]);
	m_i2c[1]->irq().set(FUNC(tms320c6745_device::event_w<42>));

	TMS320C6745_EDMA(config, m_edma);
	m_edma->set_space(DEVICE_SELF, AS_PROGRAM);
	m_edma->region_irq<1>().set(FUNC(tms320c6745_device::event_w<8>));
	m_edma->error_irq().set(FUNC(tms320c6745_device::event_w<56>));

	TMS320C6745_MCASP(config, m_mcasp[0]);
	m_mcasp[0]->arevt().set([this] (int state) { if (state) m_edma->event(0); });
	m_mcasp[0]->axevt().set([this] (int state) { if (state) m_edma->event(1); });
	TMS320C6745_MCASP(config, m_mcasp[1]);
	m_mcasp[1]->arevt().set([this] (int state) { if (state) m_edma->event(2); });
	m_mcasp[1]->axevt().set([this] (int state) { if (state) m_edma->event(3); });
}

void tms320c6745_device::device_start()
{
	tms320c674x_device::device_start();

	// TODO: follow the PLL0 controller; this is the setting the boot image programs
	set_clock_scale(25.0 / 2.0);

	save_item(NAME(m_syscfg));
	save_item(NAME(m_evtflag));
	save_item(NAME(m_evtmask));
	save_item(NAME(m_intmux));
	save_item(NAME(m_combined));
}

void tms320c6745_device::device_reset()
{
	tms320c674x_device::device_reset();

	std::fill(std::begin(m_evtflag), std::end(m_evtflag), 0);
	std::fill(std::begin(m_evtmask), std::end(m_evtmask), 0);
	m_intmux[0] = 0x0706'0504;
	m_intmux[1] = 0x0b0a'0908;
	m_intmux[2] = 0x0f0e'0d0c;
	m_combined = 0;

	// SYSCFG: REVID, DEVIDR0, HOST1CFG
	std::fill(std::begin(m_syscfg), std::end(m_syscfg), 0);
	m_syscfg[0x000 / 4] = 0x4e84'0102;
	m_syscfg[0x018 / 4] = 0x0b7d'f02f;
	m_syscfg[0x044 / 4] = 0x0070'0001;
}


//**************************************************************************
//  INTERRUPT CONTROLLER
//**************************************************************************

void tms320c6745_device::event(unsigned n)
{
	if (n >= 128)
		return;
	m_evtflag[n >> 5] |= 1U << (n & 31);

	// the interrupt selector routes one event to each of INT4-INT15
	for (unsigned line = 4; line < 16; line++)
		if (BIT(m_intmux[(line - 4) >> 2], ((line - 4) & 3) * 8, 7) == n)
			interrupt_pulse(line);

	if (n >= 4)
		update_combined();
}

void tms320c6745_device::update_combined()
{
	// each event combiner ORs the unmasked flags of one group of 32 events
	for (unsigned g = 0; g < 4; g++)
	{
		u32 const flags = m_evtflag[g] & ~m_evtmask[g] & (g ? 0xffff'ffff : 0xffff'fff0);
		bool const active = flags != 0;
		if (active && !BIT(m_combined, g))
		{
			m_combined |= 1 << g;
			event(g);
		}
		else if (!active)
			m_combined &= ~(1 << g);
	}
}

void tms320c6745_device::evtset_w(offs_t offset, u32 data)
{
	for (unsigned b = 0; b < 32; b++)
		if (BIT(data, b))
			event(offset * 32 + b);
}

void tms320c6745_device::evtclr_w(offs_t offset, u32 data)
{
	m_evtflag[offset] &= ~data;
	update_combined();
}

void tms320c6745_device::intc_map(address_map &map)
{
	map(0x000, 0x00f).lr32(NAME([this] (offs_t offset) -> u32 { return m_evtflag[offset]; }));
	map(0x020, 0x02f).w(FUNC(tms320c6745_device::evtset_w));
	map(0x040, 0x04f).w(FUNC(tms320c6745_device::evtclr_w));
	map(0x080, 0x08f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_evtmask[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_evtmask[offset] = data; update_combined(); }));
	map(0x0a0, 0x0af).lr32(NAME([this] (offs_t offset) -> u32 { return m_evtflag[offset] & ~m_evtmask[offset]; }));
	map(0x104, 0x10f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_intmux[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_intmux[offset] = data & 0x7f7f'7f7f; }));
}

ROM_START(tms320c6745)
	// the 1 MB window is the size of the address range, not of the ROM
	ROM_REGION32_LE(0x100000, "bootrom", ROMREGION_ERASE00)
	ROM_LOAD("c6745_bootrom.bin", 0x000000, 0x100000, NO_DUMP)
ROM_END

tiny_rom_entry const *tms320c6745_device::device_rom_region() const
{
	return ROM_NAME(tms320c6745);
}

void tms320c6745_device::internal_map(address_map &map)
{
	// local and global addresses of the internal memories
	map(0x0070'0000, 0x007f'ffff).rom().region("bootrom", 0);
	map(0x1170'0000, 0x117f'ffff).rom().region("bootrom", 0);
	map(0x0080'0000, 0x0083'ffff).ram().share("l2ram");
	map(0x1180'0000, 0x1183'ffff).ram().share("l2ram");
	map(0x00e0'0000, 0x00e0'7fff).ram().share("l1pram");
	map(0x11e0'0000, 0x11e0'7fff).ram().share("l1pram");
	map(0x00f0'0000, 0x00f0'7fff).ram().share("l1dram");
	map(0x11f0'0000, 0x11f0'7fff).ram().share("l1dram");

	map(0x0180'0000, 0x0180'0fff).m(FUNC(tms320c6745_device::intc_map));
	map(0x01c0'0000, 0x01c0'7fff).m(m_edma, FUNC(tms320c6745_edma_device::map));
	map(0x01c1'0000, 0x01c1'0fff).m(m_psc[0], FUNC(tms320c6745_psc_device::map));
	map(0x01c1'4000, 0x01c1'4fff).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_syscfg[offset]; }),
			NAME([this] (offs_t offset, u32 data, u32 mem_mask) { COMBINE_DATA(&m_syscfg[offset]); })); // SYSCFG, storage only
	map(0x01c2'0000, 0x01c2'0fff).m(m_timer[0], FUNC(tms320c6745_timer_device::map));
	map(0x01c2'1000, 0x01c2'1fff).m(m_timer[1], FUNC(tms320c6745_timer_device::map));
	map(0x01c2'2000, 0x01c2'2fff).m(m_i2c[0], FUNC(tms320c6745_i2c_device::map));
	map(0x01c4'1000, 0x01c4'1fff).m(m_spi[0], FUNC(tms320c6745_spi_device::map));
	map(0x01d0'0000, 0x01d0'0fff).m(m_mcasp[0], FUNC(tms320c6745_mcasp_device::map));
	map(0x01d0'2000, 0x01d0'2fff).m(m_mcasp[0], FUNC(tms320c6745_mcasp_device::data_map));
	map(0x01d0'4000, 0x01d0'4fff).m(m_mcasp[1], FUNC(tms320c6745_mcasp_device::map));
	map(0x01d0'6000, 0x01d0'6fff).m(m_mcasp[1], FUNC(tms320c6745_mcasp_device::data_map));
	map(0x01e1'2000, 0x01e1'2fff).m(m_spi[1], FUNC(tms320c6745_spi_device::map));
	map(0x01e2'6000, 0x01e2'6fff).m(m_gpio, FUNC(tms320c6745_gpio_device::map));
	map(0x01e2'8000, 0x01e2'8fff).m(m_i2c[1], FUNC(tms320c6745_i2c_device::map));
	map(0x01e2'7000, 0x01e2'7fff).m(m_psc[1], FUNC(tms320c6745_psc_device::map));
}
