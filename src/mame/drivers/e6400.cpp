// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    Skeleton driver for the E-mu E6400 sampler.

    Hardware:
    - MC68EC020 CPU @ 22.5792 MHz (45.1584 MHz audio XTAL ÷2 via D-latch toggle)
    - S82078 / N82077AA FDC controller (24 MHz crystal)
    - Sharp LM24014H LCD unit (T6963C-based, 240x64)
    - MC68901 MFP (MIDI UART, timers, GPIO)
    - AM85C80-16JC SCSI + SCC (Z85C30) controller
    - 2x HM514260 DRAM (256Kx16-bit, total 1 Mbit)
    - IDT7202SO 1Kx9 dual-port FIFO buffer
    - CS8411-CP Digital Audio Interface Receiver (S/PDIF)
    - CS8402A-CP Digital Audio Interface Transmitter (S/PDIF)
    - AMI E-MU IC413 Rev A 9807NMQ 6753-501
    - 4x AD1861 18-bit DAC

    PALs:
    - W5 IP822D (c)EMU '96 9811 - MEM_PAL
    - W5 IP872A (c)EMU '96 9808 - CS_PAL
    - W5 IP751B.1 EMU 1098

    ROM memory map (CPU addresses, confirmed from MAME debugger cross-reference with IDA):
    000000-0001FF: vector table (hardware mirror of flash[0x400:0x5FF] via CS_PAL)
    000200-0003FF: scratch RAM
    010000-0FF3FF: flash ROM (eos30b.raw = 1024-byte header + code, base CPU 010000)
      010000-0103FF: flash header (magic 0x12345678, "EOS v3.00b", sector count, checksum)
      010400-0105FF: vector table bytes (also visible at 000000)
      010600-0107FF: 0xFF padding (erased flash)
      010800-0FF3FF: firmware code and data (IDA "eOS_ROM" segment starts here)

    Peripheral address map (active when A23=0, A22=1, A21=0 via CS_PAL ENBIO1):

    Decoder #3, via CNTRLSEL (A14..A16 select):
    400000-403FFF:  CSWCR1 — control register 1 (16-bit write latch, confirmed)
    404000-407FFF:  CSWCR2 — control register 2 (16-bit write latch, confirmed)
    408000-40BFFF:  CSMSR — misc status register (16-bit read, confirmed)
                      bits[15:12]= EEPROMD,FIFOF,FIFOHF,SROMBSY; bits[11:8]= HW variant
    40C000-40FFFF:  CSLED — LED latch (16-bit write; bits 0-11 LEDs, 12-15 LCD contrast)
    414000-417FFF:  CSAESRX — CS8411 AES/EBU digital audio receiver

    Decoder #2 (A20=0, A17..A19 select):
    420000-43FFFF:  CSG1CHIP — G-chip 1 sound engine (polyphony board)
    440000-45FFFF:  CSG2CHIP — G-chip 2 sound engine (polyphony board)
    460000-47FFFF:  CSHCHIP — H-chip digital filter IC413 (+ polyphony connector)
    480000-49FFFF:  CSHDC — AM85C80 SCSI controller (NCR5380-compatible)
    4A0000-4BFFFF:  CSHDD — SCSI DMA data port (via memory PAL IP822)
    4C0000-4DFFFF:  CSSCC — AM85C80 SCC (Z85C30-compatible DUART)
    4E0000-4FFFFF:  CSRFIFO — IDT7202 1Kx9 sampling FIFO buffer (read)

    Decoder #1 (A20=1, A17..A19 select):
    500000-51FFFF:  CSKCHIP — K-chip key scanner IT433 (A0-A3 addr, D[15:8] data)
    520000-53FFFF:  CSDSP — DSP daughter card (effects processor)
    540000-55FFFF:  CSEXP — expansion daughter card (effects DSP RAM at +0x400)
    560000-57FFFF:  CSFDC — 82078 FDC (confirmed)
    580000-59FFFF:  CSLCD — LM24014H LCD / T6963C (confirmed)
    5A0000-5BFFFF:  CSMFP — MC68901 MFP (confirmed)
    5C0000-5DFFFF:  CSWGAIN — sample gain latch (write, D[15:8])
                      bits 0-5 gain, bit 7 BIGEECS (big EEPROM CS)
    5E0000-5FFFFF:  CSRJACK — jack detection latch (read, D[15:8])
                      bits 2-7 jack status

    F00000-FFFFFF:  CPU DRAM (2x HM514260 256Kx16-bit)

    Boot notes:
    - eos30b.raw IS the cold-boot ROM; no separate bootprom exists.
    - reset() at CPU 0x034FD0 runs immediately on power-on via vector alias at 0x000000.
    - reset() copies 0x6000 bytes of API jump table from ROM (0x0F9400) to DRAM (0xF00400)
      then calls bootSystem() which performs all hardware and OS initialization.
    - "RTC" time base is a software counter (timer_value) incremented by MFP timer ISR.
    - Hardware variant from register at 0x408000 bits[11:8] (4 bits from HW, firmware reads [11:9]).

