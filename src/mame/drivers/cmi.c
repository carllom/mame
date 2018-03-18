/*
 * drivers/cmi.c - Fairlight CMI (Computer Music Instrument)
 * Skeleton by C. Lom
 *
 * CMI is based on the Quasar M8 computer
 *
 * Currently emulated:
 * CMI IIx - cmi2x
 *
 *
 * http://mess.redump.net/howto:add_a_mess_skeleton_driver
 * Page 28
 *
 * Good references:
 * univac.c (gfx)
 * ldplayer.c, cpu/psx/dma.c (timer)
 *
 * TODO:
 * On reset, set curmap A(3) for processors
 *
 */

/*
 * There are 4 sources of bus addresses:
 *   P1 address (/ADD1)
 *   P2 address (/ADD2)
 *   P1 address w. interrupt (/ADD1 & /IACK)
 *   P2 address w. interrupt (/ADD2 & /IACK)
 * The interrupt circuitry can control the lowest bits of the address
 * There are one PIC per processor, and it controls the vector address offset latch
 *
 * If a CPU accesses FFF0-FFFD the circuitry substitutes A1-A4 with the interrupt vector latch,
 * enabling a wider vector fetch range of FFE0-FFFF.
 *
 * Data bus activities are indicated by READ1, READ2, WRITE1, WRITE2
 */

// Logging/debug defines
#define LOG_GFXACCESS

#define ADDRESS_MAP_MODERN

#include "includes/cmi.h"
#include "machine/cmikbd.h"

//#include "machine/serial.h"

#define SCREEN_UPDATE_MEMBER(name) bool name::screen_update(screen_device &screen, bitmap_t &bitmap, const rectangle &cliprect)

#define P1_MAPSPACE ":q209_p1"
#define P2_MAPSPACE ":q209_p2"

// NOTE: Contains all devices and local variables
class cmi2x_state : public driver_device
{
public:
    cmi2x_state(const machine_config &mconfig, device_type type, const char *tag)
        : driver_device(mconfig, type, tag),
          m_q209_p1(*this, TAG_Q209_P1), // Main CPU 1
          m_q209_p2(*this, TAG_Q209_P2), // Main CPU 2

          m_q133_pia0(*this, TAG_Q133_PIA0),
          m_q133_pia1(*this, TAG_Q133_PIA1),
          m_q133_rtc(*this, TAG_Q133_RTC),
          m_q133_timer(*this, TAG_Q133_TIMER),
    	  m_q133_acia0(*this, TAG_Q133_ACIA0),
    	  m_q133_acia1(*this, TAG_Q133_ACIA1),
	      m_q133_acia2(*this, TAG_Q133_ACIA2),
	      m_q133_acia3(*this, TAG_Q133_ACIA3),
	      m_q133_pic_p1(*this, TAG_Q133_PIC_P1), // P1 interrupt controller
	      m_q133_pic_p2(*this, TAG_Q133_PIC_P2), // P2 interrupt controller

	      m_qfc9_fdc(*this, TAG_QFC9_FDC),
	      m_floppy0(*this, FLOPPY_0),

	      m_ikb1_cpu(*this, TAG_IKB1_CPU),
    	  m_ikb1_pia(*this, TAG_IKB1_PIA),

    	  m_cmi10_cpu(*this, TAG_CMI10_CPU),
    	  m_cmi10_disp0(*this, TAG_CMI10_DISP0),
    	  m_cmi10_disp1(*this, TAG_CMI10_DISP1),
    	  m_cmi10_disp2(*this, TAG_CMI10_DISP2),
    	  m_cmi10_pia0(*this, TAG_CMI10_PIA0),
    	  m_cmi10_pia1(*this, TAG_CMI10_PIA1),

		  m_mapsel(*this, "mapsel")
    {
    	m_vram = 0;
    }

    UINT32 screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect);
    virtual void machine_start();
	virtual void device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr);

	void set_map_p1(UINT8 map) { if (map == curmapP1) return; logerror("%s: CPU P1 map switch %02x => %02x\n", machine().describe_context(), curmapP1, map); curmapP1 = map; }
	void set_map_p2(UINT8 map) { if (map == curmapP2) return; logerror("%s: CPU P2 map switch %02x => %02x\n", machine().describe_context(), curmapP2, map); curmapP2 = map; }
	void push_map_p1(UINT8 map) { if (map == curmapP1) return; prevmapP1 = curmapP1; set_map_p1(map); }
	void pop_map_p1() { logerror("CPU P1 pop map\n"); set_map_p1(prevmapP1); }
	void push_map_p2(UINT8 map) { if (map == curmapP2) return; prevmapP2 = curmapP2; set_map_p2(map); }
	void pop_map_p2() { logerror("CPU P2 pop map\n"); set_map_p2(prevmapP2); }

    bool m_penbout;
    bool m_venb;
    bool m_pergen;

    static const attoseconds_t attoPerPxClk = ATTOSECONDS_PER_SECOND / CPU_CLOCK;
    UINT8 curmapP1;
	UINT8 prevmapP1;
    UINT8 nextmapP1;
    attotime switchclkP1;
    UINT8 curmapP2;
    UINT8 prevmapP2;
	UINT8 nextmapP2;
    attotime switchclkP2;

    bool irqP1, irqP2; // Interrupt request flags from PICn to Pn. Used to facilitate vectored interrupts

	static const device_timer_id TIMER_FUSE_P1 = 0;
	static const device_timer_id TIMER_FUSE_P2 = 1;

	UINT8 cpufunc[8];
	required_shared_ptr<UINT8> m_mapsel;
	UINT8 *m_mapram;
	UINT8 *m_q256ram;
	UINT8 *m_vram;
	int m_vram_curpos; // Current position(offset) for GFX writes
	int m_vram_curbit; // Current bit for GFX writes
	UINT8 m_vram_scroll;

	UINT8 *m_region_fdc;

	UINT8 m_fdc_ctrl;
	UINT8 m_fdc_stat;
	UINT8 m_fdc_wreg;
	int m_fdc_dmaaddr;
	int m_fdc_dmacount; // inverted


//	serial_connection kbd_data;


    DECLARE_READ8_MEMBER( cmi2x_psc_r );
	DECLARE_WRITE8_MEMBER( cmi2x_psc_w );
	DECLARE_READ8_MEMBER( cmi2x_pagedmem_r );
	DECLARE_WRITE8_MEMBER( cmi2x_pagedmem_w );
	DECLARE_WRITE8_MEMBER( cmi2x_mapram_w );

	DECLARE_WRITE_LINE_MEMBER( fdc_intrq_w );
	DECLARE_WRITE_LINE_MEMBER( fdc_drq_w );
	DECLARE_WRITE_LINE_MEMBER( fdc_rdy_w );

	DECLARE_READ_LINE_MEMBER( pia_rtc_ad_r );
	DECLARE_WRITE_LINE_MEMBER( pia_rtc_ad_w );
	DECLARE_WRITE_LINE_MEMBER( pia_rtc_ctrl_w );

	//
	// Interrupt controllers P1+P2
	//
	DECLARE_WRITE8_MEMBER( pic_p1_w );
	DECLARE_READ8_MEMBER( pic_p1_r );
	DECLARE_WRITE_LINE_MEMBER( pic_p1_irq_w );
	DECLARE_WRITE8_MEMBER( pic_p2_w );
	DECLARE_READ8_MEMBER( pic_p2_r );
	DECLARE_WRITE_LINE_MEMBER( pic_p2_irq_w );
	DECLARE_READ8_MEMBER( intvec_p1_r );
	DECLARE_READ8_MEMBER( intvec_p2_r );

	DECLARE_READ8_MEMBER( cmi2x_fdc_r );
	DECLARE_WRITE8_MEMBER( cmi2x_fdc_w );

	DECLARE_READ8_MEMBER( cmi2x_q077_r );

	DECLARE_WRITE8_MEMBER( cmi2x_gfxlatch_w );
	DECLARE_WRITE8_MEMBER( cmi2x_gfxreg_w );

	required_device<m6809_device> m_q209_p1;
	required_device<m6809_device> m_q209_p2;
	required_device<pia6821_device> m_q133_pia0;
	required_device<pia6821_device> m_q133_pia1;
	required_device<msm5832_device> m_q133_rtc;
	required_device<ptm6840_device> m_q133_timer;
	required_device<acia6551_device> m_q133_acia0;
	required_device<acia6551_device> m_q133_acia1;
	required_device<acia6551_device> m_q133_acia2;
	required_device<acia6551_device> m_q133_acia3;
	required_device<i8214_device> m_q133_pic_p1;
	required_device<i8214_device> m_q133_pic_p2;
	required_device<fd1791_device> m_qfc9_fdc;
	required_device<device_t> m_floppy0;
	required_device<m6802_device> m_ikb1_cpu;
	required_device<pia6821_device> m_ikb1_pia;
	required_device<m6802_device> m_cmi10_cpu;
	required_device<dl1416_device> m_cmi10_disp0;
	required_device<dl1416_device> m_cmi10_disp1;
	required_device<dl1416_device> m_cmi10_disp2;
	required_device<pia6821_device> m_cmi10_pia0;
	required_device<pia6821_device> m_cmi10_pia1;

	DECLARE_READ8_MEMBER( cmi10_dil_r );
	DECLARE_WRITE_LINE_MEMBER( cmi10_pia0_ca2_w );
	DECLARE_WRITE_LINE_MEMBER( cmi10_pia0_cb2_w );
	DECLARE_WRITE8_MEMBER( cmi10_bka_w );
	DECLARE_WRITE8_MEMBER( cmi10_ledctr_w );
	DECLARE_WRITE_LINE_MEMBER( cmi10_thold_w );
	DECLARE_WRITE_LINE_MEMBER( cmi10_bc_w );
	DECLARE_READ8_MEMBER( cmi10_ad_r );
	DECLARE_WRITE8_MEMBER( cmi10_kd_w );
	DECLARE_READ8_MEMBER( cmi10_kd_r );
	UINT8 adValue;

	DECLARE_READ8_MEMBER( ikb1_dil_r );
	DECLARE_WRITE8_MEMBER( ikb1_keyrow_w );
	DECLARE_WRITE_LINE_MEMBER( ikb1_flagout_w );
	DECLARE_WRITE_LINE_MEMBER( ikb1_dataout_w);
};

