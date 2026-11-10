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

#define LOG_XFER (1U << 1)

#define VERBOSE (0)
#include "logmacro.h"


DEFINE_DEVICE_TYPE(TMS320C6745_EDMA, tms320c6745_edma_device, "tms320c6745_edma", "TMS320C6745 EDMA3")


namespace {

// channel registers, at 0x1000 (global) and 0x2000 + 0x200 * region (shadow)
enum : unsigned
{
	ER = 0x00 / 4, ECR = 0x08 / 4, ESR = 0x10 / 4, CER = 0x18 / 4, EER = 0x20 / 4, EECR = 0x28 / 4,
	EESR = 0x30 / 4, SER = 0x38 / 4, SECR = 0x40 / 4, IER = 0x50 / 4, IECR = 0x58 / 4, IESR = 0x60 / 4,
	IPR = 0x68 / 4, ICR = 0x70 / 4, IEVAL = 0x78 / 4, QER = 0x80 / 4, QEER = 0x84 / 4, QEECR = 0x88 / 4,
	QEESR = 0x8c / 4, QSER = 0x90 / 4, QSECR = 0x94 / 4
};

// PaRAM words
enum : unsigned
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
		for (unsigned r = 0; r < 4; r++)
			m_region_irq_cb[r](0);
	m_irq_state = 0;
}

void tms320c6745_edma_device::map(address_map &map)
{
	map(0x0000, 0x0fff).rw(FUNC(tms320c6745_edma_device::global_r), FUNC(tms320c6745_edma_device::global_w));
	map(0x1000, 0x10ff).lrw32(
			NAME([this] (offs_t offset) -> u32 { return channel_r(offset, ~u32(0)); }),
			NAME([this] (offs_t offset, u32 data) { channel_w(offset, data, ~u32(0)); }));
	map(0x2000, 0x27ff).rw(FUNC(tms320c6745_edma_device::region_r), FUNC(tms320c6745_edma_device::region_w));
	map(0x4000, 0x4fff).rw(FUNC(tms320c6745_edma_device::param_r), FUNC(tms320c6745_edma_device::param_w));
}

u32 tms320c6745_edma_device::global_r(offs_t offset)
{
	switch (offset * 4)
	{
	case 0x000: return 0x4001'5300; // REVID
	case 0x004: return 0x0034'3005; // CCCFG: 32 DMA channels, 8 QDMA channels, 128 PaRAM sets, 2 queues, 4 regions
	case 0x240: case 0x244: case 0x248: case 0x24c: return m_dmaqnum[offset - 0x240 / 4];
	case 0x260: return m_qdmaqnum;
	case 0x284: return m_quepri;
	case 0x300: return m_emr;
	case 0x310: return m_qemr;
	case 0x318: return m_ccerr;
	case 0x340: return m_drae[0];
	case 0x348: return m_drae[1];
	case 0x350: return m_drae[2];
	case 0x358: return m_drae[3];
	case 0x380: case 0x384: case 0x388: case 0x38c: return m_qrae[offset - 0x380 / 4];
	case 0x640: return 0; // CCSTAT: idle
	default:
		if ((offset >= 0x200 / 4) && (offset < 0x220 / 4))
			return m_qchmap[offset - 0x200 / 4];
		return 0;
	}
}

void tms320c6745_edma_device::global_w(offs_t offset, u32 data)
{
	switch (offset * 4)
	{
	case 0x240: case 0x244: case 0x248: case 0x24c: m_dmaqnum[offset - 0x240 / 4] = data; break;
	case 0x260: m_qdmaqnum = data; break;
	case 0x284: m_quepri = data; break;
	case 0x308: m_emr &= ~data; break; // EMCR
	case 0x314: m_qemr &= ~data; break; // QEMCR
	case 0x31c: m_ccerr &= ~data; break; // CCERRCLR
	case 0x320: // EEVAL
		if (data & 1)
			m_error_irq_cb(m_emr || m_qemr || m_ccerr);
		break;
	case 0x340: m_drae[0] = data; update_interrupts(false); break;
	case 0x348: m_drae[1] = data; update_interrupts(false); break;
	case 0x350: m_drae[2] = data; update_interrupts(false); break;
	case 0x358: m_drae[3] = data; update_interrupts(false); break;
	case 0x380: case 0x384: case 0x388: case 0x38c: m_qrae[offset - 0x380 / 4] = data; break;
	default:
		if ((offset >= 0x200 / 4) && (offset < 0x220 / 4))
			m_qchmap[offset - 0x200 / 4] = data & 0x3ffc;
		break;
	}
}

u32 tms320c6745_edma_device::region_r(offs_t offset)
{
	unsigned const region = offset / (0x200 / 4);
	unsigned const reg = offset % (0x200 / 4);
	bool const qdma = (reg >= QER) && (reg <= QSECR);
	return channel_r(reg, qdma ? m_qrae[region] : m_drae[region]);
}

void tms320c6745_edma_device::region_w(offs_t offset, u32 data)
{
	unsigned const region = offset / (0x200 / 4);
	unsigned const reg = offset % (0x200 / 4);
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
		for (unsigned ch = 0; ch < CHANNELS; ch++)
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
	unsigned const set = offset / 8;
	unsigned const word = offset % 8;
	for (unsigned q = 0; q < QCHANNELS; q++)
	{
		if (BIT(m_qeer, q) && (BIT(m_qchmap[q], 5, 9) == set) && (BIT(m_qchmap[q], 2, 3) == word))
			trigger(set, true, q);
	}
}

void tms320c6745_edma_device::event(unsigned channel)
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
	for (unsigned ch = 0; ch < CHANNELS; ch++)
	{
		if (BIT(pending, ch))
		{
			m_er &= ~(1U << ch);
			trigger(ch, false, ch);
		}
	}
}

void tms320c6745_edma_device::trigger(unsigned set, bool qdma, unsigned channel)
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
	unsigned const tcc = BIT(opt, 12, 6);
	bool const sam = BIT(opt, 0);
	bool const dam = BIT(opt, 1);

	m_busy = true;

	auto const copy_array = [this, acnt, sam, dam, opt] (u32 s, u32 d)
	{
		// constant addressing wraps within the FIFO width
		unsigned const fwid = 1U << BIT(opt, 8, 3);
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
				unsigned const next = ((link - 0x4000) & 0xfff) / 32;
				for (unsigned w = 0; w < 8; w++)
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

void tms320c6745_edma_device::completion(unsigned tcc, bool interrupt, bool chain)
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
	for (unsigned r = 0; r < 4; r++)
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