******************************************************************************/

#include "emu.h"

#include "cpu/m68000/m68020.h"
#include "machine/mc68901.h"
#include "machine/upd765.h"
#include "video/t6963c.h"

#include "formats/pc_dsk.h"
#include "imagedev/floppy.h"


namespace {

class e6400_state : public driver_device
{
public:
	e6400_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_mfp(*this, "mfp")
		, m_lcd(*this, "lcd")
		, m_fdc(*this, "fdc")
		, m_leds(*this, "led%u", 0U)
		, m_keys(*this, "SC%u", 0U)
		, m_encoder(*this, "ENCODER")
		, m_volume(*this, "VOLUME")
	{
	}

	void e6400(machine_config &config);

private:
	required_device<m68ec020_device> m_maincpu;
	required_device<mc68901_device> m_mfp;
	required_device<lm24014h_device> m_lcd;
	required_device<n82077aa_device> m_fdc;
	output_finder<12> m_leds;
	required_ioport_array<6> m_keys;
	required_ioport m_encoder;
	required_ioport m_volume;

	u16 m_cr1 = 0;
	u16 m_cr2 = 0;

	// IT433 K-chip scanner state
	emu_timer *m_kchip_scan_timer = nullptr;
	u8 m_kchip_status = 0;
	u8 m_kchip_control = 0;
	u8 m_kchip_key = 0;
	u8 m_kchip_vel = 0;
	s8 m_kchip_enc_delta = 0;
	u16 m_kchip_pot = 0;
	u8 m_prev_keys[6] = {};
	u8 m_prev_encoder = 0;
	static constexpr int KCHIP_FIFO_SIZE = 32;
	u8 m_kchip_fifo_code[KCHIP_FIFO_SIZE] = {};
	u8 m_kchip_fifo_vel[KCHIP_FIFO_SIZE] = {};
	int m_kchip_fifo_head = 0;
	int m_kchip_fifo_tail = 0;
	int m_kchip_fifo_count = 0;

	void machine_start() override ATTR_COLD;
	void machine_reset() override ATTR_COLD;
	void mem_map(address_map &map) ATTR_COLD;
	void cpuspace_map(address_map &map) ATTR_COLD;

	void cr1_w(u16 data);
	void cr2_w(u16 data);
	u16 status_r();
	void led_w(u16 data);
	u8 lcd_r();
	void lcd_w(offs_t offset, u8 data);
	void gain_w(u8 data);
	u8 jack_r();

