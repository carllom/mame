/*
 * audio/sa16.h
 *
 *  Created on: 9 okt 2014
 *      Author: Carl Lom
 */

#ifndef SA16_H_
#define SA16_H_

#include "emu.h"

#define MCFG_SA16_ADD(_tag, _clock, _waveram_map)  \
	MCFG_DEVICE_ADD(_tag, SA16, _clock) \
	MCFG_DEVICE_DATA_MAP(_waveram_map)

class sa16_device: public device_t, public device_memory_interface/*, public device_sound_interface*/ {
public:
	sa16_device(const machine_config &mconfig, const char *tag, device_t *owner, UINT32 clock);

	DECLARE_READ8_MEMBER(portb_r);
	DECLARE_WRITE8_MEMBER(portb_w);

protected:
	// device-level overrides
	virtual void device_config_complete();
	virtual void device_start();

	virtual void device_reset();

	// device_config_memory_interface overrides
	virtual const address_space_config *memory_space_config(address_spacenum spacenum = AS_DATA) const;

	// address space configurations
	const address_space_config m_space_config;

	// sound stream update overrides
	//virtual void sound_stream_update(sound_stream &stream,
	//		stream_sample_t **inputs, stream_sample_t **outputs, int samples);

private:
	UINT16 wram_r16(offs_t offset) { return this->space(AS_DATA).read_word(offset); }
	void wram_w16(offs_t offset, UINT16 data) { this->space(AS_DATA).write_word(offset, data); }
	UINT8 wram_r8(offs_t offset) { return this->space(AS_DATA).read_byte(offset); }
	void wram_w8(offs_t offset, UINT8 data) { this->space(AS_DATA).write_byte(offset, data); }

	UINT16 m_active_channels;

	int m_reg800_woffset; // offset for reg writes???
	int m_reg800_roffset; // offset for reg reads???
	int m_blockoffset; // Block offset for WaveRAM port
	int m_smpcounter; // Sample port counter
	UINT16 m_port_wram; // WaveRAM port value (
	UINT16 m_port_smp16; // Sample port value (16-bit)

	UINT8 m_regs[9]; // Regs @ offset 0
	UINT8 m_ports[8]; // Ports @ offset 400h
	UINT16 m_regs800[2048];

	//UINT8 m_snd_regs[0x30];
};

enum SA16_REGS {
	SA16REG_0 = 0,
	SA16REG_1,
	SA16REG_2,
	SA16REG_3,
	SA16REG_4,
	SA16REG_5,
	SA16REG_6,
	SA16REG_7,
	SA16REG_BLK
};

enum SA16_PORTS {
	SA16_SMP_LOW = 4,
	SA16_SMP_HIGH = 5
};

extern const device_type SA16;

#endif /* SA16_H_ */
