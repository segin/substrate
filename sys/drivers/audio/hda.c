/*
 * hda.c - Intel HDA controller driver.
 *
 * Discovers HDA controllers on the PCI bus, performs the standard
 * controller reset, brings up the CORB / RIRB ring buffers for codec
 * communication, and registers the first detected codec's audio
 * function group as an audio_dev_t.
 *
 * Playback is ring-buffered with an IRQ-driven refill: write() appends PCM to
 * a deep software FIFO and the BCIS interrupt handler stages it into the
 * stream-descriptor-0 BDL ring autonomously (hda_feed), so the controller
 * keeps playing across scheduling jitter instead of underrunning to silence.
 */

#include <stdio.h>
#include <string.h>

#include <drivers/audio/audio.h>
#include <drivers/audio/audio_fifo.h>
#include <drivers/audio/hda.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/pci.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <sys/audioio.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>

#define HDA_PCI_CLASS_MULTIMEDIA   0x04
#define HDA_PCI_SUBCLASS_HDA       0x03

#define HDA_CORB_ENTRIES           256
#define HDA_RIRB_ENTRIES           256
#define HDA_BDL_ENTRIES            32
#define HDA_CHUNK_BYTES            4096U
#define HDA_DEFAULT_RATE           48000U
#define HDA_FIFO_BYTES             (256U * 1024U) /* deep software PCM FIFO */
#define HDA_PREBUFFER_SLOTS        8U     /* DMA slots staged before start */
/* Per-verb spin budget.  Bounded low enough that a codec that never
 * answers costs a visible pause rather than minutes of boot. */
#define HDA_VERB_TIMEOUT           20000U

/* ------------------------------------------------------------------- */
/* Pure helpers (also reachable from host tests)                       */
/* ------------------------------------------------------------------- */

/*
 * Which payload width a command uses.  The HDA spec gives a 16-bit payload
 * to exactly four commands -- 2h and 3h (set converter format / amp gain)
 * and their Ah/Bh getters -- and an 8-bit payload to every other, 12-bit,
 * command.  The constants spell the four as 0xN00.
 *
 * This used to be `verb >= 0xF00`, which is wrong in both directions: it
 * called 0xA00/0xB00 long, and it called every 0x7xx command short.  The
 * whole 0x7xx block is what configures a codec (SET_POWER_STATE 0x705,
 * SET_CONVERTER_STREAM_CHANNEL 0x706, SET_PIN_WIDGET_CONTROL 0x707,
 * SET_EAPD_BTL_ENABLE 0x70C), so every one of them would have had its
 * payload land on top of the command bits.  It stayed invisible only
 * because the driver never sent any of them.
 */
static int hda_verb_is_short(uint16_t verb)
{
	if ((verb & 0x00FF) != 0) {
		return 0;
	}
	switch ((verb >> 8) & 0x0F) {
	case 0x2: case 0x3: case 0xA: case 0xB:
		return 1;
	default:
		return 0;
	}
}

uint32_t hda_pack_verb(uint8_t cad, uint8_t nid, uint16_t verb,
                       uint16_t payload)
{
	uint32_t v = 0;

	v |= ((uint32_t)(cad & 0x0F)) << 28;
	v |= ((uint32_t)nid) << 20;
	if (hda_verb_is_short(verb)) {
		/* 4-bit command in bits 19..16, 16-bit payload in 15..0. */
		v |= ((uint32_t)(verb & 0xF00)) << 8;
		v |= (uint32_t)(payload & 0xFFFF);
	} else {
		/* 12-bit command in bits 19..8, 8-bit payload in 7..0. */
		v |= ((uint32_t)(verb & 0x0FFF)) << 8;
		v |= (uint32_t)(payload & 0x00FF);
	}
	return v;
}

uint16_t hda_encode_format(uint32_t sample_rate, uint32_t bits_per_sample,
                           uint32_t channels)
{
	uint16_t fmt = 0;
	uint16_t bits;
	uint16_t chan;
	uint32_t base_rate;
	uint16_t mult;
	uint16_t div;

	if (channels == 0 || channels > 16) {
		return 0;
	}
	chan = (uint16_t)((channels - 1) & 0x0F);

	switch (bits_per_sample) {
	case 8:  bits = 0; break;
	case 16: bits = 1; break;
	case 20: bits = 2; break;
	case 24: bits = 3; break;
	case 32: bits = 4; break;
	default: return 0;
	}

	/* Determine the 48 kHz vs 44.1 kHz family and the multiplier/divisor
	 * pair.  Substrate currently exposes the canonical commercial rates;
	 * unsupported rates fall back to 48 kHz. */
	if (sample_rate % 44100U == 0) {
		base_rate = 1;   /* BASE = 44.1 kHz */
		mult = (uint16_t)((sample_rate / 44100U) - 1);
		div = 0;
	} else if (sample_rate % 48000U == 0) {
		base_rate = 0;   /* BASE = 48 kHz */
		mult = (uint16_t)((sample_rate / 48000U) - 1);
		div = 0;
	} else if (sample_rate == 22050U) {
		base_rate = 1; mult = 0; div = 1;
	} else if (sample_rate == 24000U || sample_rate == 32000U) {
		base_rate = 0; mult = 0;
		div = (sample_rate == 24000U) ? 1 : 0;
	} else {
		base_rate = 0; mult = 0; div = 0;
	}
	if (mult > 7) mult = 7;
	if (div > 7)  div = 7;

	fmt |= (uint16_t)(base_rate << 14);
	fmt |= (uint16_t)(mult << 11);
	fmt |= (uint16_t)(div << 8);
	fmt |= (uint16_t)(bits << 4);
	fmt |= chan;
	return fmt;
}

void hda_build_bdl_entry(hda_bdl_entry_t *entry, uint64_t buf_phys,
                         uint32_t length, int ioc)
{
	if (entry == NULL) {
		return;
	}
	entry->buf_phys = buf_phys;
	entry->length   = length;
	entry->flags    = (uint32_t)(ioc ? HDA_BDL_F_IOC : 0);
}

/* ------------------------------------------------------------------- */
/* Driver state                                                        */
/* ------------------------------------------------------------------- */

