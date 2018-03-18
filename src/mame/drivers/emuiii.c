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

#include "machine/wd17xx.h"
#include "imagedev/flopdrv.h"
#include "machine/ncr5380.h"
#include "imagedev/harddriv.h"

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
	required_device<wd1772_device> m_floppy;
	//required_device<ncr5380_device> m_scsi;

    virtual void machine_start();
    virtual void machine_reset();
};

void emuiii_state::machine_start()
{
}

void emuiii_state::machine_reset()
{
	machine().firstcpu->reset();
}

static ADDRESS_MAP_START(cpu_mem, AS_PROGRAM, 16, emuiii_state)
	AM_RANGE( 0x000000, 0x007fff ) AM_ROM
    AM_RANGE( 0x008000, 0xffffff ) AM_RAM
	// OS overlays 128k DRAM
	// Sample mem 4/8M as 16x256kb 150ns or 4/8x1M 120ns DRAM 
ADDRESS_MAP_END

NS32016_INTERFACE(emuiii_cpu_config)
{
};

static const wd17xx_interface emuiii_wd17xx_interface =
{
	DEVCB_NULL,
	DEVCB_NULL,//DEVCB_LINE(a310_wd177x_intrq_w),
	DEVCB_NULL,//DEVCB_LINE(a310_wd177x_drq_w),
	{FLOPPY_0, NULL, NULL, NULL}
};


void emuiii_scsi_irq(running_machine &machine, int state)
{

}
static const SCSIConfigTable dev_table =
{
	1, /* 1 SCSI device */
	{
//		{ SCSI_ID_6, "harddisk1", SCSI_DEVICE_HARDDISK }  /* SCSI ID 6, using disk1, and it's a harddisk */
	}
};
static const struct NCR5380interface emuiii_5380_intf =
{
	&dev_table,	// SCSI device table
	emuiii_scsi_irq
};
static const struct harddisk_interface emu_harddisk_config =
{
	NULL,
	NULL,
	"emu_hdd",
	NULL
};



PALETTE_INIT( emuiii )
{
    palette_set_color_rgb(machine, 0, 0x7b, 0x8c, 0x5a);
    palette_set_color_rgb(machine, 1, 0x00, 0x00, 0x00);
}

VIDEO_START( emuiii )
{
}

SCREEN_UPDATE_IND16( emuiii )
{
	return 0;
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

	MCFG_VIDEO_START( emuiii )
	MCFG_SCREEN_UPDATE_STATIC( emuiii )
	MCFG_PALETTE_LENGTH( 2 )
	MCFG_PALETTE_INIT( emuiii )
	MCFG_DEFAULT_LAYOUT(layout_lcd)

	//MCFG_RAM_ADD(RAM_TAG)
	//MCFG_RAM_DEFAULT_SIZE("4M");
	//MCFG_RAM_EXTRA_OPTIONS("8M");

	MCFG_WD1772_ADD(TAG_FLOPPY, emuiii_wd17xx_interface) // WD1772 floppy controller (3.5" 800K DSDD)
//	MCFG_NCR5380_ADD(TAG_SCSI, C7M, emuiii_5380_intf) // NCR5380 SCSI controller
	MCFG_HARDDISK_CONFIG_ADD("harddisk1", emu_harddisk_config )
	
	// i82530 serial (MIDI/RS422)

MACHINE_CONFIG_END

ROM_START(emuiii)
	ROM_REGION16_LE(0x1000000, TAG_CPU, ROMREGION_ERASEFF)
	ROM_LOAD16_BYTE( "eiii_ip381a_l.bin", 0x0000, 0x4000, CRC(34e5283f) )
	ROM_LOAD16_BYTE( "eiii_ip380a_r.bin", 0x0001, 0x4000, CRC(1302c054) )
ROM_END

/*    YEAR  NAME    PARENT  COMPAT   MACHINE INPUT CLASS          INIT  COMPANY               FULLNAME          FLAGS */
COMP( 1988, emuiii, 0,      0,       emuiii, 0,    driver_device, 0,    "E-mu Systems, Inc.", "Emulator Three", GAME_NO_SOUND )
