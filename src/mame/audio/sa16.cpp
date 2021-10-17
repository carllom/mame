/*
 * sa16.c
 *
 *  Created on: 9 okt 2014
 *      Author: Carl Lom
 *
 *  Init writes to 0,1 and 6,7
 *  Writes to 0800-08ff in combination with writes to 2,3(ports?) respectively
 *  Writes to 1000-1007 in combination with writes to 4,5
 *  Writes to 0900-090X in combination with writes to 2,3
 */

#include "sa16.h"

//**************************************************************************
//  GLOBAL VARIABLES
//**************************************************************************

#define _BLKOFF (m_regs[SA16REG_BLK] << 13)
#define LOG_REG_ACCESS 1
#define LOG_WRAM_ACCESS 1

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
		std::make_pair(0, &m_space_config)
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
	, device_memory_interface(mconfig, *this)
	, m_space_config("waveram", ENDIANNESS_LITTLE, 16, 21, 0, address_map_constructor(FUNC(sa16_base_device::sa16), this))
	, m_int_callback(*this)
	, m_sh_callback(*this)
{
}

// device-level overrides
void sa16_device::device_config_complete()
{
}

void sa16_device::device_start()
{
}

void sa16_device::device_reset()
{
	logerror("%s reset\n",tag());
	memset(m_regs,0,9*sizeof(UINT8));
	memset(m_ports,0,8*sizeof(UINT8));
	memset(m_regs800,0,2048*sizeof(UINT16));
	m_active_channels = 0;
}

const address_space_config *sa16_device::memory_space_config(address_spacenum spacenum) const
{
	return (spacenum == AS_DATA) ? &m_space_config : NULL;
}

READ8_MEMBER( sa16_device::portb_r ) //UINT8 name(address_space &space, offs_t offset, UINT8 mem_mask)
{
	UINT8 value = -1;

	// 0x1000+: Set byte offset within block
	if (offset >= 0x1000) {
		m_blockoffset = offset - 0x1000;
		return 0;
	} else if (offset >= 0x800) { // 800h+ Set register offset
		m_reg800_roffset = offset - 0x800;
		if (LOG_REG_ACCESS) logerror("%s%s offsetRegister => %03x\n", space.machine().describe_context(), tag(), m_reg800_roffset);
		return 0;
	}

    // abc def <= ab cf de
	switch(offset)
	{
	case 0: // Play tone/channel
		value = m_active_channels;
		if (LOG_REG_ACCESS) logerror("%s%s active_channels[0..7] => %02x\n", space.machine().describe_context(), tag(), value);
		break;
	case 1: // Play tone/channel
		value = m_active_channels >> 8;
		if (LOG_REG_ACCESS) logerror("%s%s active_channels[8..F] => %02x\n", space.machine().describe_context(), tag(), value);
		break;
	case 2:
		value = m_regs800[m_reg800_roffset];
		if (LOG_REG_ACCESS) logerror("%s%s regs800[%03x].lo => %02x\n", space.machine().describe_context(), tag(), m_reg800_roffset, value);
		break;
	case 3:
		value = m_regs800[m_reg800_roffset] >> 8;
		if (LOG_REG_ACCESS) logerror("%s%s regs800[%03x].hi => %02x\n", space.machine().describe_context(), tag(), m_reg800_roffset, value);
		break;
	case 4: // Waveram port low
		value = wram_r8(_BLKOFF + m_blockoffset);
		if (LOG_WRAM_ACCESS) logerror("%s%s WRAM[%08x].lo => %02x\n", space.machine().describe_context(), tag(), _BLKOFF + m_blockoffset, value);
		break;
	case 5: // Waveram port high
		value = wram_r8(_BLKOFF + m_blockoffset + 1);
		if (LOG_WRAM_ACCESS) logerror("%s%s WRAM[%08x].hi => %02x\n", space.machine().describe_context(), tag(), _BLKOFF + m_blockoffset, value);
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
		logerror("%s%s read from address %04x unimplemented\n", space.machine().describe_context(), tag(), offset);
		value = 0;
	}
	return value;
}

WRITE8_MEMBER( sa16_device::portb_w ) //void name(address_space &space, offs_t offset, UINT8 data, UINT8 mem_mask)
{
	m_int_callback.resolve_safe();
	m_sh_callback.resolve_safe();
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
	memset(m_regs,0,9*sizeof(u8));
	memset(m_ports,0,8*sizeof(u8));
	memset(m_regs800,0,2048*sizeof(u16));
	m_active_channels = 0;
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
		logerror("%s%s read from address %04x unimplemented\n", machine().describe_context(), tag(), offset);
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
		logerror("%s%s write to address %04x unimplemented (data=%02x)\n", machine().describe_context(), tag(), offset, data);
	}
}