typedef struct hda_dev {
	pci_device_t   *pdev;
	volatile uint8_t *mmio;
	int              irq;

	uint8_t          oss;           /* output streams */
	uint8_t          iss;           /* input streams */
	uint8_t          bss;           /* bidirectional */

	uint16_t         codec_mask;    /* STATESTS bitmap */
	uint8_t          codec_addr;    /* first present codec */

	uint32_t        *corb;
	dma_addr_t       corb_phys;
	uint16_t         corb_wp;

	uint64_t        *rirb;          /* responses are 64-bit */
	dma_addr_t       rirb_phys;
	uint16_t         rirb_rp;
	uint32_t         verb_timeouts;

	hda_bdl_entry_t *bdl;
	dma_addr_t       bdl_phys;
	/*
	 * One page per BDL slot rather than a single contiguous block.  A BDL
	 * IS a scatter list -- each entry carries its own address, so the
	 * slots never needed to be contiguous with each other, and demanding
	 * 128 KiB of contiguous direct-mapped memory made attach depend on an
	 * order-5 buddy allocation that does not succeed at boot on this
	 * kernel (single pages allocate fine; the order-5 request never
	 * returns).  Per-slot pages are order-0 allocations.
	 */
	void            *chunk[HDA_BDL_ENTRIES];
	dma_addr_t       chunk_pa[HDA_BDL_ENTRIES];
	uint8_t          stream_tag;
	uint8_t          next_idx;
	/*
	 * MMIO offset of the OUTPUT stream descriptor we drive.  Stream
	 * descriptors are laid out input-first: ISS input descriptors, then
	 * OSS output ones, then bidirectional.  So output stream 0 lives at
	 * index iss, not 0.  Hardcoding the first descriptor (0x80) drives an
	 * INPUT stream on any controller with iss > 0 -- RUN reads back set
	 * and FIFORDY comes up, but nothing is ever played and LPIB never
	 * moves, because it is a capture engine.  QEMU's intel-hda reports
	 * iss=4, so the base is 0x80 + 4*0x20 = 0x100.
	 */
	uint32_t         sd_base;

	/* Codec output path, found by hda_codec_configure(). */
	uint8_t          afg_nid;
	uint8_t          dac_nid;
	uint8_t          pin_nid;
	int              have_path;

	/* Back-pressure: writes_queued bumps in hda_write after we
	 * advance LVI; slots_played bumps in IRQ handler on BCIS.
	 * Block when in_flight = (writes_queued - slots_played) hits
	 * BDL_ENTRIES - 1 to leave a slot for the controller's
	 * prefetch.  Same template as ac97 — independent of any
	 * hardware position register. */
	volatile uint32_t writes_queued;
	volatile uint32_t slots_played;

	int              running;       /* SDCTL.RUN has been set; don't
	                                 * re-write CTL on every queue */

	/*
	 * Deep software PCM FIFO decoupling the write() producer from the
	 * DMA-ring consumer (the IRQ-driven feeder), so the controller keeps
	 * playing across scheduling jitter instead of underrunning.
	 */
	audio_fifo_t     fifo;
	void            *fifo_buf;

	/* Serialises the DMA-ring feeder (hda_feed) against the IRQ handler,
	 * the priming path, and other CPUs.  IRQ-safe: held with local
	 * interrupts masked. */
	spinlock_t       feed_lock;

	audio_dev_t      audio;
} hda_dev_t;

#define HDA_MAX_CONTROLLERS 2
static hda_dev_t hda_devices[HDA_MAX_CONTROLLERS];
static int hda_device_count;

/* ------------------------------------------------------------------- */
/* MMIO accessors                                                      */
/* ------------------------------------------------------------------- */

static uint8_t hda_read8(hda_dev_t *d, uint32_t off)
{
	return *(volatile uint8_t *)(d->mmio + off);
}

static void hda_write8(hda_dev_t *d, uint32_t off, uint8_t v)
{
	*(volatile uint8_t *)(d->mmio + off) = v;
}

static uint16_t hda_read16(hda_dev_t *d, uint32_t off)
{
	return *(volatile uint16_t *)(d->mmio + off);
}

static void hda_write16(hda_dev_t *d, uint32_t off, uint16_t v)
{
	*(volatile uint16_t *)(d->mmio + off) = v;
}

static uint32_t hda_read32(hda_dev_t *d, uint32_t off)
{
	return *(volatile uint32_t *)(d->mmio + off);
}

static void hda_write32(hda_dev_t *d, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(d->mmio + off) = v;
}

/* ------------------------------------------------------------------- */
/* Controller bring-up                                                 */
/* ------------------------------------------------------------------- */

static int hda_controller_reset(hda_dev_t *d)
{
	uint32_t budget;

	/* Drop CRST to enter reset, wait for clear. */
	hda_write32(d, HDA_REG_GCTL,
	            hda_read32(d, HDA_REG_GCTL) & ~HDA_GCTL_CRST);
	for (budget = 0; budget < 1000000; budget++) {
		if ((hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) == 0) {
			break;
		}
	}
	/* Raise CRST to leave reset, wait for set. */
	hda_write32(d, HDA_REG_GCTL,
	            hda_read32(d, HDA_REG_GCTL) | HDA_GCTL_CRST);
	for (budget = 0; budget < 1000000; budget++) {
		if (hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) {
			break;
		}
	}
	if ((hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) == 0) {
		return -EIO;
	}

	/* The spec mandates ~521 us for codecs to assert STATESTS; the
	 * pause loop above drains plenty of time on physical hardware and
	 * QEMU returns instantly. */
	for (budget = 0; budget < 1000; budget++) {
		__asm__ volatile("pause");
	}
	return 0;
}