void cmi2x_state::machine_start()
{
	m_mapram = memregion(TAG_Q133_MAPRAM)->base();
	m_q256ram = memregion(TAG_Q256_RAM)->base();

	// FDC init
	m_region_fdc = memregion("fdc")->base();
	m_fdc_stat = 0x98;

	// Gfx init
	m_vram_curpos = 0;
	m_vram_curbit = 0;
	m_vram_scroll = 0;
	m_vram = memregion(TAG_Q219_VRAM)->base();

	set_map_p1(3);
	set_map_p2(3);

	//	kbd_data.in_callback = &keybin_cb;
//	serial_device_connect(m_q133_acia0, &kbd_data);
}

// NOTE: Called every update
UINT32 cmi2x_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)
{
	int gfx,x,y;
	if (m_vram == 0)
		return 0;

	for (y=0;y<256;y++)
	{
		UINT16* p = (UINT16*)bitmap.raw_pixptr((y + 256 - m_vram_scroll)%256 , 0);
		for (x=0;x<64;x++)
		{
			gfx = m_vram[(y*64) + x];
			*p++ = BIT(gfx,7);
			*p++ = BIT(gfx,6);
			*p++ = BIT(gfx,5);
			*p++ = BIT(gfx,4);
			*p++ = BIT(gfx,3);
			*p++ = BIT(gfx,2);
			*p++ = BIT(gfx,1);
			*p++ = BIT(gfx,0);
		}
	}
	return 0;
}

static ADDRESS_MAP_START(cmi2x_p1_mem, AS_PROGRAM, 8, cmi2x_state)
	AM_RANGE( 0x0000, 0xefff ) AM_READWRITE(cmi2x_pagedmem_r, cmi2x_pagedmem_w) /*AM_RAMBANK("p1_ram")*/
    AM_RANGE( 0xf000, 0xf7ff ) AM_ROM AM_WRITE(cmi2x_mapram_w)
	AM_RANGE( 0xf800, 0xfbff ) AM_ROM AM_REGION(TAG_Q209_P1, 0xfc00)

	/*** Peripheral memory region start (fc00-fcef) ***/
	AM_RANGE( 0xfc5e, 0xfc5f ) AM_READWRITE(cmi2x_psc_r, cmi2x_psc_w)
	AM_RANGE( 0xfc40, 0xfc4f ) AM_SHARE("mapsel") AM_RAM //AM_BASE_LEGACY(m_mapsel)
//	AM_RANGE( 0xfc5a, 0xfc5b ) Hard disk subsystem Q077)
	AM_RANGE( 0xfc80, 0xfc83 ) AM_DEVREADWRITE(TAG_Q133_ACIA0, acia6551_device, read, write)
	AM_RANGE( 0xfc84, 0xfc87 ) AM_DEVREADWRITE(TAG_Q133_ACIA1, acia6551_device, read, write)
	AM_RANGE( 0xfc88, 0xfc8b ) AM_DEVREADWRITE(TAG_Q133_ACIA2, acia6551_device, read, write)
	AM_RANGE( 0xfc8c, 0xfc8f ) AM_DEVREADWRITE(TAG_Q133_ACIA3, acia6551_device, read, write)
	AM_RANGE( 0xfc90, 0xfc97 ) AM_DEVREADWRITE(TAG_Q133_TIMER, ptm6840_device, read, write)
//	AM_RANGE( 0xfcc*, ) PIA?
	AM_RANGE( 0xfcc4, 0xfcc4) AM_WRITE(cmi2x_gfxlatch_w) /* Graphics scroll latch */
	AM_RANGE( 0xfcd0, 0xfcdb) AM_WRITE(cmi2x_gfxreg_w)/* Graphics store/autoincrement registers */
	AM_RANGE( 0xfce0, 0xfce1) AM_READWRITE(cmi2x_fdc_r, cmi2x_fdc_w) /* FDC port FCE0 = data, FCE1 = register 0-F(sparse)  */
	AM_RANGE( 0xfcf0, 0xfcf3 ) AM_DEVREADWRITE(TAG_Q133_PIA0, pia6821_device, read, write)
	AM_RANGE( 0xfcf8, 0xfcfb ) AM_DEVREADWRITE(TAG_Q133_PIA1, pia6821_device, read, write)
	AM_RANGE( 0xfcfc, 0xfcfc ) AM_READWRITE(pic_p2_r, pic_p2_w)
	AM_RANGE( 0xfcfd, 0xfcfd ) AM_READWRITE(pic_p1_r, pic_p1_w)

	AM_RANGE( 0xfd00, 0xfeff ) AM_SHARE("share2") AM_RAM /* shared ram */
    AM_RANGE( 0xff00, 0xfff7 ) AM_RAM /* processor local ram */
    AM_RANGE( 0xfff8, 0xfff9 ) AM_READ( intvec_p1_r ) /* special handling of int vector fetch to allow for vectored interrupts */
    AM_RANGE( 0xfffa, 0xfffd ) AM_RAM /* processor local ram */
    AM_RANGE( 0xfffe, 0xffff ) AM_ROM /* restart vector from local rom */
ADDRESS_MAP_END

static ADDRESS_MAP_START(cmi2x_p2_mem, AS_PROGRAM, 8, cmi2x_state)
	AM_RANGE( 0x0000, 0xefff ) AM_READWRITE(cmi2x_pagedmem_r, cmi2x_pagedmem_w) //AM_RAMBANK("p2_ram")
	/* E000-E03F CMI-02 Master card registers (17Mhz = 34.29MHz/2) */
    AM_RANGE( 0xf000, 0xf7ff ) AM_ROM AM_WRITE(cmi2x_mapram_w)
	AM_RANGE( 0xf800, 0xfbff ) AM_ROM AM_REGION(TAG_Q209_P1, 0xf800)

	/*** Peripheral memory region start (fc00-fcef) ***/
	AM_RANGE( 0xfc5e, 0xfc5f ) AM_READWRITE(cmi2x_psc_r, cmi2x_psc_w)
	AM_RANGE( 0xfc40, 0xfc4f ) AM_SHARE("mapsel") AM_RAM
	AM_RANGE( 0xfc5a, 0xfc5b ) AM_READ(cmi2x_q077_r) /* hard drive */
	AM_RANGE( 0xfc80, 0xfc83 ) AM_DEVREADWRITE(TAG_Q133_ACIA0, acia6551_device, read, write)
	AM_RANGE( 0xfc84, 0xfc87 ) AM_DEVREADWRITE(TAG_Q133_ACIA1, acia6551_device, read, write)
	AM_RANGE( 0xfc88, 0xfc8b ) AM_DEVREADWRITE(TAG_Q133_ACIA2, acia6551_device, read, write)
	AM_RANGE( 0xfc8c, 0xfc8f ) AM_DEVREADWRITE(TAG_Q133_ACIA3, acia6551_device, read, write)
	AM_RANGE( 0xfc90, 0xfc97 ) AM_DEVREADWRITE(TAG_Q133_TIMER, ptm6840_device, read, write)
	AM_RANGE( 0xfca0, 0xfca2 ) AM_READ(cmi2x_q077_r) // MIDI
//	AM_RANGE( 0xfcc*, ) PIA?
	AM_RANGE( 0xfcc4, 0xfcc4) AM_WRITE(cmi2x_gfxlatch_w) /* Graphics scroll latch */
	AM_RANGE( 0xfcd0, 0xfcdb) AM_WRITE(cmi2x_gfxreg_w)/* Graphics store/autoincrement registers */
	AM_RANGE( 0xfce0, 0xfce1) AM_READWRITE(cmi2x_fdc_r, cmi2x_fdc_w) /* FDC port FCE0 = data, FCE1 = register 0-F(sparse)  */

	AM_RANGE( 0xfcf0, 0xfcf3 ) AM_DEVREADWRITE(TAG_Q133_PIA0, pia6821_device, read, write)
	AM_RANGE( 0xfcf8, 0xfcfb ) AM_DEVREADWRITE(TAG_Q133_PIA1, pia6821_device, read, write)
	AM_RANGE( 0xfcfc, 0xfcfc ) AM_READWRITE(pic_p2_r, pic_p2_w)
	AM_RANGE( 0xfcfd, 0xfcfd ) AM_READWRITE(pic_p1_r, pic_p1_w)

	AM_RANGE( 0xfd00, 0xfeff ) AM_SHARE("share2") AM_RAM /* shared ram */
    AM_RANGE( 0xff00, 0xfff7 ) AM_RAM /* processor local ram */
    AM_RANGE( 0xfff8, 0xfff9 ) AM_READ( intvec_p2_r ) /* special handling of int vector fetch to allow for vectored interrupts */
    AM_RANGE( 0xfffa, 0xfffd ) AM_RAM /* processor local ram */
    AM_RANGE( 0xfffe, 0xffff ) AM_ROM AM_REGION(TAG_Q209_P1, 0xfbfe) /* restart vector from local rom */
ADDRESS_MAP_END

void cmi2x_state::device_timer(emu_timer &timer, device_timer_id id, int param, void *ptr)
{
	logerror("FUSE timer elapsed, param=%i\n", id);
	switch (id)
	{
	case TIMER_FUSE_P1:
		set_map_p1(nextmapP1);
		nextmapP1 = -1;
		break;
	case TIMER_FUSE_P2:
		set_map_p2(nextmapP2);
		nextmapP2 = -1;
		break;
	}
}

//
// Reads from:
// FC5E (offset 0) - Indivisible instruction
// FC5F (offset 1) - Map status and CPU ID
//
READ8_MEMBER( cmi2x_state::cmi2x_psc_r )
{
//    logerror("%s: Read from Processor System Control register @%x\n", space.machine().describe_context(), offset);
    if (!offset) // FC5E
    {
        return 0xff; // No point in emulating indivisible instruction - the cpu scheduling granularity guarantees it.
    }
    else // FC5F
    {
    	UINT8 id = 0;
        if (!strcmp(space.device().tag(),P2_MAPSPACE))
        	id |= 1; // bit

        id |= (!curmapP1) ? 0 : 2;
        id |= (!curmapP2) ? 0 : 4;

//        logerror("%s: Read Map status and CPU id %x\n", space.machine().describe_context(), id);
        return id;
    }
}

