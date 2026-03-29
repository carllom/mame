// license:BSD-3-Clause
// copyright-holders:AJR
/****************************************************************************

    Roland RF5C36 (15229840) & SA-16 (15229874) Sampler Custom ICs

    Skeleton devices.

    Waveform data is 12 bits, and is normally stored in DRAM banks, though
    at least one Roland product also uses ROMs. 16-bit output can be
    connected directly to a PCM54 or MD6209 DAC or conditioned through a
    MB654419 TVF interface.

    Sampling rate is either 30kHz or 15kHz.

****************************************************************************/

#include "emu.h"
#include "sa16.h"

//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

#define _BLKOFF (m_regs[SA16REG_BLK] << 13)
#define LOG_REG_ACCESS 0
#define LOG_GATED 1  // log only when m_log_gate is set (for MIDI note investigation)
#define LOG_ACTIVE (LOG_REG_ACCESS || (LOG_GATED && m_log_gate))
#define LOG_WRAM_ACCESS 0 // TODO: re-enable when working on SA-16 emulation

// device type definitions
DEFINE_DEVICE_TYPE(RF5C36, rf5c36_device, "rf5c36", "Roland RF5C36 Sampler")
DEFINE_DEVICE_TYPE(SA16, sa16_device, "sa16", "Roland SA-16 Wave Gate Array")

// default address map
void sa16_base_device::sa16(address_map &map)
{
	// TODO: it looks like the chip deals with memory in 256k word banks (through CAS0-3).
	//       Maybe implement a banked solution instead of a linear
	if (!has_configured_map(0))
		map(0x000000, 0x1fffff).ram(); // total address bus width is 20 bits (1M 12-bit words)
}

//-------------------------------------------------
//  memory_space_config - return a description of
//  any address spaces owned by this device
//-------------------------------------------------

device_memory_interface::space_config_vector sa16_base_device::memory_space_config() const
{
	return space_config_vector {
		std::make_pair(AS_WRAM,     &m_space_config),
		std::make_pair(AS_REGS,     &m_space_regs_config),
		std::make_pair(AS_CHANREGS, &m_space_chanregs_config)
	};
}

//**************************************************************************
//  DEVICE IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  sa16_base_device - constructor
//-------------------------------------------------

sa16_base_device::sa16_base_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, type, tag, owner, clock)
	, device_sound_interface(mconfig, *this)
	, device_memory_interface(mconfig, *this)
	, m_space_config("waveram", ENDIANNESS_LITTLE, 16, 21, 0, address_map_constructor(FUNC(sa16_base_device::sa16), this))
	, m_space_regs_config("regs", ENDIANNESS_LITTLE, 8, 4, 0, address_map_constructor(FUNC(sa16_base_device::regs_map), this))
	, m_space_chanregs_config("chanregs", ENDIANNESS_LITTLE, 16, 11, 0, address_map_constructor(FUNC(sa16_base_device::chanregs_map), this))
	, m_int_callback(*this)
	, m_sh_callback(*this)
	, m_stream(nullptr)
{
}


//-------------------------------------------------
//  rf5c36_device - constructor
//-------------------------------------------------

rf5c36_device::rf5c36_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: sa16_base_device(mconfig, RF5C36, tag, owner, clock)
{
}


//-------------------------------------------------
//  sa16_device - constructor
//-------------------------------------------------

sa16_device::sa16_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: sa16_base_device(mconfig, SA16, tag, owner, clock)
{
}


//-------------------------------------------------
//  device_start - device-specific startup
//-------------------------------------------------

//-------------------------------------------------
//  regs_map - address map for direct registers
//  (visible as "regs" in debugger memory view)
//-------------------------------------------------

void sa16_base_device::regs_map(address_map &map)
{
	map(0x0, 0xf).lrw8(
		NAME([this] (offs_t offset) -> u8 {
			switch (offset) {
			case 0: return m_active_channels & 0xff;
			case 1: return m_active_channels >> 8;
			case 2: return m_regs800[m_reg800_roffset] & 0xff;
			case 3: return m_regs800[m_reg800_roffset] >> 8;
			case 8: return m_regs[SA16REG_BLK];
			default: return u8(0);
			}
		}),
		NAME([this] (offs_t offset, u8 data) {
			switch (offset) {
			case 0: m_active_channels = (m_active_channels & 0xff00) | data; break;
			case 1: m_active_channels = (m_active_channels & 0x00ff) | (data << 8); break;
			case 8: m_regs[SA16REG_BLK] = data; m_smpcounter = 0; break;
			}
		})
	);
}

//-------------------------------------------------
//  chanregs_map - address map for channel registers
//  (visible as "chanregs" in debugger memory view)
//-------------------------------------------------

void sa16_base_device::chanregs_map(address_map &map)
{
	map(0x000, 0x7ff).lrw16(
		NAME([this] (offs_t offset) -> u16 { return m_regs800[offset]; }),
		NAME([this] (offs_t offset, u16 data) { m_regs800[offset] = data; })
	);
}