static int hda_corb_rirb_setup(hda_dev_t *d)
{
	d->corb = dma_alloc_coherent(HDA_CORB_ENTRIES * sizeof(uint32_t),
	                             &d->corb_phys);
	if (d->corb == NULL) {
		return -ENOMEM;
	}
	d->rirb = dma_alloc_coherent(HDA_RIRB_ENTRIES * sizeof(uint64_t),
	                             &d->rirb_phys);
	if (d->rirb == NULL) {
		dma_free_coherent(d->corb, HDA_CORB_ENTRIES * sizeof(uint32_t));
		d->corb = NULL;
		return -ENOMEM;
	}

	memset(d->corb, 0, HDA_CORB_ENTRIES * sizeof(uint32_t));
	memset(d->rirb, 0, HDA_RIRB_ENTRIES * sizeof(uint64_t));

	/* Stop both before reprogramming. */
	hda_write8(d, HDA_REG_CORBCTL, 0);
	hda_write8(d, HDA_REG_RIRBCTL, 0);

	/* Program CORB. */
	hda_write32(d, HDA_REG_CORBLBASE, (uint32_t)d->corb_phys);
	hda_write32(d, HDA_REG_CORBUBASE, 0);
	hda_write8(d,  HDA_REG_CORBSIZE, HDA_RBSIZE_256);
	hda_write16(d, HDA_REG_CORBWP, 0);
	/* Reset read pointer: write 1 to bit 15, wait, write 0. */
	hda_write16(d, HDA_REG_CORBRP, 0x8000);
	hda_write16(d, HDA_REG_CORBRP, 0x0000);
	d->corb_wp = 0;

	/* Program RIRB. */
	hda_write32(d, HDA_REG_RIRBLBASE, (uint32_t)d->rirb_phys);
	hda_write32(d, HDA_REG_RIRBUBASE, 0);
	hda_write8(d,  HDA_REG_RIRBSIZE, HDA_RBSIZE_256);
	hda_write16(d, HDA_REG_RIRBWP, 0x8000);   /* reset WP */
	hda_write16(d, HDA_REG_RINTCNT, 1);
	d->rirb_rp = 0;

	hda_write8(d, HDA_REG_CORBCTL, HDA_CORBCTL_RUN);
	/*
	 * RUN + RINTCTL.  The response interrupt is only safe because the IRQ
	 * handler now clears RIRBSTS: with RINTCNT=1 every verb raises one,
	 * and leaving it unacknowledged latches the controller-interrupt
	 * summary on so the shared level-triggered INTx never drops -- which
	 * wedged the machine inside the interrupt handler and was why this
	 * driver hung the boot outright.
	 */
	hda_write8(d, HDA_REG_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINTCTL);
	return 0;
}

/*
 * Send a verb and synchronously wait for the response.  Returns the
 * lower 32 bits of the RIRB response (the upper 32 bits hold response
 * extended data we don't currently consume).  Returns 0 on timeout.
 */
static uint32_t hda_send_verb(hda_dev_t *d, uint8_t cad, uint8_t nid,
                              uint16_t verb, uint16_t payload)
{
	uint32_t encoded = hda_pack_verb(cad, nid, verb, payload);
	uint16_t wp;
	uint32_t budget;

	wp = (uint16_t)((d->corb_wp + 1) % HDA_CORB_ENTRIES);
	d->corb[wp] = encoded;
	/* Publish the CORB entry before ringing the doorbell. */
	__sync_synchronize();
	hda_write16(d, HDA_REG_CORBWP, wp);
	d->corb_wp = wp;

	/*
	 * Wait for the RIRB write pointer to move past our own read pointer,
	 * then consume exactly one response and advance.
	 *
	 * This used to compare the RIRB write pointer against the CORB write
	 * pointer, which assumes the two rings advance in lockstep forever.
	 * They do not: one dropped response, or any unsolicited response,
	 * offsets them permanently, and from then on every command spins its
	 * entire timeout and returns 0.  Harmless while the driver sent a
	 * single verb at boot; with a codec graph walk sending a hundred it
	 * turns into minutes of dead spinning.  Tracking our own read pointer
	 * is what the BSD drivers do and is self-correcting.
	 */
	for (budget = 0; budget < HDA_VERB_TIMEOUT; budget++) {
		uint16_t rwp = hda_read16(d, HDA_REG_RIRBWP) & 0xFF;

		if (rwp != d->rirb_rp) {
			d->rirb_rp = (uint16_t)((d->rirb_rp + 1) %
			                        HDA_RIRB_ENTRIES);
			return (uint32_t)d->rirb[d->rirb_rp];
		}
	}
	d->verb_timeouts++;
	return 0;
}

/* ------------------------------------------------------------------- */
/* Codec configuration                                                 */
/* ------------------------------------------------------------------- */
/*
 * The controller only moves bytes; the codec decides whether they become
 * sound.  Until this ran, the driver configured the controller perfectly
 * and never sent the codec a single command, so playback DMA'd happily
 * into silence.  Same sequence FreeBSD (hdaa.c) and NetBSD (hdafg.c) use:
 * find the audio function group, power it up, pick a DAC and an output
 * pin, route one to the other, tell the DAC which stream tag to consume,
 * enable the pin, and unmute both amps.
 */

static uint32_t hda_get_param(hda_dev_t *d, uint8_t nid, uint8_t param)
{
	return hda_send_verb(d, d->codec_addr, nid, HDA_VERB_GET_PARAMETER,
	                     param);
}

/*
 * Unmute a widget's output amp and set it to its 0 dB point.  Amps power
 * up muted on most codecs, so skipping this is silence even with a
 * perfectly routed graph.  AMPCAP offset is the 0 dB setting; when a codec
 * reports none, use the top step rather than 0, which is the quietest.
 */
static void hda_amp_unmute_out(hda_dev_t *d, uint8_t nid)
{
	uint32_t caps = hda_get_param(d, nid, HDA_PARAM_OUTPUT_AMP_CAPS);
	uint8_t gain = HDA_AMPCAP_OFFSET(caps);

	if (gain == 0) {
		gain = (uint8_t)HDA_AMPCAP_NUMSTEPS(caps);
	}
	hda_send_verb(d, d->codec_addr, nid, HDA_VERB_SET_AMP_GAIN_MUTE,
	              (uint16_t)(HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT |
	                         HDA_AMP_SET_RIGHT |
	                         (gain & HDA_AMP_GAIN_MASK)));
}

/*
 * Index of `target` in `nid`'s connection list, or -1.  Short-form lists
 * pack four 8-bit entries per response, long-form two 16-bit ones.  Range
 * entries (high bit set on a short entry) are not expanded: they only
 * appear on large mixers, and picking the explicit match is enough.
 */
static int hda_conn_index(hda_dev_t *d, uint8_t nid, uint8_t target)
{
	uint32_t lenr = hda_get_param(d, nid, HDA_PARAM_CONN_LIST_LEN);
	uint8_t len = (uint8_t)HDA_CONNLIST_LEN(lenr);
	int is_long = (lenr & HDA_CONNLIST_LONG) != 0;
	uint8_t i;

	for (i = 0; i < len; i++) {
		uint32_t resp;
		uint8_t entry;

		if (is_long) {
			resp = hda_send_verb(d, d->codec_addr, nid,
			                     HDA_VERB_GET_CONN_LIST,
			                     (uint16_t)(i & ~1u));
			entry = (uint8_t)((resp >> ((i & 1) * 16)) & 0xFF);
		} else {
			resp = hda_send_verb(d, d->codec_addr, nid,
			                     HDA_VERB_GET_CONN_LIST,
			                     (uint16_t)(i & ~3u));
			entry = (uint8_t)((resp >> ((i & 3) * 8)) & 0xFF);
		}
		if (entry == target) {
			return (int)i;
		}
	}
	return -1;
}

