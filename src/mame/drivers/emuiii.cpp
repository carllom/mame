/*
 * emuiii.c
 *
 * E-mu Emulator III sampler
 *
 */
#define ADDRESS_MAP_MODERN

#include "emu.h" // Core emulation goodies
#include "cpu/ns32016/ns32016.h"
//#include "machine/ram.h"

#include "machine/wd_fdc.h"
#include "imagedev/flopdrv.h"
#include "machine/ncr5380.h"
//#include "imagedev/harddriv.h"
#include "formats/applix_dsk.h"
#include "bus/scsi/scsi.h"
#include "bus/scsi/scsihd.h"
//#include "bus/scsi/scsicd.h"

#include "rendlay.h"

#define C7M	(7833600)
#define TAG_CPU "cpu"
#define TAG_FLOPPY "floppy"
#define TAG_SCSI "scsi"
#define TAG_PRG_RAM "prgram"

class emuiii_state : public driver_device
{
public:
	emuiii_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
    	m_cpu(*this, TAG_CPU),
		m_floppy(*this, TAG_FLOPPY)//,
		//m_scsi(*this, TAG_SCSI)
    {
    }
	required_device<ns32016_device> m_cpu;
	required_device<wd1772_t> m_floppy;
	//required_device<ncr5380_device> m_scsi;

	//DECLARE_MACHINE_START(emuiii);
	//DECLARE_MACHINE_RESET(emuiii);

	DECLARE_FLOPPY_FORMATS(floppy_formats);
	DECLARE_WRITE_LINE_MEMBER(scsi_irq);

	//DECLARE_VIDEO_START(emuiii);
	DECLARE_PALETTE_INIT(lcd_palette);
};

//MACHINE_START_MEMBER(emuiii_state, emuiii)
//{
//}

//MACHINE_RESET_MEMBER(emuiii_state, emuiii)
//{
//	machine().firstcpu->reset();
//}


//VIDEO_START_MEMBER(emuiii_state, emuiii)
//{
//
//}

static ADDRESS_MAP_START(cpu_mem, AS_PROGRAM, 16, emuiii_state)
	AM_RANGE( 0x000000, 0x007fff ) AM_ROM
    AM_RANGE( 0x008000, 0xffffff ) AM_RAM
	// OS overlays 128k DRAM
	// Sample mem 4/8M as 16x256kb 150ns or 4/8x1M 120ns DRAM 
ADDRESS_MAP_END

NS32016_INTERFACE(emuiii_cpu_config)
{
};

//
// Floppy
//
FLOPPY_FORMATS_MEMBER(emuiii_state::floppy_formats)
	FLOPPY_APPLIX_FORMAT
FLOPPY_FORMATS_END

static SLOT_INTERFACE_START(emuiii_floppies)
	SLOT_INTERFACE("35dd", FLOPPY_35_DD)
SLOT_INTERFACE_END



WRITE_LINE_MEMBER(emuiii_state::scsi_irq)
{

}

PALETTE_INIT_MEMBER(emuiii_state, lcd_palette)
{
	palette.set_pen_color(0, rgb_t(0x7b, 0x8c, 0x5a));
	palette.set_pen_color(1, rgb_t(0x00, 0x00, 0x00));
}

MACHINE_CONFIG_START(emuiii, emuiii_state)
	MCFG_CPU_ADD(TAG_CPU, NS32016, 10000000)
	MCFG_CPU_PROGRAM_MAP(cpu_mem)
	MCFG_CPU_CONFIG( emuiii_cpu_config )
	MCFG_SCREEN_ADD( "screen", RASTER )
	MCFG_SCREEN_REFRESH_RATE( 60 )
	MCFG_SCREEN_VBLANK_TIME(0)
	MCFG_SCREEN_SIZE ( 120, 36 )
	MCFG_SCREEN_VISIBLE_AREA( 0, 119, 0, 35 )

	//MCFG_MACHINE_START_OVERRIDE(emuiii_state, emuiii)
	//MCFG_MACHINE_RESET_OVERRIDE(emuiii_state, emuiii)

	//MCFG_VIDEO_START_OVERRIDE(emuiii_state, emuiii)
	//MCFG_SCREEN_UPDATE_DRIVER(emuiii_state, screen_update_apple2gs)
	MCFG_PALETTE_ADD("palette", 2)
	MCFG_PALETTE_INIT_OWNER(emuiii_state, lcd_palette)
	MCFG_DEFAULT_LAYOUT(layout_lcd)
	//MCFG_GFXDECODE_ADD("gfxdecode", "palette", apple2gs)




	//MCFG_RAM_ADD(RAM_TAG)
	//MCFG_RAM_DEFAULT_SIZE("4M");
	//MCFG_RAM_EXTRA_OPTIONS("8M");

	MCFG_WD1772_ADD(TAG_FLOPPY, 8000000) // WD1772 floppy controller (3.5" 800K DSDD) TODO: Fix clock
	//MCFG_WD_FDC_INTRQ_CALLBACK(WRITELINE(emuiii_state, a310_wd177x_intrq_w))
	//MCFG_WD_FDC_DRQ_CALLBACK(WRITELINE(emuiii_state, a310_wd177x_drq_w))
	MCFG_FLOPPY_DRIVE_ADD("fdc:0", emuiii_floppies, "35dd", emuiii_state::floppy_formats)
	
	MCFG_DEVICE_ADD("ncr5380", NCR5380, C7M) // NCR5380 SCSI controller
	MCFG_LEGACY_SCSI_PORT("scsi")
	MCFG_NCR5380_IRQ_CB(WRITELINE(emuiii_state, scsi_irq))

	MCFG_DEVICE_ADD("scsi", SCSI_PORT, 0)
	MCFG_SCSIDEV_ADD("scsi:" SCSI_PORT_DEVICE1, "harddisk", SCSIHD, SCSI_ID_6)

	// i82530 serial (MIDI/RS422)

MACHINE_CONFIG_END

ROM_START(emuiii)
	ROM_REGION16_LE(0x1000000, TAG_CPU, ROMREGION_ERASEFF)
	ROM_LOAD16_BYTE( "eiii_ip381a_l.bin", 0x0000, 0x4000, CRC(34e5283f) )
	ROM_LOAD16_BYTE( "eiii_ip380a_r.bin", 0x0001, 0x4000, CRC(1302c054) )
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE INPUT CLASS          INIT  COMPANY               FULLNAME          FLAGS */
COMP( 1988, emuiii, 0,      0,       emuiii, 0,    driver_device, 0,    "E-mu Systems, Inc.", "Emulator Three", MACHINE_NO_SOUND )