//
// Writes to:
// FC5E (offset 0) - Various CPU functions
// FC5F (offset 1) - Auto map switching fuse
//
WRITE8_MEMBER( cmi2x_state::cmi2x_psc_w )
{
	if (!offset) // FC5E
	{
		bool bitset = BIT(data,3);

		switch (data & 0x7)
		{
		case 0: // Interproc interrupt P1
			device_set_input_line(machine().device(TAG_Q209_P1), M6809_IRQ_LINE, bitset ? ASSERT_LINE : CLEAR_LINE);
			break;
		case 1: // Interproc interrupt P2
			device_set_input_line(machine().device(TAG_Q209_P2), M6809_IRQ_LINE, bitset ? ASSERT_LINE : CLEAR_LINE);
			break;
		case 2: // HW trace P1
			break;
		case 3: // HW trace P2
			break;
		case 4: // Map switch select P1
			nextmapP1 = bitset ? 3 : 0;
			break;
		case 5: // Map switch select P2
			nextmapP2 = bitset ? 3 : 0;
			break;
		case 6: // FIRQ P1
			device_set_input_line(machine().device(TAG_Q209_P1), M6809_FIRQ_LINE, bitset ? ASSERT_LINE : CLEAR_LINE);
			break;
		case 7: // FIRQ P2
			device_set_input_line(machine().device(TAG_Q209_P2), M6809_FIRQ_LINE, bitset ? ASSERT_LINE : CLEAR_LINE);
			break;
		}
//		logerror("%s: Write to Processor System Control register @%x: %02x\n", space.machine().describe_context(), offset, data);
	}
	else // FC5F - FUSE
	{
		if (!strcmp(space.device().tag(),P1_MAPSPACE))
		{
			logerror("P1 FUSE timer set, data=%02x\n", data);
			timer_set(clocks_to_attotime(data), TIMER_FUSE_P1);
		}
		else
		{
			logerror("P2 FUSE timer set, data=%02x\n", data);
			timer_set(clocks_to_attotime(data), TIMER_FUSE_P1);
		}
	}
}

/*
 * MapSel RAM controls paging
 * Location FC40-FC4F
 *
 * FC40: P2 B(user) state
 * FC43: P2 A(system) state, no DMA
 * FC44: P2 A state, DMA channel 1
 * FC45: P2 A state, DMA channel 2
 * FC46: P2 A state, DMA channel 3
 * FC47: P2 A state, DMA channel 4
 * FC48: P1 B(user) state
 * FC4B: P1 A(system) state, no DMA
 * FC4C: P1 A state, DMA channel 1
 * FC4D: P1 A state, DMA channel 2
 * FC4E: P1 A state, DMA channel 3
 * FC4F: P1 A state, DMA channel 4
 *
 * The bits written to mapsel areencoded as this:
 *
 * Odd locations:
 * 0-4 Map select number (0-4) (inverted - 0 = selected)
 * 5   VENB (0 = enable graphics ram)
 * 6   PERGEN (1 = Force parity error) (default 0)
 * 7   PENBOUT (0 = enable peripherals)
 *
 *
 * 32 maps in total - autoswitched depending on state.
 * State consists of: Processor, user/system, dma
 *
 */
/*
 * Mapram F000-F7FF (write only?) Conflicts with common ROM?!?!?
 *
 * Bit pattern for a mapram entry
 *
 * Even locations:
 * 0-2 Card select
 * 3-5 unused
 * 7   Page enable
 *
 * Odd locations:
 * 0-4 Physical 2k block (bit 0 not used in 4k mapping)
 * 5-6 Physical 64k block
 * 7   unused
 */
#define Q256_PAGE_SIZE 2048

READ8_MEMBER( cmi2x_state::cmi2x_pagedmem_r )
{
//    logerror("%s: %02x Read from Paged RAM @%x\n", space.machine().describe_context(), m_mapsel[0], offset);
    UINT8 mapsel = -1;
    if (!strcmp(space.device().tag(),P2_MAPSPACE))
    {
    	mapsel = m_mapsel[0 + curmapP2];
    }
    else if (!strcmp(space.device().tag(),P1_MAPSPACE))
    {
    	mapsel = m_mapsel[8 + curmapP1];
    }
    bool venb = (mapsel & 0x20) == 0; // VIDENB (Video enabled)
	bool penb = (mapsel & 0x80) == 0; // PERENB (Peripherals enabled)

    if (venb & (offset > 0x7FFF) & (offset < 0xC000)) // VRAM read
    {
		// Reads from VRAM area sets current position for graphics writes
		m_vram_curpos = offset - 0x8000;
		m_vram_curbit = 0;
#ifdef LOG_GFXACCESS
    	logerror("%s: GFX_R VRAM[%04x] => %02x\n", space.machine().describe_context(), m_vram_curpos, m_vram[m_vram_curpos]);
#endif
    	return m_vram[m_vram_curpos]; // Not sure this is necessary
    }
    else // Paged mem read
    {
        UINT8 map = mapsel & 0x1F; // Bit 0-4 is map#
        int mempage = offset / Q256_PAGE_SIZE;
        int memoff = offset % Q256_PAGE_SIZE;
        int mapramoff = ((0x1F - map)*64) + (2*mempage); // Map31 starts at lowest address in mapram (explaining 0x1F-map)
        bool pgen = m_mapram[mapramoff] & 0x80;
        if (!pgen)
        	return 0xFF; // Unmapped ram
    	int page = m_mapram[mapramoff + 1] & 0x7F;
//		logerror("%s: PAGM_R[%04x](map %02x, pg %02x/%03x => %02x) <= %02x\n", space.machine().describe_context(), offset, map, mempage, memoff, page, m_q256ram[(page*2048) + memoff]);
    	return m_q256ram[(page*2048) + memoff]; // TODO: 2k/4k toggle. Now always 2k pages.
    }
}

WRITE8_MEMBER( cmi2x_state::cmi2x_pagedmem_w )
{
//	logerror("%s: Write to Paged RAM @%x: %02x\n", space.machine().describe_context(), offset, data);
	UINT8 mapsel = -1;
	if (!strcmp(space.device().tag(),P2_MAPSPACE))
    {
    	// TODO: Determine if system state/user state or DMA. Now we default to user
    	mapsel = m_mapsel[0 + curmapP2];
    }
    else if (!strcmp(space.device().tag(),P1_MAPSPACE))
    {
    	mapsel = m_mapsel[8 + curmapP1];
    }
    bool venb = (mapsel & 0x20) == 0;

    if (venb & (offset > 0x7FFF) & (offset < 0xC000)) // VRAM write
    {
//    	logerror("%s: VRAM_W[%04x] = %02x\n", space.machine().describe_context(), offset - 0x8000, data);
    	m_vram[offset - 0x8000] = data;
    }
    else // Paged mem write
    {
		UINT8 map = mapsel & 0x1F;
		int mempage = offset / Q256_PAGE_SIZE;
		int memoff = offset % Q256_PAGE_SIZE;
        int mapramoff = ((0x1F - map)*64) + (2*mempage);
        bool pgen = m_mapram[mapramoff] & 0x80;
        if (!pgen)
        	return; // Unmapped ram
		int page = m_mapram[mapramoff + 1] & 0x7F;
//		logerror("%s: PAGM_W[%04x](map %02x, pg %02x/%03x => %02x) = %02x\n", space.machine().describe_context(), offset, map, mempage, memoff, page, data);
		m_q256ram[(page*2048) + memoff] = data; // TODO: 2k/4k toggle. Now always 2k pages.
    }
}

WRITE8_MEMBER( cmi2x_state::cmi2x_mapram_w )
{
	m_mapram[offset] = data;
}

/*
 * ACIA (Q133) 1.8432Mhz master oscillator
 * 1 2-way for comm RS422 or RS232C(?)
 *
 * FC80-FC83 ACIA#0 (reg 0-3) Keyboard data in/out
 * FC84-FC87 ACIA#1 (reg 0-3) Data out 0
 * FC88-FC8B ACIA#2 (reg 0-3) Data out 1
 * FC8C-FC8F ACIA#3 (reg 0-3) Connected to S02
 *
 * ACIA# Pin  S03
 * 0     TxD  Key data out
 * 0     RxD  DIN
 * 1     TxD  DOUT0
 * 1     /DTR DON0
 * 1     /DSR FLG0
 * 2     TxD  DOUT1
 * 2     /DTR DON1
 * 2     /DSR FLG1
 */


/*
 * PIA (Q133)
 * 1 to interface RTC chip (clock)
 * 1 general purpose
 *
 * FCF0-FCF3 timer PIA (reg 0-3)
 * FCF8-FCFB general purpose PIA (reg 0-3)
 *
 * timer    pia
 * A0-A4 -- PA0-PA4
 * D0-D4 -- PA5-PA7
 * HOLD  -- PB0
 * READ  -- PB1
 * WRITE -- PB2
 */
static const pia6821_interface pia0_intf =
{
		DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, pia_rtc_ad_r ),
		DEVCB_NULL, //	    devcb_read8 m_in_b_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca2_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb2_cb;
		DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, pia_rtc_ad_w ),
		DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, pia_rtc_ctrl_w),
	    DEVCB_NULL, //	    devcb_write_line m_out_ca2_cb;
	    DEVCB_NULL, //	    devcb_write_line m_out_cb2_cb; // Powerdown inhibit
	    DEVCB_NULL, //	    devcb_write_line m_irq_a_cb;
	    DEVCB_NULL //	    devcb_write_line m_irq_b_cb;
};