/*
 * Point `pin` at `dac`, directly if the pin lists it, otherwise through one
 * intermediate mixer/selector.  Depth two covers the real topologies: QEMU
 * wires pin straight to DAC, while physical codecs usually interpose a
 * mixer.  Mixers take every input at once and need no selection, so only a
 * selector's index is actually committed.
 */
static int hda_route_pin_to_dac(hda_dev_t *d, uint8_t pin, uint8_t dac)
{
	uint32_t lenr;
	uint8_t len, i;
	int idx;

	idx = hda_conn_index(d, pin, dac);
	if (idx >= 0) {
		hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_CONN_SELECT,
		              (uint16_t)idx);
		return 0;
	}

	lenr = hda_get_param(d, pin, HDA_PARAM_CONN_LIST_LEN);
	len = (uint8_t)HDA_CONNLIST_LEN(lenr);
	for (i = 0; i < len; i++) {
		uint32_t resp = hda_send_verb(d, d->codec_addr, pin,
		                              HDA_VERB_GET_CONN_LIST,
		                              (uint16_t)(i & ~3u));
		uint8_t mid = (uint8_t)((resp >> ((i & 3) * 8)) & 0xFF);
		uint32_t caps;
		int type;

		if (mid == 0) {
			continue;
		}
		caps = hda_get_param(d, mid, HDA_PARAM_AUDIO_WIDGET_CAPS);
		type = HDA_AW_TYPE(caps);
		if (type != HDA_AW_TYPE_MIXER && type != HDA_AW_TYPE_SELECTOR) {
			continue;
		}
		if (hda_conn_index(d, mid, dac) < 0) {
			continue;
		}
		if (type == HDA_AW_TYPE_SELECTOR) {
			hda_send_verb(d, d->codec_addr, mid,
			              HDA_VERB_SET_CONN_SELECT,
			              (uint16_t)hda_conn_index(d, mid, dac));
		}
		if (caps & HDA_AW_OUT_AMP) {
			hda_amp_unmute_out(d, mid);
		}
		hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_CONN_SELECT,
		              (uint16_t)i);
		return 0;
	}
	return -ENODEV;
}

/* Tell the DAC which format and stream tag to consume.  Must match SDnFMT
 * and the tag programmed into SDCTL, and must be re-sent whenever either
 * changes -- a converter left on stream 0 is disconnected. */
static void hda_codec_bind_stream(hda_dev_t *d, uint16_t fmt)
{
	if (!d->have_path) {
		return;
	}
	hda_send_verb(d, d->codec_addr, d->dac_nid, HDA_VERB_SET_CONV_FORMAT,
	              fmt);
	hda_send_verb(d, d->codec_addr, d->dac_nid, HDA_VERB_SET_CONV_STREAM,
	              (uint16_t)((d->stream_tag << 4) | 0));
}

static int hda_codec_configure(hda_dev_t *d)
{
	uint32_t sub;
	uint8_t start, count, i;
	uint8_t afg = 0;
	uint8_t dac = 0;
	uint8_t pin = 0;
	int best_pin_rank = -1;

	/* Node 0's subnodes are the function groups; we want the audio one. */
	sub = hda_get_param(d, 0, HDA_PARAM_SUBNODE_COUNT);
	start = (uint8_t)HDA_SUBNODE_START(sub);
	count = (uint8_t)HDA_SUBNODE_COUNT(sub);
	for (i = 0; i < count; i++) {
		uint8_t nid = (uint8_t)(start + i);
		uint32_t t = hda_get_param(d, nid,
		                           HDA_PARAM_FUNCTION_GROUP_TYPE);
		if ((t & 0x7F) == HDA_FGT_AUDIO) {
			afg = nid;
			break;
		}
	}
	if (afg == 0) {
		kprintf("hda: no audio function group\n");
		return -ENODEV;
	}
	d->afg_nid = afg;
	hda_send_verb(d, d->codec_addr, afg, HDA_VERB_SET_POWER_STATE,
	              HDA_PS_D0);

	/* The AFG's subnodes are the widgets.  Take the first DAC, and the
	 * best-ranked output pin that is physically connected. */
	sub = hda_get_param(d, afg, HDA_PARAM_SUBNODE_COUNT);
	start = (uint8_t)HDA_SUBNODE_START(sub);
	count = (uint8_t)HDA_SUBNODE_COUNT(sub);
	for (i = 0; i < count; i++) {
		uint8_t nid = (uint8_t)(start + i);
		uint32_t caps = hda_get_param(d, nid,
		                              HDA_PARAM_AUDIO_WIDGET_CAPS);

		switch (HDA_AW_TYPE(caps)) {
		case HDA_AW_TYPE_DAC:
			if (dac == 0) {
				dac = nid;
			}
			break;
		case HDA_AW_TYPE_PIN: {
			uint32_t pcaps = hda_get_param(d, nid,
			                               HDA_PARAM_PIN_CAPS);
			uint32_t cfg;
			int rank;

			if ((pcaps & HDA_PINCAP_OUTPUT) == 0) {
				break;
			}
			cfg = hda_send_verb(d, d->codec_addr, nid,
			                    HDA_VERB_GET_CONFIG_DEFAULT, 0);
			if (HDA_CONFIG_PORTCONN(cfg) == HDA_PORTCONN_NONE) {
				break;   /* not wired to anything */
			}
			/* Prefer speakers, then line out, then headphones;
			 * anything else output-capable is a last resort. */
			switch (HDA_CONFIG_DEVICE(cfg)) {
			case HDA_DEVICE_SPEAKER:  rank = 3; break;
			case HDA_DEVICE_LINE_OUT: rank = 2; break;
			case HDA_DEVICE_HP_OUT:   rank = 1; break;
			default:                  rank = 0; break;
			}
			if (rank > best_pin_rank) {
				best_pin_rank = rank;
				pin = nid;
			}
			break;
		}
		default:
			break;
		}
	}

	if (dac == 0 || pin == 0) {
		kprintf("hda: no output path (dac=%u pin=%u)\n", dac, pin);
		return -ENODEV;
	}
	d->dac_nid = dac;
	d->pin_nid = pin;

	hda_send_verb(d, d->codec_addr, dac, HDA_VERB_SET_POWER_STATE,
	              HDA_PS_D0);
	hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_POWER_STATE,
	              HDA_PS_D0);

	if (hda_route_pin_to_dac(d, pin, dac) != 0) {
		/* Not fatal: many pins have a single hard-wired source and no
		 * connection list at all, in which case there is nothing to
		 * select and the path is already correct. */
		kprintf("hda: no explicit route pin %u <- dac %u; "
		        "assuming hard-wired\n", pin, dac);
	}

	/* Enable the pin's output driver, keeping bits we did not set. */
	{
		uint32_t ctrl = hda_send_verb(d, d->codec_addr, pin,
		                              HDA_VERB_GET_PIN_WIDGET_CONTROL,
		                              0) & 0xFF;
		ctrl |= HDA_PIN_CTRL_OUT_ENABLE;
		if (HDA_CONFIG_DEVICE(hda_send_verb(d, d->codec_addr, pin,
		                                    HDA_VERB_GET_CONFIG_DEFAULT,
		                                    0)) == HDA_DEVICE_HP_OUT) {
			ctrl |= HDA_PIN_CTRL_HP_ENABLE;
		}
		hda_send_verb(d, d->codec_addr, pin,
		              HDA_VERB_SET_PIN_WIDGET_CONTROL, (uint16_t)ctrl);
	}

	/* External amplifier, where the pin has one -- laptop speakers are
	 * usually silent without it. */
	if (hda_get_param(d, pin, HDA_PARAM_PIN_CAPS) & HDA_PINCAP_EAPD) {
		hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_EAPD_BTL,
		              HDA_EAPD_ENABLE);
	}

	if (hda_get_param(d, dac, HDA_PARAM_AUDIO_WIDGET_CAPS) &
	    HDA_AW_OUT_AMP) {
		hda_amp_unmute_out(d, dac);
	}
	if (hda_get_param(d, pin, HDA_PARAM_AUDIO_WIDGET_CAPS) &
	    HDA_AW_OUT_AMP) {
		hda_amp_unmute_out(d, pin);
	}

	d->have_path = 1;
	hda_codec_bind_stream(d, hda_read16(d, d->sd_base + HDA_SD_FMT));

	kprintf("hda: codec %u afg=%u dac=%u pin=%u tag=%u\n",
	        d->codec_addr, afg, dac, pin, d->stream_tag);
	return 0;
}

