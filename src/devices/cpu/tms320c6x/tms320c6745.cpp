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

    Peripherals (none emulated yet):

    0x0180_0000  DSP interrupt controller (event combiner, 128 events
                 onto INT4-INT15)
    0x0181_0000  powerdown controller
    0x0182_0000  EMC, 0x0184_0000 memory system (cache control)
    0x01c0_0000  EDMA3 channel controller, 0x01c0_8000/0x01c0_8400 TC0/TC1
    0x01c1_0000  PSC0
    0x01c1_1000  PLL0 controller
    0x01c1_4000  SYSCFG (pin multiplexing, CFGCHIPn, HOST1CFG)
    0x01c2_0000  Timer64P0, 0x01c2_1000 Timer64P1
    0x01c2_2000  I2C0
    0x01c2_3000  RTC
    0x01c4_1000  SPI0
    0x01c4_2000  UART0
    0x01d0_0000  McASP0 control, 0x01d0_1000 AFIFO, 0x01d0_2000 data port
    0x01d0_4000  McASP1 control, 0x01d0_5000 AFIFO, 0x01d0_6000 data port
    0x01d0_c000  UART1, 0x01d0_d000 UART2
    0x01e1_2000  SPI1
    0x01e1_4000  MPU1, 0x01e1_5000 MPU2
    0x01e2_6000  GPIO (banks 0-7)
    0x01e2_7000  PSC1
    0x01e2_8000  I2C1
    0x01f0_0000  eHRPWM0-2, eCAP0-2, eQEP0-1
    0x6000_0000  EMIFA async CS2-CS5, 0x6800_0000 EMIFA control
    0xb000_0000  EMIFB control; SDRAM at 0xc000_0000 is on the host map

    TODO:
    - everything in the list above
    - PLL: the device clock is OSCIN, and the core should run at PLL0
      SYSCLK1 once the PLL controller exists
    - BOOT[15:0] strap input
    - dump the boot ROM, or HLE the AIS loader

***************************************************************************/

#include "emu.h"
#include "tms320c6745.h"


DEFINE_DEVICE_TYPE(TMS320C6745, tms320c6745_device, "tms320c6745", "Texas Instruments TMS320C6745")


tms320c6745_device::tms320c6745_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	tms320c674x_device(mconfig, TMS320C6745, tag, owner, clock, address_map_constructor(FUNC(tms320c6745_device::internal_map), this), 0x0070'0000, 0x00)
{
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
	map(0x0070'0000, 0x007f'ffff).mirror(0x1100'0000).rom().region("bootrom", 0);
	map(0x0080'0000, 0x0083'ffff).mirror(0x1100'0000).ram(); // L2 RAM
	map(0x00e0'0000, 0x00e0'7fff).mirror(0x1100'0000).ram(); // L1P RAM
	map(0x00f0'0000, 0x00f0'7fff).mirror(0x1100'0000).ram(); // L1D RAM
}