//
// 2x parallel port
//
static const pia6821_interface pia1_intf =
{
		DEVCB_NULL, //	    devcb_read8 m_in_a_cb;
		DEVCB_NULL, //	    devcb_read8 m_in_b_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca2_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb2_cb;
		DEVCB_NULL, //	    devcb_write8 m_out_a_cb;
		DEVCB_NULL, //	    devcb_write8 m_out_b_cb;
	    DEVCB_NULL, //	    devcb_write_line m_out_ca2_cb;
	    DEVCB_NULL, //	    devcb_write_line m_out_cb2_cb;
	    DEVCB_NULL, //	    devcb_write_line m_irq_a_cb;
	    DEVCB_NULL //	    devcb_write_line m_irq_b_cb;
};

READ_LINE_MEMBER( cmi2x_state::pia_rtc_ad_r )
{
	address_space *dummy = machine().memory().first_space();//memory_nonspecific_space(machine());
	return 0xf & m_q133_rtc->data_r( *dummy, 0, 0 );
}

WRITE_LINE_MEMBER( cmi2x_state::pia_rtc_ad_w )
{
	m_q133_rtc->address_w( state & 0x0f ); // bit 0-3
	address_space *dummy = machine().memory().first_space(); //memory_nonspecific_space(machine());
	m_q133_rtc->data_w( *dummy, 0, (state>>4) & 0xf0, 0 ); // bit 4-7
}

WRITE_LINE_MEMBER( cmi2x_state::pia_rtc_ctrl_w )
{
	m_q133_rtc->hold_w( BIT(state,0) );
	m_q133_rtc->read_w( BIT(state,1) );
	m_q133_rtc->write_w( BIT(state,2) );
}

static const ptm6840_interface timr_intf =
{
	XTAL_1MHz, //		double m_internal_clock;
	{ 0, 0, 0 }, //		double m_external_clock[3];
	{ DEVCB_LINE_MEMBER(ptm6840_device, set_c3 ),
	  DEVCB_NULL,
	  DEVCB_NULL }, //		devcb_write8 m_out_cb[3];		// function to call when output[idx] changes
	DEVCB_NULL //		devcb_write_line m_irq_cb;	// function called if IRQ line changes
};

/*
 * PIC (Q133)
 * FCFC PIC for P2
 * FCFD PIC for P1
 *
 * interrupt mapping P1:
 * 0	/CHINTX(Channel 1)
 * 1	/CHINTX(Channel 3)
 * 2	/CHINTX(Channel 5)
 * 3	/CHINTX(Channel 7)
 * 4	/CHINTX(Channel 0)
 * 5	/CHINTX(Channel 2)
 * 6	/CHINTX(Channel 4)
 * 7	/CHINTX(Channel 6)
 *
 * interrupt mapping P2:
 * 0	/RTCINT(Q133) & /PERRINT(Q256)
 * 1	/RINT(Gfx)
 * 2	/P2IR2(Master)
 * 3	/IPI2(Q209) (Interprocessor?)
 * 4	/TOUCHINT(Gfx)
 * 5	/PENINT(Gfx)
 * 6	/ADINT(Master) & /IRQ8(Q014)
 * 7	Floppy & H.Disk
 *
 * When interrupt acknowledge is received from either processor, the PIC vector replaces the lowest address bits on the bus
 * We emulate this with a address_map handler for the irq vector (FFF8-9) replacing the offset as we fetch the data
 */

//
// Interface for P1 interrupt controller (mapped to FCFD)
//
static I8214_INTERFACE( pic_p1_intf )
{
	DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, pic_p1_irq_w), //devcb_write_line m_out_int_cb;
	DEVCB_NULL	//devcb_write_line m_out_enlg_cb;
};

// INT output from P1 PIC
WRITE_LINE_MEMBER( cmi2x_state::pic_p1_irq_w )
{
	logerror("P1 interrupt, %02x, %02x\n" ,state, m_q133_pic_p1->a_r());
	m_q209_p1->set_input_line_and_vector(M6809_IRQ_LINE, state, m_q133_pic_p1->a_r());
	// TODO: Set XIRQ line on master card
}

READ8_MEMBER( cmi2x_state::intvec_p1_r )
{
	int vec = m_q133_pic_p1->a_r();
	return m_q209_p1->region()->u8( 0xffc0 | (vec << 1) | offset );
}

READ8_MEMBER( cmi2x_state::pic_p1_r )
{
	return m_q133_pic_p1->a_r();
}

WRITE8_MEMBER( cmi2x_state::pic_p1_w )
{
	m_q133_pic_p1->b_w( data );
	m_q133_pic_p1->sgs_w( data );
	// Check for interrupt?
}

static I8214_INTERFACE( pic_p2_intf )
{
	DEVCB_CPU_INPUT_LINE(TAG_Q209_P2, M6809_IRQ_LINE), //	devcb_write_line	m_out_int_cb;
	DEVCB_NULL //	devcb_write_line	m_out_enlg_cb;
};

READ8_MEMBER( cmi2x_state::pic_p2_r )
{
	return m_q133_pic_p2->a_r();
}

// INT output from P2 PIC
WRITE_LINE_MEMBER( cmi2x_state::pic_p2_irq_w )
{
	m_q209_p2->set_input_line_and_vector(M6809_IRQ_LINE, state, m_q133_pic_p2->a_r());
}

READ8_MEMBER( cmi2x_state::intvec_p2_r )
{
	int vec = m_q209_p2->default_irq_vector();
	return m_q209_p2->region()->u8( 0xffc0 | (vec << 1) | offset );
}

WRITE8_MEMBER( cmi2x_state::pic_p2_w )
{
	m_q133_pic_p2->b_w( data );
	m_q133_pic_p2->sgs_w( data );

	//Check for interrupt?
}









READ8_MEMBER( cmi2x_state::cmi2x_fdc_r )
{
	UINT8 regData = -1;
	if (offset == 0)
	{
		switch(m_fdc_wreg & 0x0f) // register
		{
		case 0: // Control register
			regData = m_fdc_ctrl;
			break;
		case 2: // DMA addr lobyte
			regData = m_fdc_dmaaddr & 0xff;
			break;
		case 4: // DMA addr hibyte
			regData = (m_fdc_dmaaddr >> 8) & 0xff;
			break;
		case 6: // DMA count lobyte (inverted)
			regData = m_fdc_dmacount & 0xff;
			break;
		case 8: // DMA count hibyte (inverted)
			regData = (m_fdc_dmacount >> 8) & 0xff;
			break;
		case 0xa: // Location to write driver ROM
			logerror("%s: FDC driver load (read) dmaddr %04x  dmacount %04x\n", space.machine().describe_context(), m_fdc_dmaaddr, m_fdc_dmacount);
			break; // Never called?
		case 0xc: // 1791 cmd
		case 0xd: // 1791 track
		case 0xe: // 1791 sector
		case 0xf: // 1791 data
			regData = wd17xx_r(m_qfc9_fdc, m_fdc_wreg & 0x03 );
			break;
		}
		logerror("%s: FDC read reg (%02x) => %02x\n", space.machine().describe_context(), m_fdc_wreg, regData);
	}
	else if (offset == 1)
	{
		// Status register: sssssXX0 (bit 0 zero, 1&2 N/C
		// 3 Ready (active high)
		// 4 Two sided (active high)
		// 5 Disk change (active high)
		// 6 Interrupt
		// 7 Device driver loading (active low)

		regData = m_fdc_stat;

		regData &= ~(DSKSTATUS_2SID | DSKSTATUS_DCHG);

		/* 2-sided diskette */
		regData |= floppy_twosid_r(m_floppy0) << 4;

		/* disk change */
		regData |= floppy_dskchg_r(m_floppy0) << 5;

//		regData |= wd17xx_intrq_r(m_qfc9_fdc) << 6;
		logerror("%s: FDC read status register %02x\n", space.machine().describe_context(), regData);
	}

	return regData;
}

WRITE8_MEMBER( cmi2x_state::cmi2x_fdc_w )
{
	int romoff = 0;

	if (offset == 0)
	{
		logerror("%s: FDC write reg (%02x) <= %02x\n", space.machine().describe_context(), m_fdc_wreg, data);
		switch(m_fdc_wreg & 0x0f) // register
		{
		case 0: // Control register
			m_fdc_ctrl = data;
			wd17xx_set_drive(m_qfc9_fdc, m_fdc_ctrl & 3); // Bit 0,1
			// bit 2 - Enable int
			// bit 3 - /DMA ctr
			// bit 4 - Direction (1=to disk)
			wd17xx_set_side(m_qfc9_fdc, BIT(m_fdc_ctrl, 5));
			// bit 6 - retrig head
			wd17xx_dden_w(m_qfc9_fdc, BIT(m_fdc_ctrl, 7));
			break; // TODO: Implement
		case 2: // DMA addr lobyte
			m_fdc_dmaaddr &= 0x0ff00;
			m_fdc_dmaaddr |= data;
			break;
		case 4: // DMA addr hibyte
			m_fdc_dmaaddr &= 0x00ff;
			m_fdc_dmaaddr |= data << 8;
			break;
		case 6: // DMA count lobyte (inverted)
			m_fdc_dmacount &= 0x0ff00;
			m_fdc_dmacount |= data;
			break;
		case 8: // DMA count hibyte (inverted)
			m_fdc_dmacount &= 0x00ff;
			m_fdc_dmacount |= data << 8;
			break;
		case 0xa: // Location to write driver ROM
			logerror("%s: FDC driver load cmd (%02x) dmaddr %04x dmacount %04x\n", space.machine().describe_context(), data, m_fdc_dmaaddr, m_fdc_dmacount);
			while(m_fdc_dmacount < 0x10000)
			{
				cmi2x_pagedmem_w(space, m_fdc_dmaaddr + romoff, *(m_region_fdc + romoff), 0xFF);
				romoff++;
				m_fdc_dmacount++;
			}
			m_fdc_dmacount = 0;
			break; // TODO: proper implementation
		case 0xc: // 1791 cmd
		case 0xd: // 1791 track
		case 0xe: // 1791 sector
		case 0xf: // 1791 data
			wd17xx_w(m_qfc9_fdc, m_fdc_wreg & 0x03, data);
			break;
		}
	}
	else if (offset == 1)
	{
		m_fdc_wreg = data;
	}
}