/* ------------------------------------------------------------------- */
/* DMA-ring feeder (consumer side of the software FIFO)                */
/* ------------------------------------------------------------------- */

/*
 * Stage PCM from the software FIFO into free BDL slots.  Caller must hold
 * d->feed_lock (IRQ-safe), which makes this the sole writer of the ring
 * state across the IRQ handler, the priming path, and other CPUs.
 */
static void hda_feed(hda_dev_t *d)
{
	if (d->chunk[0] == NULL || d->fifo_buf == NULL) {
		return;
	}
	for (;;) {
		uint32_t played = __atomic_load_n(&d->slots_played,
		                                  __ATOMIC_ACQUIRE);
		uint32_t in_flight = d->writes_queued - played;
		size_t avail;
		size_t copy_len;
		uint8_t slot;

		if (in_flight >= HDA_BDL_ENTRIES) {
			break;
		}
		avail = audio_fifo_used(&d->fifo);
		if (avail == 0) {
			break;
		}
		copy_len = (avail > HDA_CHUNK_BYTES) ? HDA_CHUNK_BYTES : avail;

		slot = d->next_idx;
		(void)audio_fifo_read(&d->fifo, (uint8_t *)d->chunk[slot],
		                      copy_len);
		/*
		 * Every BDL entry is a fixed HDA_CHUNK_BYTES, so a partial
		 * fill must be padded rather than shortening the entry: the
		 * ring length is fixed at CBL and the controller will play the
		 * whole slot regardless.  Padding with zeros makes the tail
		 * silence instead of whatever the slot held last time round.
		 */
		if (copy_len < HDA_CHUNK_BYTES) {
			memset((uint8_t *)d->chunk[slot] + copy_len, 0,
			       HDA_CHUNK_BYTES - copy_len);
		}

		/* Publish the data before the controller can reach the slot. */
		__sync_synchronize();

		d->writes_queued++;
		d->next_idx = (uint8_t)((slot + 1U) % HDA_BDL_ENTRIES);
	}
}

/*
 * Start or restart the output stream as needed.  Called from the producer;
 * takes the IRQ-safe feed lock.  While running the IRQ feeder keeps the ring
 * full, so this only acts on the priming and underrun-restart edges.
 */
static void hda_kick(hda_dev_t *d)
{
	unsigned long flags = spinlock_acquire_irq(&d->feed_lock);

	if (d->running) {
		uint32_t ctl = hda_read32(d, d->sd_base + HDA_SD_CTL);
		if ((ctl & HDA_SDCTL_RUN) == 0) {
			/* Halted (drained the ring): resync the counters so a
			 * stale in_flight doesn't block the restart, then
			 * re-prime. */
			d->slots_played = d->writes_queued;
			d->running = 0;
		}
	}

	/*
	 * Always feed, running or not.  This used to sit inside the
	 * !running branch, so once the stream started the producer never
	 * staged another buffer and the ring could only be refilled from the
	 * completion interrupt -- one missed IOC and playback deadlocks
	 * permanently with the software FIFO full and the ring starved.
	 */
	hda_feed(d);

	if (!d->running) {
		uint32_t in_flight;
		in_flight = d->writes_queued -
		            __atomic_load_n(&d->slots_played, __ATOMIC_ACQUIRE);
		if (in_flight >= HDA_PREBUFFER_SLOTS ||
		    (in_flight > 0 && audio_fifo_used(&d->fifo) == 0)) {
			uint32_t ctl;
			/* CBL and LVI describe the whole fixed ring and were
			 * programmed at init; they must not be rewritten per
			 * start.  CBL has to equal the sum of the valid BDL
			 * entry lengths, and this used to set CBL to the full
			 * ring while LVI still said one entry, so the
			 * controller streamed off the end of the programmed
			 * descriptors. */
			/*
			 * Re-publish the descriptor before every start.  SRST
			 * (hda_flush) clears BDPL/CBL/LVI, and starting with a
			 * zero BDL base makes the controller fetch nothing:
			 * RUN reads back set, FIFORDY comes up, and LPIB stays
			 * at 0 forever with no completion interrupt -- which
			 * deadlocks the writer, because past the first buffer
			 * the ring is only refilled from the completion path.
			 */
			hda_write32(d, d->sd_base + HDA_SD_BDPL,
			            (uint32_t)d->bdl_phys);
			hda_write32(d, d->sd_base + HDA_SD_BDPU, 0);
			hda_write16(d, d->sd_base + HDA_SD_LVI,
			            HDA_BDL_ENTRIES - 1);
			hda_write32(d, d->sd_base + HDA_SD_CBL,
			            (uint32_t)(HDA_BDL_ENTRIES * HDA_CHUNK_BYTES));
			ctl = HDA_SDCTL_RUN | HDA_SDCTL_IOCE |
			      ((uint32_t)d->stream_tag << HDA_SDCTL_STREAM_SHIFT);
			hda_write32(d, d->sd_base + HDA_SD_CTL, ctl);
			d->running = 1;
		}
	}

	spinlock_release_irq(&d->feed_lock, flags);
}

