// license:BSD-3-Clause
// copyright-holders:Valley Bell
#ifndef MAME_SOUND_ROLAND_LP_H
#define MAME_SOUND_ROLAND_LP_H

#pragma once

#include "dirom.h"

class mb87419_mb87420_device : public device_t, public device_sound_interface, public device_rom_interface<22>
{
public:
	mb87419_mb87420_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock);

	auto int_callback() { return m_int_callback.bind(); }

	u8 read(offs_t offset);
	void write(offs_t offset, u8 data);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

	// device_rom_interface implementation
	virtual void rom_bank_pre_change() override;

	static int16_t decode_sample(int8_t data);
	static int16_t sample_interpolate(int16_t smp1, int16_t smp2, uint16_t frac);
	static uint32_t env_target_level(uint8_t target);
	static uint32_t env_step(uint8_t rate);

	TIMER_CALLBACK_MEMBER(env_scan_tick);

private:
	static constexpr unsigned NUM_CHANNELS = 32;
	static constexpr unsigned ENV_SCAN_SAMPLES = 5; // samples between voices in the envelope event scan

	void update_irq();

	struct pcm_channel
	{
		pcm_channel() { }

		// registers
		uint16_t mode = 0;
		uint16_t bank = 0;
		uint16_t step = 0;      // 2.14 fixed point (0x4000 equals 32000 Hz)
		uint8_t env_rate = 0;   // D806: envelope rate, signed float e<<4|m (negative = down)
		uint8_t env_target = 0; // D807: envelope target level, float e<<4|m
		uint32_t env_level = 0; // current envelope level (26-bit linear amplitude)
		uint32_t start = 0;     // start address (18.14 fixed point)
		uint16_t end = 0;       // end offset (high word)
		uint16_t loop = 0;      // loop offset (high word)

		// work variables
		bool enable = false;
		int8_t play_dir = 0;    // playing direction, -1 [backwards] / 0 [stopped] / +1 [forwards]
		uint32_t addr = 0;      // current address
		int16_t smpl_cur = 0;   // current sample
		int16_t smpl_nxt = 0;   // next sample
	};

	devcb_write_line m_int_callback;

	uint32_t m_clock;                   // clock
	uint32_t m_rate;                    // sample rate (usually 32000 Hz)
	sound_stream* m_stream;             // stream handle
	emu_timer* m_scan_timer;            // envelope event scan, one voice per tick
	bool m_irq_state;                   // current IRQ line state
	uint32_t m_event_pending;           // voices with a pending envelope event
	uint8_t m_scan_voice;               // next voice to scan
	pcm_channel m_chns[NUM_CHANNELS];   // channel memory
	uint8_t m_sel_chn;                  // selected channel (D81F)

	// Readback select: writing a voice number to 10, 12 or 1A chooses what
	// registers 02/03 return for that voice; reading 00 selects the event voice.
	enum : uint8_t
	{
		PROBE_LEVEL_LO,     // 10: envelope level bits 0-15
		PROBE_LEVEL_HI,     // 12: envelope level bits 16-25
		PROBE_ADDRESS,      // 1A: playback address, in loop/end register units
		PROBE_EVENT         // 00: rate (02) and target (03)
	};
	uint8_t m_probe_slot;
	uint8_t m_probe_mode;
};

DECLARE_DEVICE_TYPE(MB87419_MB87420, mb87419_mb87420_device)

#endif // MAME_SOUND_ROLAND_LP_H
