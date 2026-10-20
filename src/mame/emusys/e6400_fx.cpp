// license:BSD-3-Clause
// copyright-holders: Carl Lom
/******************************************************************************

    E-mu e6400 effects card slot, and a stub of the G0.5 effects card
    (SK507/SK518, EMU8000)

    The card sits on CSDSP. EOS 3.00 probes it at 0x520000, then at
    0x530000 on hardware variant 4; the stub decodes the whole window, so
    the first probe finds it.

    Its register map is the EMU8000's: each register has its own address
    and the channel (0-31) is written to +$0E first.
      bus offset = reg << 4 | port: DATA0 +$4 (32-bit, two words),
      DATA1 +$8, DATA2 +$A, DATA3 +$C
    For example +$14 PTRX, +$64 PSST, +$74 CSL, +$58 DCYSUSV, +$1C IFATN,
    +$18 HWCF/SMA* (by channel: 9 HWCF4, 10 HWCF5, 20-23 SMA*, 29-31
    HWCF1-3), +$28/+$2A/+$38/+$3A INIT1/INIT2/INIT3/INIT4.

    EOS probes the card by writing HWCF1/2/3 = $88/$10/$06 and needs HWCF2
    to read back $50. It then loads one 128-word INIT-array program at
    boot and plays every effect type by rewriting parameter words in it
    (see e6400_hardware.md, "Effects card").

    The stub is a register file that passes the probe, so EOS enables its
    effects pages and writes the card as it would on a real unit. It does
    not process audio.

******************************************************************************/

#include "emu.h"
#include "e6400_fx.h"

#define LOG_REGS (1U << 1)

#define VERBOSE (0)
//#define LOG_OUTPUT_FUNC osd_printf_info
#include "logmacro.h"

#define LOGREGS(...) LOGMASKED(LOG_REGS, __VA_ARGS__)


DEFINE_DEVICE_TYPE(E6400_FX_SLOT, e6400_fx_slot_device, "e6400_fx_slot", "E-mu e6400 effects card slot")

e6400_fx_slot_device::e6400_fx_slot_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, E6400_FX_SLOT, tag, owner, clock)
	, device_single_card_slot_interface<device_e6400_fx_interface>(mconfig, *this)
	, m_card(nullptr)
{
}

void e6400_fx_slot_device::device_start()
{
	m_card = get_card_device();
}

u16 e6400_fx_slot_device::read(offs_t offset, u16 mem_mask)
{
	return m_card ? m_card->read(offset, mem_mask) : 0;
}

void e6400_fx_slot_device::write(offs_t offset, u16 data, u16 mem_mask)
{
	if (m_card)
		m_card->write(offset, data, mem_mask);
}


device_e6400_fx_interface::device_e6400_fx_interface(const machine_config &mconfig, device_t &device)
	: device_interface(device, "e6400fx")
{
}


namespace {

class e6400_fx_g05_device : public device_t, public device_e6400_fx_interface
{
public:
	static constexpr feature_type unemulated_features() { return feature::SOUND; }

	e6400_fx_g05_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock = 0);

	virtual u16 read(offs_t offset, u16 mem_mask) override;
	virtual void write(offs_t offset, u16 data, u16 mem_mask) override;

protected:
	// device_t implementation
	virtual void device_start() override ATTR_COLD;
	virtual void device_reset() override ATTR_COLD;

private:
	static constexpr unsigned NUM_CHANNELS = 32;
	static constexpr unsigned NUM_REGS = 0x40;       // words, +$00 to +$7E
	static constexpr offs_t CHANNEL_SELECT = 0x0e >> 1;
	static constexpr offs_t HWCF = 0x18 >> 1;        // DATA1 register 1

	u16 m_regs[NUM_REGS][NUM_CHANNELS];
	u8 m_channel;
};

e6400_fx_g05_device::e6400_fx_g05_device(const machine_config &mconfig, const char *tag, device_t *owner, u32 clock)
	: device_t(mconfig, E6400_FX_G05, tag, owner, clock)
	, device_e6400_fx_interface(mconfig, *this)
	, m_channel(0)
{
}

void e6400_fx_g05_device::device_start()
{
	save_item(NAME(m_regs));
	save_item(NAME(m_channel));
}

void e6400_fx_g05_device::device_reset()
{
	for (auto &reg : m_regs)
		std::fill(std::begin(reg), std::end(reg), 0);
	m_channel = 0;
}

u16 e6400_fx_g05_device::read(offs_t offset, u16 mem_mask)
{
	offset &= NUM_REGS - 1;
	if (offset == CHANNEL_SELECT)
		return m_channel;

	u16 data = m_regs[offset][m_channel];

	// HWCF2 reads back with bit 6 set: EOS's probe writes $10 and requires $50
	if (offset == HWCF && m_channel == 30)
		data |= 0x40;

	if (!machine().side_effects_disabled())
		LOGREGS("%s: read +%02X ch %2u = %04X\n", machine().describe_context(), offset << 1, m_channel, data);
	return data;
}

void e6400_fx_g05_device::write(offs_t offset, u16 data, u16 mem_mask)
{
	offset &= NUM_REGS - 1;
	if (offset == CHANNEL_SELECT)
	{
		m_channel = data & (NUM_CHANNELS - 1);
		return;
	}

	COMBINE_DATA(&m_regs[offset][m_channel]);
	LOGREGS("%s: +%02X ch %2u = %04X\n", machine().describe_context(), offset << 1, m_channel, data);
}

} // anonymous namespace

DEFINE_DEVICE_TYPE_PRIVATE(E6400_FX_G05, device_e6400_fx_interface, e6400_fx_g05_device, "e6400_fx_g05", "E-mu G0.5 effects card (stub)")


void e6400_fx_cards(device_slot_interface &device)
{
	device.option_add("g05fx", E6400_FX_G05);
}
