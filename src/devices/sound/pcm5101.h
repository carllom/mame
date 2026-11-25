// license:BSD-3-Clause
// copyright-holders:Carl Lom
#ifndef MAME_SOUND_PCM5101_H
#define MAME_SOUND_PCM5101_H

#pragma once

// outputs 0 and 1 are left and right; the clock is the sample rate
class pcm5101_device : public device_t, public device_sound_interface
{
public:
	pcm5101_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock = 0);

	// one word per slot of the serial frame (0 = left, 1 = right), MSB
	// first from bit 31; the frame is output when the right word arrives
	void data_w(offs_t offset, u32 data);

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

	// device_sound_interface implementation
	virtual void sound_stream_update(sound_stream &stream) override;

private:
	sound_stream *m_stream;
	s32 m_left;         // left word waiting for its right word
	s32 m_output[2];
};

DECLARE_DEVICE_TYPE(PCM5101, pcm5101_device)

#endif // MAME_SOUND_PCM5101_H
