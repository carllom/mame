// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    Wolfson WM8782 24-bit 192 kHz stereo ADC (also WM8782S)

    Pin-configured, with no control interface. This models the converter
    as a sample of its inputs at each frame, full scale at +/-1.0, sent
    as 24-bit words. The format pins (FORMAT, IWL) and the clocking mode
    are up to the host's serial port setup.

    TODO:
    - digital decimation and high-pass filters (group delay, DC removal)

***************************************************************************/

#include "emu.h"
#include "wm8782.h"

#include <algorithm>
#include <cmath>


DEFINE_DEVICE_TYPE(WM8782, wm8782_device, "wm8782", "Wolfson WM8782 stereo ADC")


wm8782_device::wm8782_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, WM8782, tag, owner, clock),
	device_sound_interface(mconfig, *this),
	m_stream(nullptr),
	m_current{},
	m_frame{}
{
}

void wm8782_device::device_start()
{
	m_stream = stream_alloc(2, 0, clock());

	save_item(NAME(m_current));
	save_item(NAME(m_frame));
}

void wm8782_device::device_reset()
{
	std::fill(std::begin(m_frame), std::end(m_frame), 0);
}

u32 wm8782_device::data_r(offs_t offset)
{
	// both channels of a frame are converted together
	if (!(offset & 1))
	{
		m_stream->update();
		std::copy(std::begin(m_current), std::end(m_current), std::begin(m_frame));
	}
	return u32(m_frame[offset & 1]) << 8;
}

void wm8782_device::sound_stream_update(sound_stream &stream)
{
	if (!stream.samples())
		return;
	for (unsigned ch = 0; ch < 2; ch++)
	{
		double const sample = std::clamp<double>(stream.get(ch, stream.samples() - 1), -1.0, 1.0);
		m_current[ch] = std::clamp<s32>(s32(std::lround(sample * 0x80'0000)), -0x80'0000, 0x7f'ffff);
	}
}