WRITE_LINE_MEMBER( cmi2x_state::fdc_intrq_w )
{
	m_fdc_stat &=~DSKSTATUS_INT;
	m_fdc_stat |= DSKSTATUS_INT & (state << 6);

	if (BIT(m_fdc_ctrl,2)) // Interrupt enabled
	{
		logerror("interrupt fdc->p2\n");
		m_q133_pic_p2->r_w(0x7f); // IRQ7 -> p2
	}
}

WRITE_LINE_MEMBER( cmi2x_state::fdc_drq_w )
{
	if (state == ASSERT_LINE )
	{
		logerror("%s: FDC DRQ assert\n", machine().describe_context());
		if (!BIT(m_fdc_ctrl,3)) // DMA on
		{
			if (!BIT(m_fdc_ctrl,4)) // Disk read
			{
				logerror("FDC DMA read\n");
				UINT8 data = wd17xx_data_r(m_qfc9_fdc, m_fdc_ctrl & 3);
				if (m_fdc_dmacount < 0xffff)//if (m_fdc_dmacount < 0x10000)
				{
					push_map_p2(4); // State A,DMA1
					cmi2x_pagedmem_w(*m_q209_p2->space(0), m_fdc_dmaaddr, data, 0xFF);
					logerror("FDC DMA read byte %02x to offset %x, count %x\n", data, m_fdc_dmaaddr, m_fdc_dmacount);
					m_fdc_dmaaddr++;
					m_fdc_dmacount++;
					if (m_fdc_dmacount == 0xffff)
						pop_map_p2();
				}
				//else
				//{
				//	pop_map_p2(); // Not really what we want to achieve is it? Nextmap could be scheduled
				//}
			}
			else // Disk write
			{
				logerror("FDC DMA write\n");
				UINT8 data = cmi2x_pagedmem_r(*m_q209_p2->space(0), m_fdc_dmaaddr, 0xFF);
				if (m_fdc_dmacount < 0xffff)//if (m_fdc_dmacount < 0x10000)
				{
					push_map_p2(4); // State A,DMA1
					wd17xx_data_w(m_qfc9_fdc, m_fdc_ctrl & 3, data);
					logerror("FDC DMA write byte %02x from offset %x, count %x\n", data, m_fdc_dmaaddr, m_fdc_dmacount);
					m_fdc_dmaaddr++;
					m_fdc_dmacount++;
					if (m_fdc_dmacount == 0xffff)
						pop_map_p2();
				}
				//else
				//{
				//	pop_map_p2(); // Not really what we want to achieve is it? Nextmap could be scheduled
				//}
			}
		}
		else // Do we have to read anyway to avoid missing data error?
		{
//			UINT8 data = wd17xx_data_r(m_qfc9_fdc, m_fdc_ctrl & 3);
//			logerror("FDC NoDMA dummy read byte %02x", data);
			logerror("FDC data available but NoDMA\n");
		}
	}
	else
	{
		logerror("%s: FDC DRQ clear\n", machine().describe_context());
	}
}

WRITE_LINE_MEMBER( cmi2x_state::fdc_rdy_w )
{
	m_fdc_stat = (m_fdc_stat & 0xF7) | (state << 3);
	logerror("%s: FDC RDY %02x\n", machine().describe_context(), state);
}

static LEGACY_FLOPPY_OPTIONS_START(cmi2x)
//	FLOPPY_OPTION( imd, "imd", "IMD floppy disk image",	imd_dsk_identify, imd_dsk_construct, NULL, NULL)
//	FLOPPY_OPTION(mirage, "imd", "CMI disk image", basicdsk_identify_default, basicdsk_construct_default, NULL,
//		HEADS([1])
//		TRACKS([77])
//		SECTORS([26])
//		SECTOR_LENGTH([128])
//		FIRST_SECTOR_ID([0]))
LEGACY_FLOPPY_OPTIONS_END


static const wd17xx_interface fdc_intf =
{
	DEVCB_NULL, //	devcb_read_line in_dden_func;
	DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, fdc_intrq_w), //	devcb_write_line out_intrq_func;
	DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, fdc_drq_w), //	devcb_write_line out_drq_func;
	{ FLOPPY_0, FLOPPY_1, NULL, NULL }
};

static const floppy_interface cmi2x_floppy_interface =
{
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_NULL,
	DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, fdc_rdy_w ),
	FLOPPY_STANDARD_8_DSDD,
	LEGACY_FLOPPY_OPTIONS_NAME(cmi2x),
	NULL,
	NULL
};

READ8_MEMBER( cmi2x_state::cmi2x_q077_r )
{
	return 0xFF;
}








WRITE8_MEMBER( cmi2x_state::cmi2x_gfxlatch_w )
{
	m_vram_scroll = data;
#ifdef LOG_GFXACCESS
	logerror("%s: GFX_W scroll latch <= %02x\n", space.machine().describe_context(), data);
#endif
}

#define VRAM_BIT(data) m_vram[m_vram_curpos] = (m_vram[m_vram_curpos] & m_vram[0x4200 + m_vram_curbit]) | (data & ~m_vram[0x4200 + m_vram_curbit])

WRITE8_MEMBER( cmi2x_state::cmi2x_gfxreg_w )
{
	// About comments for each command: Y means horizontal(bytes/bits), and X means vertical(lines)
	switch(offset)
	{
	case 0: // Store at current position
		VRAM_BIT(data);
		break;
	case 1: // Store and inc Y
		VRAM_BIT(data);
		m_vram_curbit++;
		break;
	case 2: // Store and dec Y
		VRAM_BIT(data);
		m_vram_curbit--;
		break;
	case 3: // Store as byte at current position
		m_vram[m_vram_curpos] = data;
		break;
	case 4: // Store and inc X
		VRAM_BIT(data);
		m_vram_curpos += 64;
		break;
	case 5: // Store and inc X, inc Y
		VRAM_BIT(data);
		m_vram_curpos += 64;
		m_vram_curbit++;
		break;
	case 6: // Store and inc X, dec Y
		VRAM_BIT(data);
		m_vram_curpos += 64;
		m_vram_curbit--;
		break;
	case 7: // Store as byte and inc X 
		m_vram[m_vram_curpos] = data;
		m_vram_curpos += 64;
		break;
	case 8: // Store and dec X
		VRAM_BIT(data);
		m_vram_curpos -= 64;
		break;
	case 9: // Store and dec X, inc Y
		VRAM_BIT(data);
		m_vram_curpos -= 64;
		m_vram_curbit++;
		break;
	case 10: // Store and dec X, dec Y
		VRAM_BIT(data);
		m_vram_curpos -= 64;
		m_vram_curbit--;
		break;
	case 11: // Store as byte and Dec X
		m_vram[m_vram_curpos] = data;
		m_vram_curpos -= 64;
		break;
	}

	// Propagate bit under/overflow to byte pos
	if (m_vram_curbit < 0)
		m_vram_curpos++;
	else if (m_vram_curbit > 7)
		m_vram_curpos++;

	m_vram_curpos &= 0x3FFF; // Keep(wrap) position within 16k VRAM
	m_vram_curbit &= 0x7; // Wrap bit position

#ifdef LOG_GFXACCESS
	char bits[9] = "........";
	for (int i=0;i<8;i++)
	{
		if ((data>>i) & 1)
			bits[7-i]= '*';
		else
			bits[7-i]= '.';
	}
	logerror("%s: GFX_W REG[%02x] <= %02x, VRAM[%04x] <= %s\n", space.machine().describe_context(), offset, data, m_vram_curpos, bits);
#endif
}







ADDRESS_MAP_START(cmi2x_mc003_mem, AS_PROGRAM, 8, cmi2x_state)
	AM_RANGE( 0x0000, 0x0080 ) AM_RAM
	AM_RANGE( 0x4000, 0x7fff ) AM_READ( ikb1_dil_r )
	AM_RANGE( 0x8000, 0xbfff ) AM_MIRROR( 0x3ffc ) AM_DEVREADWRITE(TAG_IKB1_PIA, pia6821_device, read, write)
	AM_RANGE( 0xc000, 0xffff ) AM_MIRROR( 0x3c00 ) AM_ROM
ADDRESS_MAP_END

static const pia6821_interface ikb1_pia_intf =
{
		DEVCB_NULL, //	    devcb_read8 m_in_a_cb;
		DEVCB_NULL, //	    devcb_read8 m_in_b_cb;
		/* FLAG I */DEVCB_LINE_GND, //	    devcb_read_line m_in_ca1_cb;
		 DEVCB_LINE_GND, //	    devcb_read_line m_in_cb1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca2_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb2_cb;
		DEVCB_NULL, //	    devcb_write8 m_out_a_cb;
		DEVCB_DRIVER_MEMBER(cmi2x_state, ikb1_keyrow_w), //DEVCB_NULL, //	    devcb_write8 m_out_b_cb;
		DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, ikb1_flagout_w),// FLAG O devcb_write_line m_out_ca2_cb;
	    DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, ikb1_dataout_w),// DATA O devcb_write_line m_out_cb2_cb;
	    DEVCB_CPU_INPUT_LINE(TAG_IKB1_CPU, M6800_IRQ_LINE),	//	    devcb_write_line m_irq_a_cb;
	    DEVCB_CPU_INPUT_LINE(TAG_IKB1_CPU, M6800_IRQ_LINE) //	    devcb_write_line m_irq_b_cb;
};

READ8_MEMBER( cmi2x_state::ikb1_dil_r )
{
	//return input_port_read( machine(), TAG_IKB1_SWOPT );
	return machine().root_device().ioport(TAG_IKB1_SWOPT)->read();
}

WRITE8_MEMBER( cmi2x_state::ikb1_keyrow_w )
{
	UINT8 keys = 0;

	switch (data)
	{
	case 0xfe:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW0);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW0)->read();
		break;
	case 0xfd:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW1);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW1)->read();
		break;
	case 0xfb:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW2);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW2)->read();
		break;
	case 0xf7:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW3);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW3)->read();
		break;
	case 0xef:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW4);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW4)->read();
		break;
	case 0xdf:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW5);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW5)->read();
		break;
	case 0xbf:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW6);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW6)->read();
		break;
	case 0x7f:
//		keys = input_port_read(machine(),TAG_IKB1_KBROW7);
		keys = machine().root_device().ioport(TAG_IKB1_KBROW7)->read();
		break;
	}
	m_ikb1_pia->set_a_input(keys, keys);
//	logerror("%s: Keyboard matrix read. Row @%02x -> col %02x\n", space.machine().describe_context(), data, keys);
}

static TIMER_DEVICE_CALLBACK( pulse_ikb1_pia )
{
	// There MUST be a better way to do this...
	pia6821_device *mach = (pia6821_device *)timer.machine().device(TAG_IKB1_PIA);
	if (mach->cb1_r() == 0)
		mach->cb1_w(ASSERT_LINE);
	else
		mach->cb1_w(CLEAR_LINE);
}

WRITE_LINE_MEMBER( cmi2x_state::ikb1_flagout_w )
{
	logerror("ikb flagout: %02x\n", state);
}

WRITE_LINE_MEMBER( cmi2x_state::ikb1_dataout_w )
{
	logerror("ikb data: %02x\n", state);
//	set_out_data_bit(kbd_data.State,state);
}

// CMI-10 Master keyboard controller
ADDRESS_MAP_START(cmi2x_mc004_mem, AS_PROGRAM, 8, cmi2x_state)
	AM_RANGE( 0x0000, 0x007f ) AM_RAM // Internal(m6802) RAM
	AM_RANGE( 0x0080, 0x0083 ) AM_DEVREADWRITE(TAG_CMI10_PIA1, pia6821_device, read, write)
	AM_RANGE( 0x0090, 0x0093 ) AM_DEVREADWRITE(TAG_CMI10_PIA0, pia6821_device, read, write)
	AM_RANGE( 0x00a0, 0x00a0 ) AM_DEVREADWRITE(TAG_CMI10_ACIA0, acia6850_device, status_read, control_write)
	AM_RANGE( 0x00a1, 0x00a1 ) AM_DEVREADWRITE(TAG_CMI10_ACIA0, acia6850_device, data_read, data_write)
	AM_RANGE( 0x00b0, 0x00b0 ) AM_DEVREADWRITE(TAG_CMI10_ACIA1, acia6850_device, status_read, control_write)
	AM_RANGE( 0x00b1, 0x00b1 ) AM_DEVREADWRITE(TAG_CMI10_ACIA1, acia6850_device, data_read, data_write)
	AM_RANGE( 0x00c0, 0x00c0 ) AM_READ(cmi10_dil_r)
	AM_RANGE( 0x4000, 0x43ff ) AM_RAM
	AM_RANGE( 0x5000, 0x53ff ) AM_UNMAP // RAM, but normally not installed
	AM_RANGE( 0x9000, 0x93ff ) AM_UNMAP // ROM, but normally not installed
	AM_RANGE( 0xa000, 0xa3ff ) AM_UNMAP // ROM, but normally not installed
	AM_RANGE( 0xb000, 0xb3ff ) AM_ROM
	AM_RANGE( 0xb400, 0xfbff ) AM_UNMAP
	AM_RANGE( 0xfc00, 0xffff ) AM_ROM
ADDRESS_MAP_END

//
// PA0-7 <-> KD0-7
// PB0-7 <- AD0-7
// CB1 -> /DR
// CB2 -> B,/C
// CA1 -> SC0
// CA2 -> THLD
static const pia6821_interface cmi10_piain_intf =
{
		DEVCB_DRIVER_MEMBER(cmi2x_state, cmi10_kd_r), // devcb_read8 m_in_a_cb;
		DEVCB_DRIVER_MEMBER(cmi2x_state, cmi10_ad_r), // devcb_read8 m_in_b_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca2_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb2_cb;
		DEVCB_DRIVER_MEMBER(cmi2x_state, cmi10_kd_w), // devcb_write8 m_out_a_cb;
		DEVCB_NULL, //	    devcb_write8 m_out_b_cb;
	    DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, cmi10_thold_w), //	    devcb_write_line m_out_ca2_cb;
	    DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, cmi10_bc_w), //	    devcb_write_line m_out_cb2_cb;
	    DEVCB_NULL, //	    devcb_write_line m_irq_a_cb;
	    DEVCB_NULL //	    devcb_write_line m_irq_b_cb;
};

WRITE_LINE_MEMBER( cmi2x_state::cmi10_thold_w )
{
//	logerror("%s: PIA1 - THOLD CA2=%02x, BKA=%02x\n", machine().describe_context(), state, m_cmi10_pia0->a_output());
}

WRITE_LINE_MEMBER( cmi2x_state::cmi10_bc_w )
{
//	logerror("%s: PIA1 - B/C flag=%02x\n", machine().describe_context(), state);
	if (state == 0)
	{
		UINT8 bka = m_cmi10_pia0->a_output();

		switch (bka & 0x7)
		{
		case 0:
//			adValue = input_port_read(machine(), TAG_CMI10_SLIDER1);
			adValue = machine().root_device().ioport(TAG_CMI10_SLIDER1)->read();
			break;
		case 1:
//			adValue = input_port_read(machine(), TAG_CMI10_SLIDER2);
			adValue = machine().root_device().ioport(TAG_CMI10_SLIDER2)->read();
			break;
		case 2:
//			adValue = input_port_read(machine(), TAG_CMI10_SLIDER3);
			adValue = machine().root_device().ioport(TAG_CMI10_SLIDER3)->read();
			break;
		case 3:
//			adValue = input_port_read(machine(), TAG_CMI10_PEDAL1);
			adValue = machine().root_device().ioport(TAG_CMI10_PEDAL1)->read();
			break;
		case 4:
//			adValue = input_port_read(machine(), TAG_CMI10_PEDAL2);
			adValue = machine().root_device().ioport(TAG_CMI10_PEDAL2)->read();
			break;
		case 5:
//			adValue = input_port_read(machine(), TAG_CMI10_PEDAL3);
			adValue = machine().root_device().ioport(TAG_CMI10_PEDAL3)->read();
			break;
		case 6:
			adValue = 0; // Unknown analog input - Connector S5, pin 6
			break;
		case 7:
			adValue = 0; // Unknown analog input - Connector S5, pin 4
			break;
		}

//		logerror("%s: PIA1 - Asserting /DR flag\n", machine().describe_context());
		m_cmi10_pia1->cb1_w(0); // Assert data ready flag. My, aren't we quick...
	}
	else
	{
//		logerror("%s: PIA1 - Clearing /DR flag\n", machine().describe_context());
		m_cmi10_pia1->cb1_w(1); // Clear data ready flag
	}
}

READ8_MEMBER( cmi2x_state::cmi10_ad_r )
{
//	logerror("%s: PIA1 - Reading AD port\n", machine().describe_context());
	if (!m_cmi10_pia1->cb1_r())
	{
//		logerror("%s: PIA1 - Valid AD value %02x on bus\n", machine().describe_context(), adValue);
		return adValue;
	}
	return 0;
}

WRITE8_MEMBER( cmi2x_state::cmi10_kd_w )
{
//	logerror("%s: PIA1 - KD update %02x\n", machine().describe_context(), data);
}

READ8_MEMBER( cmi2x_state::cmi10_kd_r )
{
//	logerror("%s: PIA1 - KD read. Currently, BKA=%02x\n", machine().describe_context(), m_cmi10_pia0->a_output());
	UINT8 bka = m_cmi10_pia0->a_output();

	UINT8 kd = 0;

	if (bka & 0x80)
	{
		// Switches + pedals
//		if (input_port_read(machine(), TAG_CMI10_SWITCHES) & (1<<(bka&0x7)))
		if (machine().root_device().ioport(TAG_CMI10_SWITCHES)->read() & (1<<(bka&0x7)))
		{
			kd |= 0x40;
		}
		else
		{
			kd &= 0xbf;
		}

		// Keypad
//		if (input_port_read(machine(), TAG_CMI10_NUMPAD) & (1<<(bka&0xf)))
		if (machine().root_device().ioport(TAG_CMI10_NUMPAD)->read() & (1<<(bka&0xf)))
		{
			kd |= 0x80;
		}
		else
		{
			kd &= 0x7f;
		}
	}

	if (m_cmi10_pia1->ca2_output()) // indicates THLD keyboard scan
	{
		//
		// Master keyboard
		//
//		if (input_port_read(machine(), TAG_CMI10_KEYB0) & (1<<(bka&0x1f)))
		if (machine().root_device().ioport(TAG_CMI10_KEYB0)->read() & (1<<(bka&0x1f)))
			kd |= 0x01;
		else
			kd &= 0xfe;
//		if (input_port_read(machine(), TAG_CMI10_KEYB1) & (1<<(bka&0x1f)))
		if (machine().root_device().ioport(TAG_CMI10_KEYB1)->read() & (1<<(bka&0x1f)))
			kd |= 0x02;
		else
			kd &= 0xfd;
//		if (input_port_read(machine(), TAG_CMI10_KEYB2) & (1<<(bka&0x1f)))
		if (machine().root_device().ioport(TAG_CMI10_KEYB2)->read() & (1<<(bka&0x1f)))
			kd |= 0x04;
		else
			kd &= 0xfb;

		//
		// Slave keyboard are bit 3-5 - input ports not mapped (yet)
		//
//		if (input_port_read(machine(), TAG_CMI14_KEYB3) & (1<<(bka&0x1f)))
		if (machine().root_device().ioport(TAG_CMI14_KEYB3)->read() & (1<<(bka&0x1f)))
			kd |= 0x08;
		else
			kd &= 0xf7;
//		if (input_port_read(machine(), TAG_CMI14_KEYB4) & (1<<(bka&0x1f)))
		if (machine().root_device().ioport(TAG_CMI14_KEYB4)->read() & (1<<(bka&0x1f)))
			kd |= 0x10;
		else
			kd &= 0xef;
//		if (input_port_read(machine(), TAG_CMI14_KEYB5) & (1<<(bka&0x1f)))
		if (machine().root_device().ioport(TAG_CMI14_KEYB5)->read() & (1<<(bka&0x1f)))
			kd |= 0x20;
		else
			kd &= 0xdf;

	}
	return kd;
}


