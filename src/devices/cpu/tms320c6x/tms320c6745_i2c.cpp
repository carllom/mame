// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 I2C controller (I2C0, I2C1)

    Reference: SPRUH91D chapter 21.

    Master mode with byte-level callbacks for the devices on the bus.
    Transfers complete at once. Without callbacks nothing acknowledges.

    TODO:
    - slave mode, arbitration, bit timing, free data format, DMA events

***************************************************************************/

#include "emu.h"
#include "tms320c6745_i2c.h"

#include <bit>


DEFINE_DEVICE_TYPE(TMS320C6745_I2C, tms320c6745_i2c_device, "tms320c6745_i2c", "TMS320C6745 I2C")


namespace {

enum : unsigned
{
	ICOAR, ICIMR, ICSTR, ICCLKL, ICCLKH, ICCNT, ICDRR, ICSAR, ICDXR, ICMDR, ICIVR, ICEMDR, ICPSC, REVID1, REVID2
};

// ICSTR
enum : u32
{
	AL = 1U << 0, NACK = 1U << 1, ARDY = 1U << 2, ICRRDY = 1U << 3, ICXRDY = 1U << 4, SCD = 1U << 5,
	BB = 1U << 12
};

// ICMDR
enum : u32
{
	RM = 1U << 7, IRS = 1U << 5, TRX = 1U << 9, MST = 1U << 10, STP = 1U << 11, STT = 1U << 13
};

} // anonymous namespace


tms320c6745_i2c_device::tms320c6745_i2c_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_I2C, tag, owner, clock),
	m_start_cb(*this),
	m_write_cb(*this),
	m_read_cb(*this),
	m_stop_cb(*this),
	m_irq_cb(*this),
	m_reg{},
	m_dxr_full(false),
	m_irq(0)
{
}

void tms320c6745_i2c_device::device_resolve_objects()
{
	m_start_cb.resolve();
	m_write_cb.resolve();
	m_read_cb.resolve();
	m_stop_cb.resolve();
}

void tms320c6745_i2c_device::device_start()
{
	save_item(NAME(m_reg));
	save_item(NAME(m_dxr_full));
	save_item(NAME(m_irq));
}

void tms320c6745_i2c_device::device_reset()
{
	std::fill(std::begin(m_reg), std::end(m_reg), 0);
	m_reg[ICSTR] = (1U << 10) | ICXRDY; // XSMT
	m_reg[REVID1] = 0x4415;
	m_reg[REVID2] = 0x0006;
	m_dxr_full = false;
	m_irq = 0;
	m_irq_cb(0);
}

void tms320c6745_i2c_device::map(address_map &map)
{
	map(0x00, 0x3b).lrw32(
			NAME([this] (offs_t offset) -> u32
			{
				switch (offset)
				{
				case ICDRR: return drr_r();
				case ICIVR: return ivr_r();
				default: return m_reg[offset];
				}
			}),
			NAME([this] (offs_t offset, u32 data)
			{
				switch (offset)
				{
				case ICSTR:
					// write 1 to clear; ICXRDY and ICRRDY follow ICDXR and ICDRR (firmware
					// clears NACK with a read-modify-write of ICSTR, then waits for them)
					m_reg[ICSTR] &= ~(data & (AL | NACK | ARDY | SCD));
					update_irq();
					break;
				case ICIMR:
					m_reg[ICIMR] = data & 0x7f;
					update_irq();
					break;
				case ICDXR:
					dxr_w(data);
					break;
				case ICMDR:
					mdr_w(data);
					break;
				case ICDRR: case ICIVR: case REVID1: case REVID2:
					break;
				default:
					m_reg[offset] = data;
					break;
				}
			}));
}

void tms320c6745_i2c_device::update_irq()
{
	int const state = (m_reg[ICSTR] & m_reg[ICIMR] & 0x7f) ? 1 : 0;
	if (state != m_irq)
	{
		m_irq = state;
		m_irq_cb(state);
	}
}

