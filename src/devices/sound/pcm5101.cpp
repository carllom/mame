// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Texas Instruments PCM5101 32-bit 384 kHz stereo DAC

    Pin-configured, with no control interface. This models the converter
    as holding each frame until the next, full scale at +/-1.0. The format
    pin (FMT) and the clocking are up to the host's serial port setup.

    TODO:
    - interpolation filters (FLT pin), de-emphasis, soft mute (XSMT)
    - the PCM5100 and PCM5102 are the same apart from noise figures

***************************************************************************/

#include "emu.h"
#include "pcm5101.h"

#include <algorithm>


DEFINE_DEVICE_TYPE(PCM5101, pcm5101_device, "pcm5101", "Texas Instruments PCM5101 stereo DAC")


pcm5101_device::pcm5101_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, PCM5101, tag, owner, clock),
	device_sound_interface(mconfig, *this),
	m_stream(nullptr),
	m_left(0),
	m_output{}
{
}

void pcm5101_device::device_start()
{
	m_stream = stream_alloc(0, 2, clock());

	save_item(NAME(m_left));
	save_item(NAME(m_output));
}

void pcm5101_device::device_reset()
{
	m_stream->update();
	m_left = 0;
	std::fill(std::begin(m_output), std::end(m_output), 0);
}

void pcm5101_device::data_w(offs_t offset, u32 data)
{
	if (!(offset & 1))
	{
		m_left = s32(data);
		return;
	}
	m_stream->update();
	m_output[0] = m_left;
	m_output[1] = s32(data);
}

void pcm5101_device::sound_stream_update(sound_stream &stream)
{
	for (unsigned ch = 0; ch < 2; ch++)
		stream.fill(ch, sound_stream::sample_t(m_output[ch] * (1.0 / 0x8000'0000)));
}