void sa16_base_device::device_start()
{
	// Set up wave RAM cache for sound_stream_update
	space(AS_WRAM).cache(m_wram_cache);

	// Allocate sound stream: mono output at 30 kHz (clock / 896)
	m_stream = stream_alloc(0, 1, clock() / CLOCK_DIVIDER);

	m_active_channels = 0;
	m_reg800_woffset = 0;
	m_reg800_roffset = 0;
	m_blockoffset = 0;
	m_smpcounter = 0;
	m_port_wram = 0;
	m_port_smp16 = 0;
	m_log_gate = false;
	memset(m_regs, 0, sizeof(m_regs));
	memset(m_ports, 0, sizeof(m_ports));
	memset(m_regs800, 0, sizeof(m_regs800));

	for (int v = 0; v < NUM_VOICES; v++)
	{
		m_voice[v].m_phase = 0;
		m_voice[v].m_active = false;
	}

	save_item(NAME(m_active_channels));
	save_item(NAME(m_reg800_woffset));
	save_item(NAME(m_reg800_roffset));
	save_item(NAME(m_blockoffset));
	save_item(NAME(m_smpcounter));
	save_item(NAME(m_port_wram));
	save_item(NAME(m_port_smp16));
	save_item(NAME(m_regs));
	save_item(NAME(m_ports));
	save_item(NAME(m_regs800));
	save_item(NAME(m_log_gate));
	save_item(STRUCT_MEMBER(m_voice, m_phase));
	save_item(STRUCT_MEMBER(m_voice, m_active));
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void sa16_base_device::device_reset()
{
}


//-------------------------------------------------
//  sound_stream_update - generate audio output
//-------------------------------------------------

void sa16_base_device::sound_stream_update(sound_stream &stream)
{
	for (int i = 0; i < stream.samples(); i++)
	{
		s32 mix = 0;

		for (int v = 0; v < NUM_VOICES; v++)
		{
			// Check if this slot is active
			if (!(m_active_channels & (1 << v)))
			{
				m_voice[v].m_active = false;
				continue;
			}

			const int base = v * 0x10; // 16 registers per slot
			const u16 pitch    = m_regs800[base + 0]; // 2.14 phase increment
			const u16 start    = m_regs800[base + 1]; // sample start word addr (in bank)
			const u16 r2       = m_regs800[base + 2]; // bank select (bits 15:14)
			const u16 loop_len = m_regs800[base + 5]; // loop length (samples)
			const u16 endpoint = m_regs800[base + 6]; // endpoint (samples from start)

			// Reset phase on voice activation
			if (!m_voice[v].m_active)
			{
				m_voice[v].m_active = true;
				m_voice[v].m_phase = 0;
			}

			// Compute byte address in wave RAM
			// Bank: r2 bits [15:14] select one of 4 CAS banks (256K words each)
			const u32 bank_base = u32((r2 >> 14) & 3) * 0x80000;
			const u32 sample_pos = m_voice[v].m_phase >> 14;
			const u32 byte_addr = bank_base + (u32(start) + sample_pos) * 2;

			// Read 12-bit sample from wave RAM, sign-extend to 16-bit
			const s16 sample = s16(m_wram_cache.read_word(byte_addr) << 4) >> 4;

			mix += sample;

			// Advance phase accumulator
			m_voice[v].m_phase += pitch;

			// Handle endpoint
			if (endpoint > 0 && (m_voice[v].m_phase >> 14) >= endpoint)
			{
				if (loop_len > 4)
					m_voice[v].m_phase -= u32(loop_len) << 14;
				else
				{
					m_voice[v].m_active = false;
					m_voice[v].m_phase = 0;
				}
			}
		}

		stream.put_int(0, i, mix, 32768);
	}
}


//-------------------------------------------------
//  read - read data to CPU bus
//-------------------------------------------------

u8 sa16_base_device::read(offs_t offset)
{
	u8 value = -1;

	// 0x1000+: Set byte offset within block
	if (offset >= 0x1000) {
		m_blockoffset = offset - 0x1000;
		return 0;
	} else if (offset >= 0x800) { // 800h+ Set register offset
		m_reg800_roffset = offset - 0x800;
		if (LOG_ACTIVE) logerror("%s%s offsetRegister => %03x\n", machine().describe_context(), tag(), m_reg800_roffset);
		return 0;
	}

    // abc def <= ab cf de
	switch(offset)
	{
	case 0: // Play tone/channel
		value = m_active_channels;
		if (LOG_ACTIVE) logerror("%s%s active_channels[0..7] => %02x\n", machine().describe_context(), tag(), value);
		break;
	case 1: // Play tone/channel
		value = m_active_channels >> 8;
		if (LOG_ACTIVE) logerror("%s%s active_channels[8..F] => %02x\n", machine().describe_context(), tag(), value);
		break;
	case 2:
		value = m_regs800[m_reg800_roffset];
		if (LOG_ACTIVE) logerror("%s%s regs800[%03x].lo => %02x\n", machine().describe_context(), tag(), m_reg800_roffset, value);
		break;
	case 3:
		value = m_regs800[m_reg800_roffset] >> 8;
		if (LOG_ACTIVE) logerror("%s%s regs800[%03x].hi => %02x\n", machine().describe_context(), tag(), m_reg800_roffset, value);
		break;
	case 4: // Waveram port low
		value = wram_r8(_BLKOFF + m_blockoffset);
		if (LOG_WRAM_ACCESS) logerror("%s%s WRAM[%08x].lo => %02x\n", machine().describe_context(), tag(), _BLKOFF + m_blockoffset, value);
		break;
	case 5: // Waveram port high
		value = wram_r8(_BLKOFF + m_blockoffset + 1);
		if (LOG_WRAM_ACCESS) logerror("%s%s WRAM[%08x].hi => %02x\n", machine().describe_context(), tag(), _BLKOFF + m_blockoffset, value);
		break;
	case 8:
		value = m_regs[SA16REG_BLK];
		break;
	case 0x404: // Sample port low (16-bit value)
		value = wram_r16(_BLKOFF + (m_smpcounter<<1)) << 4 ;
		break;
	case 0x405: // Sample port high (16-bit value)
		value = wram_r16(_BLKOFF + (m_smpcounter<<1)) >> 4 ;
		m_smpcounter++; // ??
		break;
	default:
		if (LOG_ACTIVE) logerror("%s%s read from address %04x unimplemented\n", machine().describe_context(), tag(), offset);
		value = 0;
	}
	return value;
}


//-------------------------------------------------
//  write - write data from CPU bus
//-------------------------------------------------

void sa16_base_device::write(offs_t offset, u8 data)
{
	// logerror("%s%s args: offset=%08x, data=%02x\n", machine().describe_context(), tag(), offset, data);
	if (offset >= 0x1000) { // 1000h+: Set offset within block
		m_blockoffset = (offset - 0x1000) << 1;
		return;
	} else if (offset >= 0x800) { // 800h+ Set "800"-register number to be written using regs800[] port below
		m_reg800_woffset = offset - 0x800;
		if (LOG_ACTIVE) logerror("%s%s offsetRegister <= %02x\n", machine().describe_context(), tag(), m_reg800_woffset);
		return;
	}

	switch(offset)
	{
	case 0: // Play tone/channel
		m_active_channels = (m_active_channels & 0xFF00) | data;
		if (LOG_ACTIVE) logerror("%s%s active_channels[0..7] <= %02x\n", machine().describe_context(), tag(), data);
		break;
	case 1: // Play tone/channel
		m_active_channels = (m_active_channels & 0x00FF) | (data << 8);
		if (LOG_ACTIVE) logerror("%s%s active_channels[8..F] <= %02x\n", machine().describe_context(), tag(), data);
		break;
	case 2:
		if (LOG_ACTIVE) logerror("%s%s regs800[%03x].lo <= %02x\n", machine().describe_context(), tag(), m_reg800_woffset, data);
		m_regs800[m_reg800_woffset] = (0xFF00 & m_regs800[m_reg800_woffset]) | data;
		break;
	case 3:
		if (LOG_ACTIVE) logerror("%s%s regs800[%03x].hi <= %02x\n", machine().describe_context(), tag(), m_reg800_woffset, data);
		m_regs800[m_reg800_woffset] = (0x00FF & m_regs800[m_reg800_woffset]) | data<<8;
		break;
	case 4:
		if (LOG_WRAM_ACCESS) logerror("%s%s WRAM[%08x].lo <= %02x\n", machine().describe_context(), tag(), _BLKOFF + m_blockoffset, data);
		wram_w8(_BLKOFF + m_blockoffset, data);
		break;
	case 5:
		if (LOG_WRAM_ACCESS) logerror("%s%s WRAM[%08x].hi <= %02x\n", machine().describe_context(), tag(), _BLKOFF + m_blockoffset, data);
		wram_w8(_BLKOFF + m_blockoffset + 1, data);
		break;
	case 8:
		if (LOG_ACTIVE) logerror("%s%s set block = %04x\n", machine().describe_context(), tag(), data);
		m_regs[SA16REG_BLK] = data;
		m_smpcounter = 0;
		break;
	case 0x404: // Sample port low (16-bit value)
		m_port_smp16 = (0xFF00 & m_port_smp16) | data;
		break;
	case 0x405: // Sample port high (16-bit value)
		m_port_smp16 = (0x00FF & m_port_smp16) | data<<8;
		if (LOG_ACTIVE) logerror("%s%s sample port write[%08x] <= %04x\n", machine().describe_context(), tag(), _BLKOFF + (m_smpcounter<<1), m_port_smp16 >> 4);
		wram_w16(_BLKOFF + (m_smpcounter<<1), m_port_smp16 >> 4);
		m_smpcounter++;
		break;
	default:
		if (LOG_ACTIVE) logerror("%s%s write to address %04x unimplemented (data=%02x)\n", machine().describe_context(), tag(), offset, data);
	}
}