/* ------------------------------------------------------------------- */
/* IRQ                                                                 */
/* ------------------------------------------------------------------- */

static int hda_irq_handler(unsigned int irq, void *dev_id, void *frame)
{
	hda_dev_t *d = dev_id;
	uint32_t status;
	uint8_t  sdsts;

	(void)irq;
	(void)frame;
	if (d == NULL) {
		return 0;
	}
	status = hda_read32(d, HDA_REG_INTSTS);
	if (status == 0) {
		return 0;
	}

	/*
	 * Controller interrupt: a codec state change (STATESTS).  This MUST be
	 * acknowledged at the source.  INTSTS bit 30 is only a summary of it,
	 * so writing INTSTS alone leaves STATESTS set, the summary re-asserts
	 * immediately, and because PCI INTx is level triggered the line never
	 * drops -- the handler is re-entered forever and the machine wedges
	 * inside it.  STATESTS is RW1C: write the bits back to clear.
	 *
	 * It fires the instant CIE is armed, because reset leaves the
	 * codec-present bit latched, so this is not a rare path -- it is the
	 * first interrupt the controller ever raises.
	 */
	if (status & HDA_INTCTL_CIE) {
		uint16_t sts = hda_read16(d, HDA_REG_STATESTS);
		uint8_t rsts = hda_read8(d, HDA_REG_RIRBSTS);

		if (sts != 0) {
			hda_write16(d, HDA_REG_STATESTS, sts);
		}
		/* RIRBSTS is the other half of the controller interrupt and is
		 * likewise RW1C; leaving it set keeps CIS asserted forever. */
		if (rsts != 0) {
			hda_write8(d, HDA_REG_RIRBSTS, rsts);
		}
	}
	/* ACK output stream 0 status if it fired.  BCIS = buffer
	 * completion (one IOC-marked BDL slot drained); track for the
	 * write-path back-pressure. */
	sdsts = hda_read8(d, d->sd_base + HDA_SD_STS);
	if (sdsts & (HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE)) {
		if (sdsts & HDA_SDSTS_BCIS) {
			unsigned long f = spinlock_acquire_irq(&d->feed_lock);
			uint32_t done = __atomic_fetch_add(&d->slots_played, 1,
			                                   __ATOMIC_ACQ_REL);
			/* The ring is cyclic and never stops while RUN is set,
			 * so a slot we do not refill plays again.  Zero it now;
			 * hda_feed below overwrites it if data is waiting. */
			if (d->chunk[0] != NULL) {
				memset(d->chunk[done % HDA_BDL_ENTRIES], 0,
				       HDA_CHUNK_BYTES);
			}
			/* Autonomously refill freed ring slot(s) from the
			 * software FIFO so playback survives producer jitter. */
			hda_feed(d);
			spinlock_release_irq(&d->feed_lock, f);
			(void)sleepq_wake_all(d);
		}
		hda_write8(d, d->sd_base + HDA_SD_STS,
		           sdsts & (HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE |
		                    HDA_SDSTS_DESE));
	}
	/* INTSTS is RW1C — write back to clear. */
	hda_write32(d, HDA_REG_INTSTS, status);
	return 1;
}

/* ------------------------------------------------------------------- */
/* Output stream 0 setup                                               */
/* ------------------------------------------------------------------- */

static int hda_output_stream_init(hda_dev_t *d)
{
	d->bdl = dma_alloc_coherent(HDA_BDL_ENTRIES * sizeof(hda_bdl_entry_t),
	                            &d->bdl_phys);
	if (d->bdl == NULL) {
		return -ENOMEM;
	}
	memset(d->bdl, 0, HDA_BDL_ENTRIES * sizeof(hda_bdl_entry_t));

	for (int i = 0; i < HDA_BDL_ENTRIES; i++) {
		d->chunk[i] = dma_alloc_coherent(HDA_CHUNK_BYTES,
		                                 &d->chunk_pa[i]);
		if (d->chunk[i] == NULL) {
			kprintf("hda: chunk page %d allocation failed\n", i);
			while (--i >= 0) {
				dma_free_coherent(d->chunk[i], HDA_CHUNK_BYTES);
				d->chunk[i] = NULL;
			}
			dma_free_coherent(d->bdl, HDA_BDL_ENTRIES *
			                  sizeof(hda_bdl_entry_t));
			d->bdl = NULL;
			return -ENOMEM;
		}
	}

	/*
	 * Plain kernel heap, NOT dma_alloc_coherent: this FIFO is a purely
	 * software staging ring that hda_feed() copies OUT of into the per-slot
	 * chunk pages, which are what the controller actually DMAs.  Nothing
	 * FIFO to hardware -- the dma_addr_t it used to produce was stored and
	 * never read -- so demanding 256 KiB of physically contiguous
	 * direct-mapped memory for it bought nothing and made attach depend on
	 * a large order-6 buddy allocation succeeding at boot.  uac.c already
	 * uses kmalloc for the identical FIFO.
	 */
	d->fifo_buf = kmalloc(HDA_FIFO_BYTES);
	if (d->fifo_buf == NULL) {
		kprintf("hda: FIFO allocation failed\n");
		for (int i = 0; i < HDA_BDL_ENTRIES; i++) {
			dma_free_coherent(d->chunk[i], HDA_CHUNK_BYTES);
			d->chunk[i] = NULL;
		}
		dma_free_coherent(d->bdl,
		                  HDA_BDL_ENTRIES * sizeof(hda_bdl_entry_t));
		d->bdl = NULL;
		return -ENOMEM;
	}
	audio_fifo_init(&d->fifo, (uint8_t *)d->fifo_buf, HDA_FIFO_BYTES);

	d->stream_tag = 1;   /* tag 0 is reserved per spec */
	d->next_idx = 0;

	/* Reset stream descriptor 0 (output stream 0).  Per HDA spec
	 * §3.3.35, software must wait for SRST to read back as 1
	 * (controller acknowledged the request), then clear SRST and
	 * wait for it to read back as 0 (reset complete).  The
	 * controller is required to honor a 100 µs link-reset window;
	 * the second readback poll inherently waits for that since
	 * each MMIO read costs hundreds of nanoseconds. */
	hda_write32(d, d->sd_base + HDA_SD_CTL, HDA_SDCTL_SRST);
	{
		int budget;
		for (budget = 0; budget < 1000; budget++) {
			if (hda_read32(d, d->sd_base + HDA_SD_CTL) &
			    HDA_SDCTL_SRST) {
				break;
			}
		}
	}
	hda_write32(d, d->sd_base + HDA_SD_CTL, 0);
	{
		int budget;
		for (budget = 0; budget < 1000; budget++) {
			if ((hda_read32(d, d->sd_base + HDA_SD_CTL) &
			     HDA_SDCTL_SRST) == 0) {
				break;
			}
		}
	}

	/*
	 * Program the whole ring once.  An HDA output stream is cyclic over a
	 * fixed set of descriptors: CBL is the total byte length and must equal
	 * the sum of the valid entries, and LVI is the index of the last one.
	 * Refilling happens by rewriting slot CONTENTS on IOC completion, never
	 * by appending entries and moving LVI while running.
	 */
	{
		int i;
		for (i = 0; i < HDA_BDL_ENTRIES; i++) {
			memset(d->chunk[i], 0, HDA_CHUNK_BYTES);
			hda_build_bdl_entry(&d->bdl[i],
			                    (uint64_t)d->chunk_pa[i],
			                    HDA_CHUNK_BYTES, 1 /* IOC */);
		}
	}
	hda_write32(d, d->sd_base + HDA_SD_BDPL, (uint32_t)d->bdl_phys);
	hda_write32(d, d->sd_base + HDA_SD_BDPU, 0);
	hda_write16(d, d->sd_base + HDA_SD_LVI, HDA_BDL_ENTRIES - 1);
	hda_write32(d, d->sd_base + HDA_SD_CBL,
	            (uint32_t)(HDA_BDL_ENTRIES * HDA_CHUNK_BYTES));
	hda_write16(d, d->sd_base + HDA_SD_FMT,
	            hda_encode_format(HDA_DEFAULT_RATE, 16, 2));
	return 0;
}

