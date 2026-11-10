// license:BSD-3-Clause
// copyright-holders:Carl Lom
/***************************************************************************

    TMS320C6745/C6747 enhanced DMA controller (EDMA3 channel controller
    with its two transfer controllers)

    References: SPRUH91D chapter 16, SPRUGS5B (EDMA3 user's guide).

    32 DMA channels, 8 QDMA channels, 128 PaRAM sets, 4 shadow regions.
    Transfers complete as soon as they are triggered.

    TODO:
    - transfer controller timing, queues and priorities
    - memory protection, error reporting details

***************************************************************************/

#include "emu.h"
#include "tms320c6745_edma.h"

#include <algorithm>

#define LOG_XFER (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(TMS320C6745_EDMA, tms320c6745_edma_device, "tms320c6745_edma", "TMS320C6745 EDMA3")


namespace {

// channel registers, at 0x1000 (global) and 0x2000 + 0x200 * region (shadow)
enum : u32
{
	ER = 0x00 / 4, ECR = 0x08 / 4, ESR = 0x10 / 4, CER = 0x18 / 4, EER = 0x20 / 4, EECR = 0x28 / 4,
	EESR = 0x30 / 4, SER = 0x38 / 4, SECR = 0x40 / 4, IER = 0x50 / 4, IECR = 0x58 / 4, IESR = 0x60 / 4,
	IPR = 0x68 / 4, ICR = 0x70 / 4, IEVAL = 0x78 / 4, QER = 0x80 / 4, QEER = 0x84 / 4, QEECR = 0x88 / 4,
	QEESR = 0x8c / 4, QSER = 0x90 / 4, QSECR = 0x94 / 4
};

// PaRAM words
enum : u32
{
	P_OPT, P_SRC, P_ABCNT, P_DST, P_BIDX, P_LINK, P_CIDX, P_CCNT
};

} // anonymous namespace


tms320c6745_edma_device::tms320c6745_edma_device(machine_config const &mconfig, char const *tag, device_t *owner, u32 clock) :
	device_t(mconfig, TMS320C6745_EDMA, tag, owner, clock),
	m_space(*this, finder_base::DUMMY_TAG, -1),
	m_region_irq_cb(*this),
	m_error_irq_cb(*this),
	m_er(0), m_eer(0), m_cer(0), m_ser(0), m_ier(0), m_ipr(0), m_emr(0),
	m_qer(0), m_qeer(0), m_qemr(0), m_ccerr(0),
	m_drae{}, m_qrae{}, m_qchmap{}, m_dmaqnum{},
	m_qdmaqnum(0), m_quepri(0), m_irq_state(0),
	m_param{},
	m_busy(false)
{
}

void tms320c6745_edma_device::device_start()
{
	save_item(NAME(m_er));
	save_item(NAME(m_eer));
	save_item(NAME(m_cer));
	save_item(NAME(m_ser));
	save_item(NAME(m_ier));
	save_item(NAME(m_ipr));
	save_item(NAME(m_emr));
	save_item(NAME(m_qer));
	save_item(NAME(m_qeer));
	save_item(NAME(m_qemr));
	save_item(NAME(m_ccerr));
	save_item(NAME(m_drae));
	save_item(NAME(m_qrae));
	save_item(NAME(m_qchmap));
	save_item(NAME(m_dmaqnum));
	save_item(NAME(m_qdmaqnum));
	save_item(NAME(m_quepri));
	save_item(NAME(m_irq_state));
	save_item(NAME(m_param));
}

void tms320c6745_edma_device::device_reset()
{
	m_er = m_eer = m_cer = m_ser = m_ier = m_ipr = m_emr = 0;
	m_qer = m_qeer = m_qemr = m_ccerr = 0;
	std::fill(std::begin(m_drae), std::end(m_drae), 0);
	std::fill(std::begin(m_qrae), std::end(m_qrae), 0);
	std::fill(std::begin(m_qchmap), std::end(m_qchmap), 0);
	std::fill(std::begin(m_dmaqnum), std::end(m_dmaqnum), 0);
	m_qdmaqnum = 0;
	m_quepri = 0;
	m_busy = false;
	if (m_irq_state)
		for (u32 r = 0; r < 4; r++)
			m_region_irq_cb[r](0);
	m_irq_state = 0;
}

