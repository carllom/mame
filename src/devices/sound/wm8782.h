// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_SOUND_WM8782_H
#define MAME_SOUND_WM8782_H

#pragma once

// inputs 0 and 1 are left and right; the clock is the sample rate
class wm8782_device : public device_t, public device_sound_interface
{
public:
	wm8782_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	// one word per slot of the serial frame (0 = left, 1 = right), the
	// 24-bit sample MSB first from bit 31
	u32 data_r(offs_t offset);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	sound_stream *m_stream;
	s32 m_current[2];   // latest conversion
	s32 m_frame[2];     // the frame being sent
};

DECLARE_DEVICE_TYPE(WM8782, wm8782_device)

#endif // MAME_SOUND_WM8782_H