// Entire music key encoding:
// BKA0-4 subgroup key (0-24(25))
// returns result in KD0-2(master) & KD3-5(slave)
//
// Lamps are set by CA2 BKA6,7
//

//
// PA0-7 -> BKA0-7
// PB0,1 -> DA1,0
// PB2-7 -> CS2,CU2,CS1,CU1,CS0,CU0
// CB1 -> /KPAD
// CB2 -> /DWS
// CA1 -> SCND
// CA2 -> flank triggers BKA6->LP1, BKA7->LP2
//
static const pia6821_interface cmi10_piaout_intf =
{
		DEVCB_NULL, //	    devcb_read8 m_in_a_cb;
		DEVCB_NULL, //	    devcb_read8 m_in_b_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb1_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_ca2_cb;
		DEVCB_LINE_GND, //	    devcb_read_line m_in_cb2_cb;
		DEVCB_DRIVER_MEMBER(cmi2x_state, cmi10_bka_w), //	    devcb_write8 m_out_a_cb;
		DEVCB_DRIVER_MEMBER(cmi2x_state, cmi10_ledctr_w), //	    devcb_write8 m_out_b_cb;
		DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, cmi10_pia0_ca2_w), //	    devcb_write_line m_out_ca2_cb;
	    DEVCB_DRIVER_LINE_MEMBER(cmi2x_state, cmi10_pia0_cb2_w), //	    devcb_write_line m_out_cb2_cb;
	    DEVCB_NULL, //	    devcb_write_line m_irq_a_cb;
	    DEVCB_NULL //	    devcb_write_line m_irq_b_cb;
};

static void updateLed(cmi2x_state* state, UINT8 ctrl, UINT8 data)
{
	dl1416_ce_w(state->m_cmi10_disp0, ctrl & 0x40);
	dl1416_ce_w(state->m_cmi10_disp1, ctrl & 0x10);
	dl1416_ce_w(state->m_cmi10_disp2, ctrl & 0x04);
	int adr = ((ctrl & 2)>>1) | ((ctrl & 1)<<1);

	if ( !(ctrl & 0x40) ) // CS0 low
	{
		dl1416_data_w(state->m_cmi10_disp0, adr, data);
		dl1416_cu_w(state->m_cmi10_disp0, ctrl & 0x80);
	}
	else if (!(ctrl & 0x10)) // CS1 low
	{
		dl1416_data_w(state->m_cmi10_disp1, adr, data);
		dl1416_cu_w(state->m_cmi10_disp1, ctrl & 0x20);
	}
	else if (!(ctrl & 0x4)) // CS2 low
	{
		dl1416_data_w(state->m_cmi10_disp2, adr, data);
		dl1416_cu_w(state->m_cmi10_disp2, ctrl & 0x8);
	}

	logerror("%s: PIA0 - Wrote to LED - data %02x, %02x\n", state->machine().describe_context(), ctrl, data);
}

static void updateSwitchlamp(int lamp1, int lamp2)
{
	output_set_value("switch1", lamp1);
	output_set_value("switch2", lamp2);
}

WRITE_LINE_MEMBER( cmi2x_state::cmi10_pia0_ca2_w )
{
	int lamp = m_cmi10_pia0->a_output();
	if (state)
		updateSwitchlamp((lamp & 0x40)>>6,(lamp & 0x80)>>7);
//	logerror("%s: PIA0 - CA2 lamp strobe %02x, KBA=%02x\n", machine().describe_context(), state, lamp);
}

WRITE_LINE_MEMBER( cmi2x_state::cmi10_pia0_cb2_w )
{
//	logerror("%s: PIA0 - CB2 LED strobe %02x\n", machine().describe_context(), state);
	dl1416_wr_w(m_cmi10_disp0, state);
	dl1416_wr_w(m_cmi10_disp1, state);
	dl1416_wr_w(m_cmi10_disp2, state);
	if (state == 0)
		updateLed(this, m_cmi10_pia0->b_output(), m_cmi10_pia0->a_output());
}


WRITE8_MEMBER( cmi2x_state::cmi10_bka_w )
{
//	logerror("%s: PIA0 - BKA update %02x (CB2=%02x)\n", machine().describe_context(), data, m_cmi10_pia0->cb2_output());
	if (m_cmi10_pia0->cb2_output() == 0)
		updateLed(this, m_cmi10_pia0->b_output(), data);

	if (m_cmi10_pia0->ca2_output() == 1)
		updateSwitchlamp((data & 0x40)>>6,(data & 0x80)>>7);
}

// PB0,1 -> DA1,0
// PB2-7 -> CS2,CU2,CS1,CU1,CS0,CU0
// CB2 -> /DWS
WRITE8_MEMBER( cmi2x_state::cmi10_ledctr_w )
{
//	logerror("%s: PIA0 - CS/U update %02x (CB2=%02x)\n", machine().describe_context(), data, m_cmi10_pia0->cb2_output());
	if (m_cmi10_pia0->cb2_output() == 0)
		updateLed(this, data, m_cmi10_pia0->a_output());
}


// Alphanumeric keyboard comm
static ACIA6850_INTERFACE( cmi10_acia0_intf )
{
	9600, //	int	m_tx_clock;
	9600, //	int	m_rx_clock;
	DEVCB_DEVICE_LINE_MEMBER(TAG_IKB1_PIA, pia6821_device, cb2_r), //	devcb_read_line		m_in_rx_cb;
	DEVCB_NULL, //	devcb_write_line	m_out_tx_cb;
	DEVCB_DEVICE_LINE_MEMBER(TAG_IKB1_PIA, pia6821_device, ca2_r), //	devcb_read_line		m_in_cts_cb;
	DEVCB_DEVICE_LINE_MEMBER(TAG_IKB1_PIA, pia6821_device, ca1_w), //	devcb_write_line	m_out_rts_cb;
	DEVCB_NULL, //	devcb_read_line		m_in_dcd_cb;
	DEVCB_NULL //	devcb_write_line	m_out_irq_cb;
};

static ACIA6850_INTERFACE( cmi10_acia1_intf )
{
	9600, //	int	m_tx_clock;
	9600, //	int	m_rx_clock;
	DEVCB_NULL, //	devcb_read_line		m_in_rx_cb;
	DEVCB_NULL, //	devcb_write_line	m_out_tx_cb;
	DEVCB_NULL, //	devcb_read_line		m_in_cts_cb;
	DEVCB_NULL, //	devcb_write_line	m_out_rts_cb;
	DEVCB_NULL, //	devcb_read_line		m_in_dcd_cb;
	DEVCB_NULL //	devcb_write_line	m_out_irq_cb;
};

READ8_MEMBER( cmi2x_state::cmi10_dil_r )
{
//	return input_port_read( machine(), TAG_CMI10_SW4 );
	return machine().root_device().ioport(TAG_CMI10_SW4)->read();
}

// These functions update output whenever a digit is changed in the device
void cmi2x_update_ds1(device_t *device, int digit, int data)
{
	output_set_digit_value(0 + (digit ^ 3), data);
}
void cmi2x_update_ds2(device_t *device, int digit, int data)
{
	output_set_digit_value(4 + (digit ^ 3), data);
}
void cmi2x_update_ds3(device_t *device, int digit, int data)
{
	output_set_digit_value(8 + (digit ^ 3), data);
}

static TIMER_DEVICE_CALLBACK( pulse_cmi10_scnd )
{
	// There MUST be a better way to do this...
	pia6821_device *mach = (pia6821_device *)timer.machine().device(TAG_CMI10_PIA0);
	if (mach->ca1_r() == 0)
		mach->ca1_w(ASSERT_LINE);
	else
		mach->ca1_w(CLEAR_LINE);
}



static INPUT_PORTS_START( cmi2x )
	PORT_START(TAG_CMI10_SW4)
	PORT_DIPNAME(0x3F, 0x03, "Master keyboard analog sensitivity")
//	PORT_DIPSETTING(0x03, "Default")

	//
	// CMI-10: Misc. controls (Switches/pedals
	//
	PORT_START(TAG_CMI10_SWITCHES)
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_NAME("Switch 1")
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_NAME("Switch 2")
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_NAME("Pedal 1")
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_NAME("Pedal 2")
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_NAME("Pedal 3")

	PORT_START(TAG_CMI10_SLIDER1)
	PORT_BIT( 0xff, 0x00, IPT_AD_STICK_X ) PORT_SENSITIVITY(100) PORT_KEYDELTA(64)
	PORT_START(TAG_CMI10_SLIDER2)
	PORT_BIT( 0xff, 0x00, IPT_AD_STICK_Y ) PORT_SENSITIVITY(100) PORT_KEYDELTA(64)
	PORT_START(TAG_CMI10_SLIDER3)
	PORT_BIT( 0xff, 0x00, IPT_AD_STICK_Z ) PORT_SENSITIVITY(100) PORT_KEYDELTA(64)
	PORT_START(TAG_CMI10_PEDAL1)
	PORT_BIT( 0xff, 0x00, IPT_PEDAL ) PORT_SENSITIVITY(100) PORT_KEYDELTA(64)
	PORT_START(TAG_CMI10_PEDAL2)
	PORT_BIT( 0xff, 0x00, IPT_PEDAL2 ) PORT_SENSITIVITY(100) PORT_KEYDELTA(64)
	PORT_START(TAG_CMI10_PEDAL3)
	PORT_BIT( 0xff, 0x00, IPT_PEDAL3 ) PORT_SENSITIVITY(100) PORT_KEYDELTA(64)

	//
	// CMI-10: Master keyboard - 73 keys C0-C6
	//
	PORT_INCLUDE( cmi_master_keyboard )

	PORT_INCLUDE( cmi_slave_keyboard )

	// 1 2 3 A
	// 4 5 6 B
	// 7 8 9 C
	// * 0 # D
	// 1 4 * 7 2 5 0 8 3 6 # 9 A B D C
	//
	// CMI-10: Numeric keypad
	//
	PORT_INCLUDE( cmi_numeric_keyboard )

	PORT_START(TAG_IKB1_SWOPT)
	PORT_DIPNAME(0x07, 0x00, "Baud rate")
	PORT_DIPSETTING(0x00,"9600")
	PORT_DIPSETTING(0x01,"600")
	PORT_DIPSETTING(0x02,"2400")
	PORT_DIPSETTING(0x03,"150")
	PORT_DIPSETTING(0x04,"4800")
	PORT_DIPSETTING(0x05,"300")
	PORT_DIPSETTING(0x06,"1200")
	PORT_DIPSETTING(0x07,"110")
	PORT_DIPUNUSED(0x08, 0x00)
	PORT_DIPNAME(0x30, 0x10, "Parity")
	PORT_DIPSETTING(0x00,"Even")
	PORT_DIPSETTING(0x10,"Odd")
	PORT_DIPSETTING(0x20,"None, bit7=0")
	PORT_DIPSETTING(0x30,"None, bit7=1")

	//
	// IKB1: Alphanumeric keyboard
	//
	PORT_INCLUDE( cmi_alphanum_keyboard )

