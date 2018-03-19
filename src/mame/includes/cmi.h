/*
 * cmi.h
 *
 *  Created on: 20 maj 2011
 *      Author: clo
 * Cards:
 * CMI-04 Audio module
 *
 * Slot  Card
 * 1     Master card (CMI-02)
 * 2     General interface card (CMI-28) (MIDI/SMPTE, optional)
 * 3-10  Channel card(s) (CMI-01-A)
 * 11    Analog interface card (CMI-07)
 * 12    64k RAM (Q096) (not used)
 * 13+14 256k RAM (Q256)
 * 15    4-port ACIA module (Q014)
 * 16    Processor control module (EPROM,RAM, ACIA, PIA) (Q133)
 * 17    Dual 6809 CPU module (Q209)
 * 18    Lightpen/Graphics interface (Q219)
 * 19    Floppy disk controller (QFC9)
 * 20    Hard disk controller (Q077)
 *
 *
 * Keyboard - 64 keys
 * 6802 @ 3.840MHz
 * E/100 -> INT (9.6kHz)
 *
 * PIA
 * PA0-8 column (in)
 * PB0-8 row (out)
 * CA1 9.6 kHz clock (in)
 * CA2 CTS (out)
 * CB1 RTS (in)
 * CB2 data (out)
 * A-Z 25
 * 0-9 10
 *
 */

#ifndef CMI_H_
#define CMI_H_

#include "emu.h"
#include "cpu/m6809/m6809.h"
#include "cpu/m6800/m6800.h"
#include "imagedev/flopdrv.h"
#include "formats/flopimg.h"
#include "formats/basicdsk.h"
#include "machine/wd_fdc.h"
#include "machine/mos6551.h"
#include "machine/i8214.h"
#include "machine/6821pia.h"
#include "machine/6840ptm.h"
#include "machine/6850acia.h"

#include "machine/msm5832.h"
#include "video/dl1416.h"
#include "cmi.lh"

//
// Clocks
//
// Main crystal (Q1) is 40.120 MHz, but fed through a toggling flip-flop halving the frequency
// There are 2 opposite clocks - one for each processor. This is unnecessary to emulate
#define MAIN_CLOCK 20105000
// A 10 stage Johnson counter divides the main clock into a ~2MHz cpu clock
#define CPU_CLOCK MAIN_CLOCK/10
#define CLK_MC003_CPU 3840000
#define CLK_MC004_CPU 4000000

//
// Device tags
//

// Main computer
#define TAG_Q209_P1 "q209_p1"
#define TAG_Q209_P2 "q209_p2"
#define TAG_Q133_PIA0 "q133_pia0"
#define TAG_Q133_PIA1 "q133_pia1"
#define TAG_Q133_RTC "q133_rtc"
#define TAG_Q133_TIMER "q133_timr"
#define TAG_Q133_ACIA0 "q133_acia0"
#define TAG_Q133_ACIA1 "q133_acia1"
#define TAG_Q133_ACIA2 "q133_acia2"
#define TAG_Q133_ACIA3 "q133_acia3"
#define TAG_Q133_PIC_P1 "q133_picp1"
#define TAG_Q133_PIC_P2 "q133_picp2"
#define TAG_Q133_MAPRAM "q133_mapram"
#define TAG_QFC9_FDC "qfc9_fdc"
#define TAG_QFC9_FD0 "qfc9_fdc:0"
#define TAG_QFC9_FD1 "qfc9_fdc:1"
#define TAG_Q219_VRAM "q219_vram"
#define TAG_Q256_RAM "q256_ram"
// Alphanumeric keyboard
#define TAG_IKB1_CPU "ikb1_cpu"
#define TAG_IKB1_PIA "ikb1_pia"
#define TAG_IKB1_COMMTMR "ikb1_tmr"
// Music keyboard
#define TAG_CMI10_CPU "cmi10_cpu"
#define TAG_CMI10_PIA0 "cmi10_pia0" /* Output (F34) */
#define TAG_CMI10_PIA1 "cmi10_pia1"	/* Input (K34) */
#define TAG_CMI10_ACIA0 "cmi10_acia0"
#define TAG_CMI10_ACIA1 "cmi10_acia1"
#define TAG_CMI10_DISP0 "cmi10_disp0"
#define TAG_CMI10_DISP1 "cmi10_disp1"
#define TAG_CMI10_DISP2 "cmi10_disp2"
#define TAG_CMI10_SCND "cmi10_scnd"


#define TAG_CMI10_SW4 "CMI10_SW4"
#define TAG_CMI10_SWITCHES "CMI10_SWITCH"
#define TAG_CMI10_SLIDER1 "CMI10_SLIDER1"
#define TAG_CMI10_SLIDER2 "CMI10_SLIDER2"
#define TAG_CMI10_SLIDER3 "CMI10_SLIDER3"
#define TAG_CMI10_PEDAL1 "CMI10_PEDAL1"
#define TAG_CMI10_PEDAL2 "CMI10_PEDAL2"
#define TAG_CMI10_PEDAL3 "CMI10_PEDAL3"

#define TAG_IKB1_SWOPT "IKB1_SWOPT"

#define DSKSTATUS_RDY  0x08 /* Disk ready */
#define DSKSTATUS_2SID 0x10 /* Two sided */
#define DSKSTATUS_DCHG 0x20 /* Disk change */
#define DSKSTATUS_INT  0x40 /* Interrupt */
#define DSKSTATUS_DTRQ 0x40 /* Device driver loading (active low) */

#endif /* CMI_H_ */