/* ------------------------------------------------------------------- */
/* Backend ops                                                         */
/* ------------------------------------------------------------------- */

static int hda_open(audio_dev_t *adev, int mode)
{
	(void)adev;
	(void)mode;
	return 0;
}

static int hda_close(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;

	/*
	 * Stop the stream and reset the ring/FIFO state under feed_lock
	 * (IRQ-masked).  hda_feed() runs from the IRQ handler holding this
	 * lock; without it, a completion IRQ firing between CTL=0 and the
	 * reset below re-arms the ring / advances slots_played against the
	 * counters we are zeroing, corrupting the next stream's back-pressure.
	 */
	unsigned long flags = spinlock_acquire_irq(&d->feed_lock);

	hda_write32(d, d->sd_base + HDA_SD_CTL, 0);
	/* Drop latched status bits so stale BCIS doesn't bump the next
	 * stream's slots_played at open. */
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);

	/* Reset ring back-pressure state.  Otherwise the second cat
	 * inherits writes_queued from this stream but slots_played
	 * never catches up (no more IRQs after CTL=0), so the
	 * back-pressure spin thinks the ring is permanently full. */
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->next_idx      = 0;
	d->running       = 0;
	audio_fifo_reset(&d->fifo);

	spinlock_release_irq(&d->feed_lock, flags);
	return 0;
}

static int hda_set_params(audio_dev_t *adev, audio_info_t *info)
{
	hda_dev_t *d = adev->driver_data;
	uint16_t fmt;

	fmt = hda_encode_format(info->play.sample_rate,
	                        info->play.precision, info->play.channels);
	if (fmt == 0) {
		return -EINVAL;
	}
	hda_write16(d, d->sd_base + HDA_SD_FMT, fmt);
	/* The converter has its own copy of the format; leaving it on the
	 * old one makes the codec decode the stream wrongly (wrong rate /
	 * channel count) rather than fall silent, which is worse. */
	hda_codec_bind_stream(d, fmt);
	return 0;
}

static int hda_write(audio_dev_t *adev, const void *buf, size_t len)
{
	hda_dev_t *d = adev->driver_data;
	const uint8_t *src = buf;
	size_t total_consumed = 0;

	if (len == 0 || d->chunk[0] == NULL || d->fifo_buf == NULL) {
		return (int)len;
	}

	/*
	 * Producer: append PCM to the deep software FIFO; the IRQ-driven
	 * feeder stages it into the DMA ring.  Block only when the FIFO
	 * fills, so the controller stays fed even while this thread is
	 * descheduled under load.  Single-producer (one stream open at a
	 * time), matching the framework's per-device usage.
	 */
	while (total_consumed < len) {
		size_t n = audio_fifo_write(&d->fifo, src + total_consumed,
		                            len - total_consumed);
		total_consumed += n;

		hda_kick(d);   /* prime / restart; no-op while IRQ feeds */

		if (total_consumed >= len) {
			break;
		}

		if (current_thread) {
			/* Killable: break the wait on a pending unmasked signal
			 * so the player can be ^C'd / kill(1)ed instead of
			 * wedging uninterruptibly if the device stalls. */
			if (current_thread->sig_pending & ~current_thread->sig_mask) {
				break;
			}
			sleepq_add(d, current_thread);
			if (audio_fifo_free(&d->fifo) > 0) {
				sleepq_wake_all(d);
			} else {
				current_thread->flags |= THREAD_F_INTERRUPTIBLE;
				sched_sleep(d);
				current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
			}
		} else {
			__asm__ volatile("pause");
		}
	}

	if (total_consumed == 0 && current_thread &&
	    (current_thread->sig_pending & ~current_thread->sig_mask)) {
		return -EINTR;
	}
	return (int)total_consumed;
}

static int hda_drain(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static int hda_flush(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	unsigned long flags = spinlock_acquire_irq(&d->feed_lock);
	/* Stop, do not park in reset: asserting SRST and leaving it set wedges
	 * the stream descriptor for every later start. */
	hda_write32(d, d->sd_base + HDA_SD_CTL, 0);
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);
	d->next_idx      = 0;
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->running       = 0;
	audio_fifo_reset(&d->fifo);
	spinlock_release_irq(&d->feed_lock, flags);
	return 0;
}