u32 tms320c6745_i2c_device::ivr_r()
{
	// highest priority pending enabled interrupt; reading clears its flag
	u32 const pending = m_reg[ICSTR] & m_reg[ICIMR] & 0x3f;
	if (!pending)
		return 0;
	unsigned const n = std::countr_zero(pending);
	if (!machine().side_effects_disabled())
	{
		m_reg[ICSTR] &= ~(1U << n);
		update_irq();
	}
	return n + 1;
}

void tms320c6745_i2c_device::stop()
{
	if (!m_stop_cb.isnull())
		m_stop_cb();
	m_reg[ICSTR] = (m_reg[ICSTR] & ~BB) | SCD;
	m_reg[ICMDR] &= ~(STP | MST);
}

void tms320c6745_i2c_device::mdr_w(u32 data)
{
	m_reg[ICMDR] = data;
	if (!(data & IRS))
	{
		// in reset
		m_reg[ICSTR] = (1U << 10) | ICXRDY;
		m_dxr_full = false;
		update_irq();
		return;
	}

	if ((data & (STT | MST)) == (STT | MST))
	{
		bool const read = !(data & TRX);
		m_reg[ICMDR] &= ~STT;
		m_reg[ICSTR] = (m_reg[ICSTR] & ~(NACK | ARDY | SCD | ICRRDY)) | BB;

		bool const ack = !m_start_cb.isnull() && m_start_cb(m_reg[ICSAR] & 0x7f, read);
		if (!ack)
		{
			m_reg[ICSTR] |= NACK;
			if (m_reg[ICMDR] & STP)
				stop();
		}
		else if (read)
			receive();
		else
		{
			m_reg[ICSTR] |= ICXRDY;
			if (m_dxr_full)
				transmit();
		}
		update_irq();
	}
	else if ((data & STP) && (m_reg[ICSTR] & BB) && !(m_reg[ICCNT] & 0xffff))
	{
		stop();
		update_irq();
	}
}

void tms320c6745_i2c_device::dxr_w(u32 data)
{
	m_reg[ICDXR] = data & 0xff;
	m_dxr_full = true;
	m_reg[ICSTR] &= ~ICXRDY;
	if ((m_reg[ICSTR] & BB) && (m_reg[ICMDR] & TRX) && (m_reg[ICMDR] & MST))
		transmit();
	update_irq();
}

void tms320c6745_i2c_device::transmit()
{
	// ICDXR moves to the shift register, so ICXRDY sets even for the last byte
	m_dxr_full = false;
	m_reg[ICSTR] |= ICXRDY;
	bool const ack = !m_write_cb.isnull() && m_write_cb(u8(m_reg[ICDXR]));
	if (!ack)
	{
		m_reg[ICSTR] |= NACK;
		if (m_reg[ICMDR] & STP)
			stop();
		return;
	}

	bool done = false;
	if (!(m_reg[ICMDR] & RM))
	{
		u32 const count = m_reg[ICCNT] & 0xffff;
		m_reg[ICCNT] = (count ? count : 0x10000) - 1;
		done = !m_reg[ICCNT];
	}
	if (done)
	{
		if (m_reg[ICMDR] & STP)
			stop();
		else
			m_reg[ICSTR] |= ARDY;
	}
}

void tms320c6745_i2c_device::receive()
{
	m_reg[ICDRR] = m_read_cb.isnull() ? 0xff : m_read_cb();
	m_reg[ICSTR] |= ICRRDY;
	if (!(m_reg[ICMDR] & RM))
	{
		u32 const count = m_reg[ICCNT] & 0xffff;
		m_reg[ICCNT] = (count ? count : 0x10000) - 1;
		if (!m_reg[ICCNT])
		{
			if (m_reg[ICMDR] & STP)
				stop();
			else
				m_reg[ICSTR] |= ARDY;
		}
	}
}

u32 tms320c6745_i2c_device::drr_r()
{
	u32 const value = m_reg[ICDRR];
	if (!machine().side_effects_disabled() && (m_reg[ICSTR] & ICRRDY))
	{
		m_reg[ICSTR] &= ~ICRRDY;
		// keep receiving while the count lasts
		if ((m_reg[ICSTR] & BB) && !(m_reg[ICMDR] & TRX) && ((m_reg[ICMDR] & RM) || (m_reg[ICCNT] & 0xffff)))
			receive();
		update_irq();
	}
	return value;
}