INPUT_PORTS_END

static MACHINE_CONFIG_START( cmi2x, cmi2x_state )
	// Q209 - Dual CPU card
	MCFG_CPU_ADD(TAG_Q209_P1, M6809, CPU_CLOCK)  // P1 -
    MCFG_CPU_PROGRAM_MAP(cmi2x_p1_mem)
    MCFG_CPU_ADD(TAG_Q209_P2, M6809, CPU_CLOCK)  // P1 -
    MCFG_CPU_PROGRAM_MAP(cmi2x_p2_mem)

    // QFC9 - Floppy controller
    MCFG_FD1791_ADD(TAG_QFC9_FDC, fdc_intf)//default_wd17xx_interface_2_drives)  // WD1791 disk controller 2MHz clock derived from 16mhz master on card
	MCFG_LEGACY_FLOPPY_2_DRIVES_ADD(cmi2x_floppy_interface)

    // Q133 - Processor control
    MCFG_PIA6821_ADD(TAG_Q133_PIA0, pia0_intf)
    MCFG_PIA6821_ADD(TAG_Q133_PIA1, pia1_intf)
    MCFG_MSM5832_ADD(TAG_Q133_RTC, XTAL_32_768kHz)
    MCFG_ACIA6551_ADD(TAG_Q133_ACIA0)
    MCFG_ACIA6551_ADD(TAG_Q133_ACIA1)
    MCFG_ACIA6551_ADD(TAG_Q133_ACIA2)
    MCFG_ACIA6551_ADD(TAG_Q133_ACIA3)
    MCFG_PTM6840_ADD(TAG_Q133_TIMER, timr_intf)
    MCFG_I8214_ADD(TAG_Q133_PIC_P1, CPU_CLOCK, pic_p1_intf)
    MCFG_I8214_ADD(TAG_Q133_PIC_P2, CPU_CLOCK, pic_p2_intf)

    // MC003 - Alphanumeric keyboard
    MCFG_CPU_ADD(TAG_IKB1_CPU, M6802, CLK_MC003_CPU)
    MCFG_CPU_PROGRAM_MAP(cmi2x_mc003_mem)
    MCFG_PIA6821_ADD(TAG_IKB1_PIA, ikb1_pia_intf) // Active key input + A/D conv input
    MCFG_TIMER_ADD_PERIODIC(TAG_IKB1_COMMTMR, pulse_ikb1_pia, attotime::from_hz( CLK_MC003_CPU / 200 ))


    // MC004 - Musical keyboard
    MCFG_CPU_ADD(TAG_CMI10_CPU, M6802, CLK_MC004_CPU)
    MCFG_CPU_PROGRAM_MAP(cmi2x_mc004_mem)
    MCFG_DL1416B_ADD(TAG_CMI10_DISP0, cmi2x_update_ds1)
    MCFG_DL1416B_ADD(TAG_CMI10_DISP1, cmi2x_update_ds2)
    MCFG_DL1416B_ADD(TAG_CMI10_DISP2, cmi2x_update_ds3)
    MCFG_PIA6821_ADD(TAG_CMI10_PIA1, cmi10_piain_intf) // Active key input + A/D conv input
    MCFG_PIA6821_ADD(TAG_CMI10_PIA0, cmi10_piaout_intf) // Key address output
    MCFG_ACIA6850_ADD(TAG_CMI10_ACIA0, cmi10_acia0_intf) // Alphanumeric keyboard communication
    MCFG_ACIA6850_ADD(TAG_CMI10_ACIA1, cmi10_acia1_intf) // CMI communication
    MCFG_TIMER_ADD_PERIODIC(TAG_CMI10_SCND, pulse_cmi10_scnd, attotime::from_hz( CLK_MC004_CPU / 1024 )) // SCND half-cycle toggle

    MCFG_DEFAULT_LAYOUT(layout_cmi)

    MCFG_SCREEN_ADD( "screen", RASTER )
	MCFG_SCREEN_UPDATE_DRIVER(cmi2x_state, screen_update)
//	MCFG_SCREEN_SIZE( 512, 256 )
//	MCFG_SCREEN_REFRESH_RATE( 30 )
	MCFG_SCREEN_RAW_PARAMS(10380000, 512, 0, 511, 256, 0, 255)
	MCFG_PALETTE_LENGTH( 2 )
	MCFG_PALETTE_INIT( monochrome_green )

//	MCFG_VIDEO_START( cmi2x )
//	MCFG_SCREEN_UPDATE( cmi2x ) // generic_bitmapped
MACHINE_CONFIG_END

/* ROM definition */
ROM_START( cmi2x )
	// Q133 - Processor control
	ROM_REGION( 0x10000, TAG_Q209_P1, ROMREGION_ERASEFF )
	ROM_LOAD( "q9f0mrk1.bin", 0xf000, 0x0800, CRC(16f195cc) )
	ROM_LOAD( "f8lmrk5.bin", 0xf800, 0x0800, CRC(cfc7967f) )

	ROM_REGION( 0x10000, TAG_Q209_P2, ROMREGION_ERASEFF )
	ROM_LOAD( "q9f0mrk1.bin", 0xf000, 0x0800, CRC(16f195cc) )
	ROM_LOAD( "f8lmrk5.bin", 0xf800, 0x0800, CRC(cfc7967f) )

	// QFC9 - Floppy controller
	ROM_REGION( 0x10000, "fdc", ROMREGION_ERASEFF )
	ROM_LOAD( "dqfc911.bin", 0x0000, 0x0800, CRC(5bc38db2) )

	// MC003 - Alphanumeric keyboard
	ROM_REGION( 0x10000, TAG_IKB1_CPU, ROMREGION_ERASEFF )
	ROM_LOAD( "cmikeys4.bin", 0xc000, 0x0400,  CRC(b214fbe9) ) // Circuitry implies f000-ffff, but rom content relies on base in c000

	// MC004 - Musical keyboard
	ROM_REGION( 0x10000, TAG_CMI10_CPU, ROMREGION_ERASEFF )
	ROM_LOAD( "velkeysd.bin", 0xb000, 0x0400, CRC(9b636781) )
	ROM_LOAD( "kbdioa.bin", 0xfc00, 0x0400, CRC(a5cbe218) )

	ROM_REGION( 0x40000, TAG_Q256_RAM, ROMREGION_ERASEFF )
	
	ROM_REGION( 0x04220, TAG_Q219_VRAM, ROMREGION_ERASEVAL(0x55))
	//ROM_LOAD( "srom.bin", 0x4000, 0x0200, CRC(DA3C0EBD) )
	ROM_LOAD( "wrom.bin", 0x4200, 0x0020, CRC(68A9E17F) )
	
	ROM_REGION( 0x00800, TAG_Q133_MAPRAM, ROMREGION_ERASEFF )

	// CMI-28 general i/f
//	ROM_REGION( 0x10000, "cmi28", ROMREGION_ERASEFF )
//	ROM_LOAD( "mon1110E.bin", 0x0000, 0x2000, CRC(476f7d5f) )
//	ROM_LOAD( "mon1110O.bin", 0x0000, 0x2000, CRC(150c8ebe) )

	// Q219 lightpen/graphics
//	ROM_REGION( 0x10000, "q219", ROMREGION_ERASEFF )
//	ROM_LOAD( "brom.bin", 0x0000, 0x0200, CRC(aa246081) )
//	ROM_LOAD( "srom.bin", 0x0000, 0x0200, CRC(da3c0ebd) )
//	ROM_LOAD( "wrom.bin", 0x0000, 0x0020, CRC(68a9e17f) )

	// CMI-02 Master card
//	ROM_REGION( 0x10000, "cmi02", ROMREGION_ERASEFF )
//	ROM_LOAD( "mrom.bin", 0x0000, 0x0200, CRC(23c1ff04) )
//	ROM_LOAD( "timrom.bin", 0x0000, 0x0200, CRC(9984d0c1) )

ROM_END


/*    YEAR  NAME    PARENT  COMPAT   MACHINE    INPUT    INIT    COMPANY   FULLNAME       FLAGS */
COMP( 1983, cmi2x,  0,      0,       cmi2x,     cmi2x,   driver_device, 0,      "Fairlight", "CMI IIx",  GAME_NO_SOUND )
//COMP(1985,ibm5162,ibm5170, 0,       ibm5162,   atcga,  atcga,      "International Business Machines",  "IBM PC/XT-286 5162", GAME_NOT_WORKING )
//
// Q219: Graphics + lightpen
//

/*
 * VRAM at 8000-BFFF
 *
 * FCD0 just store at current position
 * FCD1 store and inc Y
 * FCD2 store and dec Y
 * FCD3 store as byte at current position
 * FCD4 store and inc X
 * FCD5 store and inc X, inc Y
 * FCD6 store and inc X, dec Y
 * FCD7 store as byte, inc X
 * FCD8 store and dec X
 * FCD9 store and dec X, inc Y
 * FCDA store and dec X, dec Y
 * FCDB store as byte, dec X
 *
 * VRAM read sets position
 *
 * FCC4 scroll latch (port A PIA)
 *
 * PIA + timer
 *
 */