	TIMER_CALLBACK_MEMBER(kchip_scan);
	void kchip_update_irq();
	u8 kchip_r(offs_t offset);
	void kchip_w(offs_t offset, u8 data);
};

void e6400_state::machine_start()
{
	m_leds.resolve();

	m_kchip_scan_timer = timer_alloc(FUNC(e6400_state::kchip_scan), this);

	save_item(NAME(m_cr1));
	save_item(NAME(m_cr2));
	save_item(NAME(m_kchip_status));
	save_item(NAME(m_kchip_control));
	save_item(NAME(m_kchip_key));
	save_item(NAME(m_kchip_vel));
	save_item(NAME(m_kchip_enc_delta));
	save_item(NAME(m_kchip_pot));
	save_item(NAME(m_prev_keys));
	save_item(NAME(m_prev_encoder));
	save_item(NAME(m_kchip_fifo_code));
	save_item(NAME(m_kchip_fifo_vel));
	save_item(NAME(m_kchip_fifo_head));
	save_item(NAME(m_kchip_fifo_tail));
	save_item(NAME(m_kchip_fifo_count));
}

void e6400_state::machine_reset()
{
	m_cr1 = 0;
	m_cr2 = 0;

	// Reset IT433 K-chip state
	m_kchip_status = 0;
	m_kchip_control = 0;
	m_kchip_key = 0;
	m_kchip_vel = 0;
	m_kchip_enc_delta = 0;
	m_kchip_pot = 0;
	std::fill(std::begin(m_prev_keys), std::end(m_prev_keys), 0);
	m_prev_encoder = 0;
	m_kchip_fifo_head = 0;
	m_kchip_fifo_tail = 0;
	m_kchip_fifo_count = 0;

	// Start K-chip scan timer at 200 Hz (5ms period)
	m_kchip_scan_timer->adjust(attotime::from_hz(200), 0, attotime::from_hz(200));
}

void e6400_state::cr1_w(u16 data)
{
	m_cr1 = data;
}

void e6400_state::cr2_w(u16 data)
{
	m_cr2 = data;
}

u16 e6400_state::status_r()
{
	// CSMSR — misc status register
	// bits[15:12] = EEPROMD, FIFOF, FIFOHF, SROMBSY
	// bits[11:8]  = hardware variant (4 bits from schematic; firmware reads [11:9] via bfextu)
	// E6400 schematic: bits[11:8] = 0101 → firmware bfextu{20:3} reads bits[11:9] = 010 = 2 → variant_id 4
	return 0x0500; // bits[11:8] = 0b0101 (E6400)
}

void e6400_state::led_w(u16 data)
{
	// CSLED — bits 0-11 = front panel LEDs, bits 12-15 = LCD contrast DAC
	for (int i = 0; i < 12; i++)
		m_leds[i] = BIT(data, i);
}

u8 e6400_state::lcd_r()
{
	return m_lcd->read(BIT(m_cr1, 8));
}

void e6400_state::lcd_w(offs_t offset, u8 data)
{
	m_lcd->write(BIT(m_cr1, 8), data);
}

void e6400_state::gain_w(u8 data)
{
	// CSWGAIN — sample gain latch (bits 0-5 = gain, bit 7 = big EEPROM CS)
}

u8 e6400_state::jack_r()
{
	// CSRJACK — jack detection latch (bits 2-7 = jack status)
	return 0x00;
}

// IT433 K-chip key scanner emulation
//
// The IT433 continuously scans a 6×8 button matrix, a rotary encoder, and a
// volume potentiometer ADC.  When data is available it asserts KCHPINT (active
// low → MFP GP4).  The ISR (ISR_User7 at 0x350F0) loops calling the key-read
// routine sub_21AD0, which reads one key event and one encoder event per call,
// until the IT433 deasserts KCHPINT (indicating its internal FIFO is empty).
//
// Register map (A1-A4 address → 16 word-aligned registers, 8-bit data on D8-D15):
//   Reg 0 (0x500000) R  — key code: bits[6:0]=key number, bit 7=release
//   Reg 1 (0x500002) R  — key velocity: raw 8-bit (firmware maps: vel-0x68, clamp 1-127)
//   Reg 2 (0x500004) R  — status: bit 7=key ready, bit 6=pot ready, bit 5=encoder ready
//   Reg 3 (0x500006) R  — pot MSB: upper 8 bits of 11-bit pot ADC value
//   Reg 4 (0x500008) R  — pot LSB: lower 3 bits of 11-bit pot ADC value
//   Reg 5 (0x50000A) R  — encoder delta: signed 8-bit relative movement
//   Reg 7 (0x50000E) W  — control: bit 7=enable, bits[3:2]=MIDI LEDs, bit 0=scan config

TIMER_CALLBACK_MEMBER(e6400_state::kchip_scan)
{
	// Scan 6×8 key matrix for press/release transitions
	for (int col = 0; col < 6; col++)
	{
		u8 curr = m_keys[col]->read();
		u8 changed = curr ^ m_prev_keys[col];
		for (int row = 0; row < 8; row++)
		{
			if (BIT(changed, row) && m_kchip_fifo_count < KCHIP_FIFO_SIZE)
			{
				u8 key_num = col * 8 + row + 0x51; // button codes start at 0x51 (>0x50)
				u8 code = BIT(curr, row) ? key_num : (key_num | 0x80); // bit 7 = release
				m_kchip_fifo_code[m_kchip_fifo_tail] = code;
				m_kchip_fifo_vel[m_kchip_fifo_tail] = 0xa0; // fixed velocity
				m_kchip_fifo_tail = (m_kchip_fifo_tail + 1) % KCHIP_FIFO_SIZE;
				m_kchip_fifo_count++;
			}
		}
		m_prev_keys[col] = curr;
	}

	// Scan rotary encoder for rising edges
	u8 enc = m_encoder->read();
	if ((enc & 1) && !(m_prev_encoder & 1))
		m_kchip_enc_delta++;
	if ((enc & 2) && !(m_prev_encoder & 2))
		m_kchip_enc_delta--;
	m_prev_encoder = enc;

	// Sample volume pot ADC (always converting)
	m_kchip_pot = m_volume->read() * 2047 / 100;

	// Update status register
	if (m_kchip_fifo_count > 0)
		m_kchip_status |= 0x80;
	m_kchip_status |= 0x40; // pot data always ready
	if (m_kchip_enc_delta != 0)
		m_kchip_status |= 0x20;

	kchip_update_irq();
}

void e6400_state::kchip_update_irq()
{
	// KCHPINT is active low — asserted when key or encoder data available
	bool active = (m_kchip_status & 0xa0) != 0;
	m_mfp->i4_w(active ? 0 : 1);
}

u8 e6400_state::kchip_r(offs_t offset)
{
	switch (offset)
	{
	case 0: // key code — reading dequeues one event from FIFO
		if (m_kchip_fifo_count > 0)
		{
			m_kchip_key = m_kchip_fifo_code[m_kchip_fifo_head];
			m_kchip_vel = m_kchip_fifo_vel[m_kchip_fifo_head];
			m_kchip_fifo_head = (m_kchip_fifo_head + 1) % KCHIP_FIFO_SIZE;
			m_kchip_fifo_count--;
			if (m_kchip_fifo_count == 0)
				m_kchip_status &= ~0x80;
			kchip_update_irq();
		}
		return m_kchip_key;

	case 1: // key velocity (latched when key code was read)
		return m_kchip_vel;

	case 2: // status (bit 7=key, bit 6=pot, bit 5=encoder)
		return m_kchip_status;

	case 3: // pot data MSB — reading clears pot-ready flag
		m_kchip_status &= ~0x40;
		return u8(m_kchip_pot >> 3);

	case 4: // pot data LSB (bits 2:0)
		return u8(m_kchip_pot & 7);

	case 5: // encoder delta — signed byte, reading clears encoder flag
	{
		u8 delta = u8(m_kchip_enc_delta);
		m_kchip_enc_delta = 0;
		m_kchip_status &= ~0x20;
		kchip_update_irq();
		return delta;
	}

	case 7: // control register readback
		return m_kchip_control;

	default:
		return 0;
	}
}

void e6400_state::kchip_w(offs_t offset, u8 data)
{
	switch (offset)
	{
	case 7: // control register
		m_kchip_control = data;
		break;
	}
}

void e6400_state::mem_map(address_map &map)
{
	map(0x000000, 0x0001ff).rom().region("eos_flash", 0x400);    // vector table mirror (CS_PAL maps flash[0x400:0x5FF])
	map(0x000200, 0x0003ff).ram();                                // scratch RAM
	map(0x010000, 0x0ff3ff).rom().region("eos_flash", 0);        // flash ROM at CPU base (header + code)

	// Decoder #3 (via CNTRLSEL, A14..A16 select) — 0x400000-0x41FFFF
	map(0x400000, 0x400001).w(FUNC(e6400_state::cr1_w));         // CSWCR1 — control register 1
	map(0x404000, 0x404001).w(FUNC(e6400_state::cr2_w));         // CSWCR2 — control register 2
	map(0x408000, 0x408001).r(FUNC(e6400_state::status_r));      // CSMSR — misc status register
	map(0x40c000, 0x40c001).w(FUNC(e6400_state::led_w));         // CSLED — front panel LEDs + LCD contrast DAC
	// 0x414000: CSAESRX — CS8411 AES/EBU receiver

	// Decoder #2 (A20=0, A17..A19 select) — 0x420000-0x4FFFFF
	// 0x420000: CSG1CHIP — G-chip 1 sound engine (polyphony board)
	// 0x440000: CSG2CHIP — G-chip 2 sound engine (polyphony board)
	// 0x460000: CSHCHIP — H-chip digital filter IC413
	// 0x480000: CSHDC — AM85C80 SCSI controller (NCR5380)
	// 0x4A0000: CSHDD — SCSI DMA data port (via memory PAL IP822)
	// 0x4C0000: CSSCC — AM85C80 SCC (Z85C30 DUART)
	// 0x4E0000: CSRFIFO — IDT7202 sampling FIFO

	// Decoder #1 (A20=1, A17..A19 select) — 0x500000-0x5FFFFF
	map(0x500000, 0x50001f).rw(FUNC(e6400_state::kchip_r), FUNC(e6400_state::kchip_w)).umask16(0xff00); // CSKCHIP — IT433 key scanner
	// 0x520000: CSDSP — DSP daughter card (effects processor)
	// 0x540000: CSEXP — expansion daughter card
	map(0x560000, 0x560007).m(m_fdc, FUNC(n82077aa_device::map)); // CSFDC
	map(0x580000, 0x580003).rw(FUNC(e6400_state::lcd_r), FUNC(e6400_state::lcd_w)); // CSLCD
	map(0x5a0000, 0x5a002f).rw(m_mfp, FUNC(mc68901_device::read), FUNC(mc68901_device::write)).umask16(0xff00); // CSMFP
	map(0x5c0000, 0x5c0001).w(FUNC(e6400_state::gain_w)).umask16(0xff00);  // CSWGAIN
	map(0x5e0000, 0x5e0001).r(FUNC(e6400_state::jack_r)).umask16(0xff00);  // CSRJACK

	map(0xf00000, 0xffffff).ram(); // 2x HM514260 256Kx16-bit DRAM
}

void e6400_state::cpuspace_map(address_map &map)
{
	// MC68020 CPU space: autovectors for all levels, MFP gets level-6 IACK slot
	// Level N IACK address = 0xfffffff1 + N*2; level 6 → 0xfffffffd
	// TODO: confirm IPL6 from schematic (assumed by analogy with MC68901/VR=0x48 designs)
	map(0xfffffff0, 0xffffffff).m(m_maincpu, FUNC(m68ec020_device::autovectors_map));
	map(0xfffffffd, 0xfffffffd).r(m_mfp, FUNC(mc68901_device::get_vector));
}

static void e6400_floppies(device_slot_interface &device)
{
	device.option_add("35hd", FLOPPY_35_HD);
}

void e6400_state::e6400(machine_config &config)
{
	M68EC020(config, m_maincpu, 45.1584_MHz_XTAL / 2); // 22.5792 MHz — 45.1584 MHz audio XTAL ÷2 via D-latch toggle
	m_maincpu->set_addrmap(AS_PROGRAM, &e6400_state::mem_map);
	m_maincpu->set_addrmap(m68000_base_device::AS_CPU_SPACE, &e6400_state::cpuspace_map);

	N82077AA(config, m_fdc, 24_MHz_XTAL, n82077aa_device::mode_t::AT);
	m_fdc->intrq_wr_callback().set(m_mfp, FUNC(mc68901_device::i0_w)); // FDCINT → MFP GP0
	FLOPPY_CONNECTOR(config, "fdc:0", e6400_floppies, "35hd", floppy_image_device::default_pc_floppy_formats);

	// MC68901 MFP — MIDI UART, timers (system tick), GPIO
	// Timer clock: 16 MHz XTAL (U56/ZX314) ÷4 via HC393 binary counter Q1 = 4 MHz
	// VR=0x48 → vectors 0x120-0x15F; timer ISR at 0x120 increments software timer
	// GPIO interrupt inputs:
	//   GP0 = FDCINT (FDC, active rising edge)
	//   GP1 = ROMWRINT (flash ROM write, from PAL IP822, active rising edge)
	//   GP2 = FIFOHF (sample FIFO half-full, from IDT7202)
	//   GP3 = HDCINT (SCSI, active rising edge)
	//   GP4 = KCHPINT (key scanner, from PAL IT433)
	//   GP5 = EXPINT2 (expansion bus interrupt #2)
	//   GP6 = EXPINT1 (expansion bus interrupt #1)
	//   GP7 = SCCINT (SCC Z85C30)
	MC68901(config, m_mfp, 16_MHz_XTAL / 4); // 4 MHz — 16 MHz XTAL ÷4 via HC393 Q1
	m_mfp->set_timer_clock(16_MHz_XTAL / 4);
	m_mfp->out_irq_cb().set_inputline(m_maincpu, M68K_IRQ_6); // IPL6 (assumed; IACK at 0xfffffffd)

	LM24014H(config, m_lcd, 0);
	m_lcd->set_fs(0); // font size 6x8
}

// K-chip IT433 key matrix: 6 scan columns (SC0-SC5) × 8 scan rows (SI0-SI7)
static INPUT_PORTS_START( e6400 )
	PORT_START("SC0")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Return to Zero")  PORT_CODE(KEYCODE_HOME)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Rewind")          PORT_CODE(KEYCODE_COMMA)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Fast Forward")    PORT_CODE(KEYCODE_STOP)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Stop")            PORT_CODE(KEYCODE_SPACE)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Play")            PORT_CODE(KEYCODE_ENTER)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Record")          PORT_CODE(KEYCODE_R)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Sequencer")       PORT_CODE(KEYCODE_Q)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Preset Manage")   PORT_CODE(KEYCODE_F9)

	PORT_START("SC1")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Sample Manage")   PORT_CODE(KEYCODE_F10)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Preset Edit")     PORT_CODE(KEYCODE_F11)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Sample Edit")     PORT_CODE(KEYCODE_F12)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Master")          PORT_CODE(KEYCODE_M)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Disk")            PORT_CODE(KEYCODE_D)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Exit")            PORT_CODE(KEYCODE_ESC)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Assignable #1")   PORT_CODE(KEYCODE_A)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Assignable #2")   PORT_CODE(KEYCODE_S)

	PORT_START("SC2")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F1")              PORT_CODE(KEYCODE_F1)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Assignable #3")   PORT_CODE(KEYCODE_Z)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F2")              PORT_CODE(KEYCODE_F2)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_UNUSED)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F3")              PORT_CODE(KEYCODE_F3)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Controls/FX")     PORT_CODE(KEYCODE_X)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F4")              PORT_CODE(KEYCODE_F4)

	PORT_START("SC3")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Page Prev")       PORT_CODE(KEYCODE_PGUP)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F5")              PORT_CODE(KEYCODE_F5)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Page Next")       PORT_CODE(KEYCODE_PGDN)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("F6")              PORT_CODE(KEYCODE_F6)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Enter")           PORT_CODE(KEYCODE_ENTER_PAD)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Up")              PORT_CODE(KEYCODE_UP)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Left")            PORT_CODE(KEYCODE_LEFT)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Right")           PORT_CODE(KEYCODE_RIGHT)

	PORT_START("SC4")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Down")            PORT_CODE(KEYCODE_DOWN)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Dec")             PORT_CODE(KEYCODE_MINUS)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Inc")             PORT_CODE(KEYCODE_EQUALS)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("0 / QZ")          PORT_CODE(KEYCODE_0)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("1")               PORT_CODE(KEYCODE_1)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("2 / ABC")         PORT_CODE(KEYCODE_2)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("3 / DEF")         PORT_CODE(KEYCODE_3)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("4 / GHI")         PORT_CODE(KEYCODE_4)

	PORT_START("SC5")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("5 / JKL")         PORT_CODE(KEYCODE_5)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("6 / MNO")         PORT_CODE(KEYCODE_6)
	PORT_BIT(0x04, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("7 / PQR")         PORT_CODE(KEYCODE_7)
	PORT_BIT(0x08, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("8 / TUV")         PORT_CODE(KEYCODE_8)
	PORT_BIT(0x10, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Lock")            PORT_CODE(KEYCODE_L)
	PORT_BIT(0x20, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("9 / WXY")         PORT_CODE(KEYCODE_9)
	PORT_BIT(0x40, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Set/Shift")       PORT_CODE(KEYCODE_INSERT)
	PORT_BIT(0x80, IP_ACTIVE_HIGH, IPT_UNUSED)

	// Rotary encoder (read via IT433 register; Panel Test shows range 0-36)
	PORT_START("ENCODER")
	PORT_BIT(0x01, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Encoder CW")      PORT_CODE(KEYCODE_CLOSEBRACE)
	PORT_BIT(0x02, IP_ACTIVE_HIGH, IPT_KEYBOARD) PORT_NAME("Encoder CCW")     PORT_CODE(KEYCODE_OPENBRACE)

	// Volume pot (read via IT433 ADC; Panel Test shows range 0-255)
	PORT_START("VOLUME")
	PORT_ADJUSTER(100, "Volume")
INPUT_PORTS_END

ROM_START( e6400 )
	ROM_REGION32_BE(0x100000, "eos_flash", 0)
	ROM_LOAD( "eos30b.raw", 0x000000, 0x0ef400, CRC(8b4408fc) SHA1(BD2D1FF7ACA5F658475AD677F815A275E9BB8923) )
ROM_END

} // anonymous namespace


//    YEAR  NAME   PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY  FULLNAME               FLAGS
SYST( 1996, e6400, 0,      0,      e6400,   e6400, e6400_state, empty_init, "E-mu",  "E-6400 Sampler", MACHINE_NOT_WORKING | MACHINE_NO_SOUND )
