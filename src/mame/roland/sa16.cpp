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
#define LOG_REG_ACCESS 0  // TODO: re-enable when working on SA-16 emulation
#define LOG_WRAM_ACCESS 0 // TODO: re-enable when working on SA-16 emulation

// device type definitions
DEFINE_DEVICE_TYPE(RF5C36, rf5c36_device, "rf5c36", "Roland RF5C36 Sampler")
DEFINE_DEVICE_TYPE(SA16, sa16_device, "sa16", "Roland SA-16 Sampler")


//**************************************************************************
//  DEVICE IMPLEMENTATION
//**************************************************************************

//-------------------------------------------------
//  sa16_base_device - constructor
//-------------------------------------------------

sa16_base_device::sa16_base_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, type, tag, owner, clock)
	, m_int_callback(*this)
	, m_sh_callback(*this)
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

void sa16_base_device::device_start()
{
}


//-------------------------------------------------
//  device_reset - device-specific reset
//-------------------------------------------------

void sa16_base_device::device_reset()
{
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
		if (LOG_REG_ACCESS) logerror("%s%s offsetRegister => %03x\n", machine().describe_context(), tag(), m_reg800_roffset);
		return 0;
	}

    // abc def <= ab cf de
	switch(offset)
	{
	case 0: // Play tone/channel
		value = m_active_channels;
		if (LOG_REG_ACCESS) logerror("%s%s active_channels[0..7] => %02x\n", machine().describe_context(), tag(), value);
		break;
	case 1: // Play tone/channel
		value = m_active_channels >> 8;
		if (LOG_REG_ACCESS) logerror("%s%s active_channels[8..F] => %02x\n", machine().describe_context(), tag(), value);
		break;
	case 2:
		value = m_regs800[m_reg800_roffset];
		if (LOG_REG_ACCESS) logerror("%s%s regs800[%03x].lo => %02x\n", machine().describe_context(), tag(), m_reg800_roffset, value);
		break;
	case 3:
		value = m_regs800[m_reg800_roffset] >> 8;
		if (LOG_REG_ACCESS) logerror("%s%s regs800[%03x].hi => %02x\n", machine().describe_context(), tag(), m_reg800_roffset, value);
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
		if (LOG_REG_ACCESS) logerror("%s%s read from address %04x unimplemented\n", machine().describe_context(), tag(), offset);
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
		if (LOG_REG_ACCESS) logerror("%s%s offsetRegister <= %02x\n", machine().describe_context(), tag(), m_reg800_woffset);
		return;
	}

	switch(offset)
	{
	case 0: // Play tone/channel
		m_active_channels = (m_active_channels & 0xFF00) | data;
		if (LOG_REG_ACCESS) logerror("%s%s active_channels[0..7] <= %02x\n", machine().describe_context(), tag(), data);
		break;
	case 1: // Play tone/channel
		m_active_channels = (m_active_channels & 0x00FF) | (data << 8);
		if (LOG_REG_ACCESS) logerror("%s%s active_channels[8..F] <= %02x\n", machine().describe_context(), tag(), data);
		break;
	case 2:
		if (LOG_REG_ACCESS) logerror("%s%s regs800[%03x].lo <= %02x\n", machine().describe_context(), tag(), m_reg800_woffset, data);
		m_regs800[m_reg800_woffset] = (0xFF00 & m_regs800[m_reg800_woffset]) | data;
		break;
	case 3:
		if (LOG_REG_ACCESS) logerror("%s%s regs800[%03x].hi <= %02x\n", machine().describe_context(), tag(), m_reg800_woffset, data);
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
		if (LOG_REG_ACCESS) logerror("%s%s set block = %04x\n", machine().describe_context(), tag(), data);
		m_regs[SA16REG_BLK] = data;
		m_smpcounter = 0;
		break;
	case 0x404: // Sample port low (16-bit value)
		m_port_smp16 = (0xFF00 & m_port_smp16) | data;
		break;
	case 0x405: // Sample port high (16-bit value)
		m_port_smp16 = (0x00FF & m_port_smp16) | data<<8;
		if (LOG_REG_ACCESS) logerror("%s%s sample port write[%08x] <= %04x\n", machine().describe_context(), tag(), _BLKOFF + (m_smpcounter<<1), m_port_smp16 >> 4);
		wram_w16(_BLKOFF + (m_smpcounter<<1), m_port_smp16 >> 4);
		m_smpcounter++;
		break;
	default:
		if (LOG_REG_ACCESS) logerror("%s%s write to address %04x unimplemented (data=%02x)\n", machine().describe_context(), tag(), offset, data);
	}
}