void tms320c6745_edma_device::map(address_map &map)
{
	map(0x0000, 0x0003).lr32(NAME([] () -> u32 { return 0x4001'5300; })); // REVID
	map(0x0004, 0x0007).lr32(NAME([] () -> u32 { return 0x0034'3005; })); // CCCFG: 32 DMA channels, 8 QDMA channels, 128 PaRAM sets, 2 queues, 4 regions
	map(0x0200, 0x021f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_qchmap[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_qchmap[offset] = data & 0x3ffc; })); // QCHMAPn
	map(0x0240, 0x024f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_dmaqnum[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_dmaqnum[offset] = data; })); // DMAQNUMn
	map(0x0260, 0x0263).lrw32(NAME([this] () -> u32 { return m_qdmaqnum; }), NAME([this] (u32 data) { m_qdmaqnum = data; }));
	map(0x0284, 0x0287).lrw32(NAME([this] () -> u32 { return m_quepri; }), NAME([this] (u32 data) { m_quepri = data; }));
	map(0x0300, 0x0303).lr32(NAME([this] () -> u32 { return m_emr; }));
	map(0x0308, 0x030b).lw32(NAME([this] (u32 data) { m_emr &= ~data; })); // EMCR
	map(0x0310, 0x0313).lr32(NAME([this] () -> u32 { return m_qemr; }));
	map(0x0314, 0x0317).lw32(NAME([this] (u32 data) { m_qemr &= ~data; })); // QEMCR
	map(0x0318, 0x031b).lr32(NAME([this] () -> u32 { return m_ccerr; }));
	map(0x031c, 0x031f).lw32(NAME([this] (u32 data) { m_ccerr &= ~data; })); // CCERRCLR
	map(0x0320, 0x0323).lw32(
			NAME([this] (u32 data)
			{
				// EEVAL
				if (BIT(data, 0))
					m_error_irq_cb(m_emr || m_qemr || m_ccerr);
			}));
	map(0x0340, 0x035f).rw(FUNC(tms320c6745_edma_device::drae_r), FUNC(tms320c6745_edma_device::drae_w));
	map(0x0380, 0x038f).lrw32(
			NAME([this] (offs_t offset) -> u32 { return m_qrae[offset]; }),
			NAME([this] (offs_t offset, u32 data) { m_qrae[offset] = data; })); // QRAEn
	map(0x0640, 0x0643).lr32(NAME([] () -> u32 { return 0; })); // CCSTAT: idle

	map(0x1000, 0x10ff).lrw32(
			NAME([this] (offs_t offset) -> u32 { return channel_r(offset, ~u32(0)); }),
			NAME([this] (offs_t offset, u32 data) { channel_w(offset, data, ~u32(0)); }));
	map(0x2000, 0x27ff).rw(FUNC(tms320c6745_edma_device::region_r), FUNC(tms320c6745_edma_device::region_w));
	map(0x4000, 0x4fff).rw(FUNC(tms320c6745_edma_device::param_r), FUNC(tms320c6745_edma_device::param_w));
}

u32 tms320c6745_edma_device::drae_r(offs_t offset)
{
	// DRAEn at even words; DRAEHn (channels 32-63) don't exist here
	return BIT(offset, 0) ? 0 : m_drae[offset >> 1];
}

void tms320c6745_edma_device::drae_w(offs_t offset, u32 data)
{
	if (!BIT(offset, 0))
	{
		m_drae[offset >> 1] = data;
		update_interrupts(false);
	}
}

u32 tms320c6745_edma_device::region_r(offs_t offset)
{
	u32 const region = offset / (0x200 / 4);
	u32 const reg = offset % (0x200 / 4);
	bool const qdma = (reg >= QER) && (reg <= QSECR);
	return channel_r(reg, qdma ? m_qrae[region] : m_drae[region]);
}

void tms320c6745_edma_device::region_w(offs_t offset, u32 data)
{
	u32 const region = offset / (0x200 / 4);
	u32 const reg = offset % (0x200 / 4);
	bool const qdma = (reg >= QER) && (reg <= QSECR);
	channel_w(reg, data, qdma ? m_qrae[region] : m_drae[region]);
}

u32 tms320c6745_edma_device::channel_r(offs_t offset, u32 mask)
{
	switch (offset)
	{
	case ER: return m_er & mask;
	case CER: return m_cer & mask;
	case EER: return m_eer & mask;
	case SER: return m_ser & mask;
	case IER: return m_ier & mask;
	case IPR: return m_ipr & mask;
	case QER: return m_qer & mask;
	case QEER: return m_qeer & mask;
	case QSER: return 0;
	default: return 0;
	}
}

void tms320c6745_edma_device::channel_w(offs_t offset, u32 data, u32 mask)
{
	data &= mask;
	switch (offset)
	{
	case ECR: m_er &= ~data; break;
	case ESR:
		// manual trigger
		for (u32 ch = 0; ch < CHANNELS; ch++)
			if (BIT(data, ch))
				trigger(ch, false, ch);
		break;
	case EECR: m_eer &= ~data; break;
	case EESR: m_eer |= data; process_events(); break;
	case SECR: m_ser &= ~data; break;
	case IECR: m_ier &= ~data; update_interrupts(false); break;
	case IESR: m_ier |= data; update_interrupts(false); break;
	case ICR: m_ipr &= ~data; update_interrupts(false); break;
	case IEVAL: if (data & 1) update_interrupts(true); break;
	case QEECR: m_qeer &= ~data; break;
	case QEESR: m_qeer |= data; break;
	case QSECR: break;
	default: break;
	}
}

u32 tms320c6745_edma_device::param_r(offs_t offset)
{
	return m_param[offset];
}

void tms320c6745_edma_device::param_w(offs_t offset, u32 data, u32 mem_mask)
{
	COMBINE_DATA(&m_param[offset]);

	// QDMA channels trigger on a write to their trigger word
	u32 const set = offset / 8;
	u32 const word = offset % 8;
	for (u32 q = 0; q < QCHANNELS; q++)
	{
		if (BIT(m_qeer, q) && (BIT(m_qchmap[q], 5, 9) == set) && (BIT(m_qchmap[q], 2, 3) == word))
			trigger(set, true, q);
	}
}

void tms320c6745_edma_device::event(u32 channel)
{
	if (channel >= CHANNELS)
		return;
	if (BIT(m_er, channel))
	{
		m_emr |= 1U << channel; // missed event
		m_error_irq_cb(1);
		return;
	}
	m_er |= 1U << channel;
	process_events();
}

void tms320c6745_edma_device::process_events()
{
	if (m_busy)
		return;
	u32 const pending = m_er & m_eer;
	for (u32 ch = 0; ch < CHANNELS; ch++)
	{
		if (BIT(pending, ch))
		{
			m_er &= ~(1U << ch);
			trigger(ch, false, ch);
		}
	}
}

void tms320c6745_edma_device::trigger(u32 set, bool qdma, u32 channel)
{
	u32 *const p = &m_param[set * 8];
	u32 const opt = p[P_OPT];
	u32 src = p[P_SRC];
	u32 dst = p[P_DST];
	u32 const acnt = p[P_ABCNT] & 0xffff;
	u32 bcnt = p[P_ABCNT] >> 16;
	s32 const srcbidx = s16(p[P_BIDX]);
	s32 const dstbidx = s16(p[P_BIDX] >> 16);
	u32 const link = p[P_LINK] & 0xffff;
	u32 const bcntrld = p[P_LINK] >> 16;
	s32 const srccidx = s16(p[P_CIDX]);
	s32 const dstcidx = s16(p[P_CIDX] >> 16);
	u32 ccnt = p[P_CCNT] & 0xffff;

	bool const ab_sync = BIT(opt, 2);
	bool const fixed = BIT(opt, 3);
	u32 const tcc = BIT(opt, 12, 6);
	bool const sam = BIT(opt, 0);
	bool const dam = BIT(opt, 1);

	m_busy = true;

	auto const copy_array = [this, acnt, sam, dam, opt] (u32 s, u32 d)
	{
		// constant addressing wraps within the FIFO width
		u32 const fwid = 1U << BIT(opt, 8, 3);
		if (!(acnt & 3) && !((s | d) & 3))
		{
			for (u32 n = 0; n < acnt; n += 4)
				m_space->write_dword(dam ? (d + (n % fwid)) : (d + n), m_space->read_dword(sam ? (s + (n % fwid)) : (s + n)));
		}
		else
		{
			for (u32 n = 0; n < acnt; n++)
				m_space->write_byte(dam ? (d + (n % fwid)) : (d + n), m_space->read_byte(sam ? (s + (n % fwid)) : (s + n)));
		}
	};

	bool final_tr = true;
	if (!acnt || !bcnt || !ccnt)
	{
		// a null set (all counts 0) is an error; a dummy set is a legal transfer of 0 bytes
		LOGMASKED(LOG_XFER, "%s %u: %s set %u\n", qdma ? "QDMA" : "DMA", channel, (acnt || bcnt || ccnt) ? "dummy" : "null", set);
		if (!acnt && !bcnt && !ccnt)
		{
			if (qdma)
				m_qemr |= 1U << channel;
			else
				m_emr |= 1U << channel;
			m_error_irq_cb(1);
		}
	}
	else if (!ab_sync)
	{
		// one array of ACNT bytes
		copy_array(src, dst);
		if (bcnt > 1)
		{
			bcnt--;
			src += srcbidx;
			dst += dstbidx;
		}
		else
		{
			ccnt--;
			bcnt = bcntrld;
			src += srccidx;
			dst += dstcidx;
		}
		final_tr = !ccnt;
	}
	else
	{
		// BCNT arrays of ACNT bytes
		u32 s = src, d = dst;
		for (u32 n = 0; n < bcnt; n++)
		{
			copy_array(s, d);
			s += srcbidx;
			d += dstbidx;
		}
		ccnt--;
		src += srccidx;
		dst += dstcidx;
		final_tr = !ccnt;
	}

	LOGMASKED(LOG_XFER, "%s %u: set %u %s ccnt %u\n", qdma ? "QDMA" : "DMA", channel, set, final_tr ? "done" : "partial", ccnt);

	if (!fixed)
	{
		if (final_tr)
		{
			// link the next set in, or a null set
			if (link != 0xffff)
			{
				u32 const next = ((link - 0x4000) & 0xfff) / 32;
				for (u32 w = 0; w < 8; w++)
					p[w] = m_param[next * 8 + w];
			}
			else
			{
				std::fill_n(p, 8, 0);
				p[P_LINK] = 0xffff;
			}
		}
		else
		{
			p[P_SRC] = src;
			p[P_DST] = dst;
			p[P_ABCNT] = (bcnt << 16) | acnt;
			p[P_CCNT] = ccnt;
		}
	}

	if (final_tr)
		completion(tcc, BIT(opt, 20), BIT(opt, 22));
	else
		completion(tcc, BIT(opt, 21), BIT(opt, 23));

	m_busy = false;
	process_events();
}

void tms320c6745_edma_device::completion(u32 tcc, bool interrupt, bool chain)
{
	if (tcc >= 32)
		return;
	if (interrupt)
	{
		m_ipr |= 1U << tcc;
		update_interrupts(false);
	}
	if (chain)
		trigger(tcc, false, tcc); // chained event, serviced at once
}

void tms320c6745_edma_device::update_interrupts(bool force)
{
	// each region interrupt is the OR of its enabled, pending completion codes
	for (u32 r = 0; r < 4; r++)
	{
		bool const active = (m_ipr & m_ier & m_drae[r]) != 0;
		if (active && (!BIT(m_irq_state, r) || force))
		{
			m_region_irq_cb[r](1);
			m_region_irq_cb[r](0);
		}
		m_irq_state = (m_irq_state & ~(1 << r)) | (active ? (1 << r) : 0);
	}
}