static void hda_get_devinfo(audio_dev_t *adev, audio_device_t *out)
{
	hda_dev_t *d = adev->driver_data;

	memset(out, 0, sizeof(*out));
	snprintf(out->name, sizeof(out->name), "hda");
	snprintf(out->version, sizeof(out->version), "1.0");
	snprintf(out->config, sizeof(out->config), "%04x:%04x",
	         d->pdev != NULL ? d->pdev->vendor_id : 0,
	         d->pdev != NULL ? d->pdev->device_id : 0);
}

static int hda_get_props(audio_dev_t *adev)
{
	(void)adev;
	return AUDIO_PROP_PLAYBACK | AUDIO_PROP_CAPTURE |
	       AUDIO_PROP_FULLDUPLEX | AUDIO_PROP_INDEPENDENT;
}

static audio_dev_ops_t hda_ops = {
	.open        = hda_open,
	.close       = hda_close,
	.write       = hda_write,
	.read        = NULL,
	.set_params  = hda_set_params,
	.drain       = hda_drain,
	.flush       = hda_flush,
	.get_devinfo = hda_get_devinfo,
	.get_props   = hda_get_props,
};

/* ------------------------------------------------------------------- */
/* Discovery / init                                                    */
/* ------------------------------------------------------------------- */

static int hda_attach(pci_device_t *pdev)
{
	hda_dev_t *d;
	uint16_t cmd;
	uint16_t gcap;
	uint32_t vendor_id;

	if (hda_device_count >= HDA_MAX_CONTROLLERS) {
		return -EBUSY;
	}
	d = &hda_devices[hda_device_count];
	memset(d, 0, sizeof(*d));
	spinlock_init(&d->feed_lock, "hda_feed");
	d->pdev = pdev;

	cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
	                        PCI_CONFIG_COMMAND);
	cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	pci_write_config16(pdev->bus, pdev->slot, pdev->func,
	                   PCI_CONFIG_COMMAND, cmd);

	d->mmio = pci_iomap(pdev, 0, 16384);
	if (d->mmio == NULL) {
		kprintf("hda: failed to map BAR0\n");
		return -ENODEV;
	}
	d->irq = pci_get_irq(pdev);

	gcap = hda_read16(d, HDA_REG_GCAP);
	d->oss = (uint8_t)((gcap >> 12) & 0x0F);
	d->iss = (uint8_t)((gcap >> 8) & 0x0F);
	d->bss = (uint8_t)((gcap >> 3) & 0x1F);
	if (d->oss == 0) {
		kprintf("hda: controller advertises no output streams\n");
		return -ENODEV;
	}
	/* Output stream 0 sits after the input descriptors. */
	d->sd_base = HDA_SD_BASE + ((uint32_t)d->iss * HDA_SD_STRIDE);

	if (hda_controller_reset(d) != 0) {
		kprintf("hda: controller reset timed out\n");
		return -EIO;
	}

	d->codec_mask = hda_read16(d, HDA_REG_STATESTS);
	if (d->codec_mask == 0) {
		kprintf("hda: no codec detected\n");
		return -ENODEV;
	}
	{
		int i;
		for (i = 0; i < 15; i++) {
			if (d->codec_mask & (1u << i)) {
				d->codec_addr = (uint8_t)i;
				break;
			}
		}
	}
	/* STATESTS is RW1C and comes out of reset with the codec-present bits
	 * latched.  Clear them now, or arming CIE below immediately raises a
	 * state-change interrupt on a level-triggered shared line. */
	hda_write16(d, HDA_REG_STATESTS, d->codec_mask);

	if (hda_corb_rirb_setup(d) != 0) {
		return -ENOMEM;
	}

	/*
	 * Claim the IRQ line BEFORE arming the controller's interrupts.
	 *
	 * INTCTL.GIE makes the controller assert its PCI INTx, which is level
	 * triggered and shared -- on the emulated 'pc' machine the HDA lands
	 * on IRQ 10 alongside an IDE channel.  Arming it with no handler
	 * registered means nothing ever acknowledges the source, so the line
	 * stays asserted and the other device's handler is re-entered forever:
	 * attach never returns, audio_init never returns, and the boot wedges
	 * with the CPU parked in ide_irq_dispatch.  That is exactly what this
	 * driver did, which is why it hung the moment an HDA controller was
	 * actually present.  Registering first means the very first assertion
	 * has somewhere to go.
	 */
	if (d->irq >= 0) {
		/* Shared PCI INTx -- see the note in ac97.c. */
		(void)request_irq((unsigned int)d->irq, hda_irq_handler,
		                  IRQF_SHARED, "hda", d);
	}

	/* Now safe to arm.  Some controllers only latch a codec response with
	 * CIE armed, so this has to precede the first verb. */
	hda_write32(d, HDA_REG_INTCTL,
	            HDA_INTCTL_GIE | HDA_INTCTL_CIE | 0x40000000U);

	vendor_id = hda_send_verb(d, d->codec_addr, 0,
	                          HDA_VERB_GET_PARAMETER,
	                          HDA_PARAM_VENDOR_ID);

	if (hda_output_stream_init(d) != 0) {
		return -ENOMEM;
	}

	/* Must follow output_stream_init: the codec is bound to the stream
	 * tag and format that call establishes.  A codec we cannot route is
	 * not a usable audio device -- registering it would give userland a
	 * /dev/audio0 that silently swallows everything. */
	if (hda_codec_configure(d) != 0) {
		kprintf("hda: codec configuration failed; not registering\n");
		return -ENODEV;
	}

	/* The IRQ was claimed before INTCTL was armed, further up. */

	d->audio.ops = &hda_ops;
	d->audio.driver_data = d;
	snprintf(d->audio.name, sizeof(d->audio.name), "hda");
	if (audio_register_device(&d->audio) != 0) {
		return -EBUSY;
	}

	hda_device_count++;
	kprintf("hda: %04x:%04x oss=%u iss=%u codecs=0x%04x cad=%u "
	        "vid=0x%08x\n",
	        pdev->vendor_id, pdev->device_id, d->oss, d->iss,
	        d->codec_mask, d->codec_addr, vendor_id);
	return 0;
}

void hda_init(void)
{
	pci_device_t *pdev;
	uint8_t cls, sub;

	for (pdev = pci_first_device(); pdev != NULL;
	     pdev = pci_next_device(pdev)) {
		if (pdev->kdev == NULL) {
			continue;
		}
		cls = pdev->kdev->class;
		sub = pdev->kdev->subclass;
		if (cls != HDA_PCI_CLASS_MULTIMEDIA ||
		    sub != HDA_PCI_SUBCLASS_HDA) {
			continue;
		}
		(void)hda_attach(pdev);
	}
}
