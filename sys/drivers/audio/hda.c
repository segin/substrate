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
#include <kern/time.h>
#include <sys/audioio.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>

#define HDA_PCI_CLASS_MULTIMEDIA   0x04
#define HDA_PCI_SUBCLASS_HDA       0x03

#define HDA_BDL_ENTRIES            32
#define HDA_CHUNK_BYTES            4096U
#define HDA_DEFAULT_RATE           48000U
#define HDA_FIFO_BYTES             (256U * 1024U) /* deep software PCM FIFO */
#define HDA_PREBUFFER_SLOTS        8U     /* DMA slots staged before start */
/* Per-verb spin budget.  Bounded low enough that a codec that never
 * answers costs a visible pause rather than minutes of boot. */
#define HDA_VERB_TIMEOUT           20000U
/* Spin budget for RUN / SRST readbacks.  The spec bounds a stop at 40 us
 * (4.5.4); each MMIO read here costs hundreds of ns, so this is a wide
 * margin that still cannot hang the boot. */
#define HDA_STREAM_TIMEOUT         10000
/* Drain polling, mirroring ac97.c: give up on a stalled controller
 * rather than blocking close() forever. */
#define HDA_DRAIN_POLL_MS          10U
#define HDA_DRAIN_STALL_POLLS      150U  /* ~1.5 s of no progress -> stop */
#define HDA_DRAIN_POLL_MAX         6000U /* ~60 s absolute ceiling        */
/*
 * Controller reset timing.  The link RESET# pulse width is software's
 * responsibility (spec 3.3.7), and codec enumeration needs at least
 * 521 us / 25 frames after CRST reads back 1 (4.3).  Millisecond
 * granularity is the finest this kernel offers a tick-independent busy
 * wait for, and rounding up costs 2 ms once per controller at boot.
 */
#define HDA_RESET_HOLD_MS          1U
#define HDA_CODEC_DISCOVERY_MS     1U
/* GCAP allows 15 input + 15 output + 30 bidirectional descriptors. */
#define HDA_MAX_STREAMS            60U
/* Backstop on the INTSTS re-read loop; see hda_irq_handler(). */
#define HDA_INTR_MAX_ROUNDS        64
/* Spin budget for CORB/RIRB pointer-reset and RUN readbacks. */
#define HDA_RING_TIMEOUT           10000U
/* Output converters / pins considered when picking a path. */
#define HDA_MAX_CANDIDATES         16
/* STATESTS reports codec presence on SDI[14:0]. */
#define HDA_MAX_CODECS             15

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

/*
 * Every rate SDnFMT can express, as (BASE, MULT, DIV) field values.
 *
 * The link rate is BASE * (MULT + 1) / (DIV + 1), but only MULT 0..3
 * (x1..x4) and DIV 0..7 are legal -- spec table 40 marks MULT 100b-111b
 * reserved -- so this is not an arithmetic identity to be computed on the
 * fly.  Anything absent here has no legal encoding at all and must be
 * refused rather than approximated.
 *
 * The arithmetic version this replaces got that backwards in both
 * directions.  It only ever emitted a nonzero MULT or a nonzero DIV,
 * never both, so 32 kHz (48 kHz x2 / 3) was unreachable; its `else`
 * fell back to a perfectly valid 48 kHz encoding, so 8000, 11025, 16000
 * and 32000 all silently played at 48 kHz -- three to six times too
 * fast -- with no error anywhere for set_params to catch; and it clamped
 * an out-of-range multiplier to 7 rather than rejecting it, handing the
 * controller a reserved MULT for any rate above 192 kHz.
 */
static const struct hda_rate_enc {
	uint32_t rate;
	uint8_t  base;   /* SDnFMT bit 14:    0 = 48 kHz, 1 = 44.1 kHz */
	uint8_t  mult;   /* SDnFMT bits 13:11 (x1..x4 as 0..3)         */
	uint8_t  div;    /* SDnFMT bits 10:8  (/1../8 as 0..7)         */
} hda_rate_tab[] = {
	/* 48 kHz base */
	{      6000, 0, 0, 7 },
	{      8000, 0, 0, 5 },
	{      9600, 0, 0, 4 },
	{     12000, 0, 0, 3 },
	{     16000, 0, 0, 2 },
	{     18000, 0, 2, 7 },
	{     19200, 0, 1, 4 },
	{     24000, 0, 0, 1 },
	{     28800, 0, 2, 4 },
	{     32000, 0, 1, 2 },
	{     36000, 0, 2, 3 },
	{     38400, 0, 3, 4 },
	{     48000, 0, 0, 0 },
	{     64000, 0, 3, 2 },
	{     72000, 0, 2, 1 },
	{     96000, 0, 1, 0 },
	{    144000, 0, 2, 0 },
	{    192000, 0, 3, 0 },
	/* 44.1 kHz base */
	{      8820, 1, 0, 4 },
	{     11025, 1, 0, 3 },
	{     12600, 1, 1, 6 },
	{     14700, 1, 0, 2 },
	{     17640, 1, 1, 4 },
	{     18900, 1, 2, 6 },
	{     22050, 1, 0, 1 },
	{     25200, 1, 3, 6 },
	{     26460, 1, 2, 4 },
	{     29400, 1, 1, 2 },
	{     33075, 1, 2, 3 },
	{     35280, 1, 3, 4 },
	{     44100, 1, 0, 0 },
	{     58800, 1, 3, 2 },
	{     66150, 1, 2, 1 },
	{     88200, 1, 1, 0 },
	{    132300, 1, 2, 0 },
	{    176400, 1, 3, 0 },
};

int hda_encode_format(uint32_t sample_rate, uint32_t bits_per_sample,
                      uint32_t channels, uint16_t *out)
{
	uint16_t bits;
	size_t i;

	if (out == NULL || channels == 0 || channels > 16) {
		return -EINVAL;
	}

	switch (bits_per_sample) {
	case 8:  bits = 0; break;
	case 16: bits = 1; break;
	case 20: bits = 2; break;
	case 24: bits = 3; break;
	case 32: bits = 4; break;
	default: return -EINVAL;
	}

	for (i = 0; i < sizeof(hda_rate_tab) / sizeof(hda_rate_tab[0]); i++) {
		const struct hda_rate_enc *r = &hda_rate_tab[i];

		if (r->rate != sample_rate) {
			continue;
		}
		*out = (uint16_t)(((uint16_t)r->base << HDA_FMT_BASE_SHIFT) |
		                  ((uint16_t)r->mult << HDA_FMT_MULT_SHIFT) |
		                  ((uint16_t)r->div  << HDA_FMT_DIV_SHIFT)  |
		                  (bits << HDA_FMT_BITS_SHIFT) |
		                  (uint16_t)((channels - 1) & 0x0F));
		return 0;
	}
	return -EINVAL;
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
	int              irq_claimed;   /* request_irq() succeeded */

	uint8_t          oss;           /* output streams */
	uint8_t          iss;           /* input streams */
	uint8_t          bss;           /* bidirectional */

	uint16_t         codec_mask;    /* STATESTS bitmap */
	uint8_t          codec_addr;    /* first present codec */

	/*
	 * Ring sizes are what the controller advertises, not a constant --
	 * see hda_ring_size().  Everything that indexes these rings has to
	 * use these counts, including the modulo in hda_send_verb().
	 */
	unsigned         corb_entries;
	unsigned         rirb_entries;

	uint32_t        *corb;
	dma_addr_t       corb_phys;
	uint16_t         corb_wp;

	uint64_t        *rirb;          /* responses are 64-bit */
	dma_addr_t       rirb_phys;
	uint16_t         rirb_rp;
	uint32_t         verb_timeouts;
	/* Stream error tallies, reported on close rather than per event --
	 * they arrive from interrupt context. */
	uint32_t         fifo_errors;
	uint32_t         desc_errors;

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
	uint8_t          sd_index;     /* descriptor number, for INTCTL.SIE */

	/* Codec output path, found by hda_codec_configure(). */
	uint8_t          afg_nid;
	/*
	 * The AFG's amp capabilities, which stand in for every widget that
	 * does not set Amp Param Override.  Read once at configure time
	 * because hda_amp_caps() needs them for most widgets on real codecs.
	 */
	uint32_t         afg_outamp_caps;
	uint32_t         afg_inamp_caps;
	/* Likewise for the rate/format parameters, which default to the
	 * AFG's unless the converter sets Format Override. */
	uint32_t         afg_pcm_caps;
	uint32_t         afg_fmt_caps;
	uint32_t         dac_pcm_caps;
	uint32_t         dac_fmt_caps;
	uint8_t          dac_max_chan;
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
	 * The completion handler noticed the ring had drained and wants the
	 * engine stopped.  It must not do that itself (spec 4.5.6: "The ISR
	 * should not attempt to write to the stream Control register"), and
	 * leaving the engine running a little longer is also what lets the
	 * controller's own FIFO empty -- see hda_irq_handler().
	 */
	volatile int     halt_pending;

	/* Last format programmed into SDnFMT.  A stream reset clears the
	 * register, so the driver has to remember it to put it back. */
	uint16_t         fmt;

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

	/* Serialises CORB submission and RIRB consumption.  IRQ-safe: the
	 * completion handler does not send verbs, but attach and
	 * set_params can race on other CPUs. */
	spinlock_t       verb_lock;

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

/*
 * Bring every DMA engine to a stop and clear the latched status before
 * the controller is reset.
 *
 * 3.3.7 is explicit that this is a precondition, not hygiene: "Note that
 * the CORB/RIRB RUN bits and all Stream RUN bits must be verified
 * cleared to 0 before CRST# is written to 0 (asserted) in order to
 * assure a clean re-start."  None of it was being done, so a controller
 * inherited mid-stream from firmware or from another OS -- the
 * warm-reboot path -- was reset with its engines still running.
 *
 * WAKEEN and STATESTS need separate attention because they outlive the
 * reset: 3.3.7 again, "The exceptions are the WAKEEN and STATESTS
 * registers, which are only cleared on power-on reset".  WAKEEN was
 * never touched at all, leaving whatever the firmware had set.
 */
static void hda_quiesce(hda_dev_t *d)
{
	uint16_t gcap = hda_read16(d, HDA_REG_GCAP);
	unsigned nstreams = (unsigned)(((gcap >> 12) & 0x0F) +
	                               ((gcap >> 8) & 0x0F) +
	                               ((gcap >> 3) & 0x1F));
	unsigned i;

	/* Stream engines.  Byte 0 of each SDnCTL holds RUN. */
	for (i = 0; i < nstreams && i < HDA_MAX_STREAMS; i++) {
		uint32_t off = HDA_SD_BASE + (i * HDA_SD_STRIDE);

		hda_write8(d, off + HDA_SD_CTL, 0);
		hda_write8(d, off + HDA_SD_STS,
		           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);
	}

	/* Command/response engines. */
	hda_write8(d, HDA_REG_CORBCTL, 0);
	hda_write8(d, HDA_REG_RIRBCTL, 0);

	/* No interrupts and no wake events across the reset. */
	hda_write32(d, HDA_REG_INTCTL, 0);
	hda_write16(d, HDA_REG_WAKEEN, 0);

	/* Both RW1C, and both survive CRST. */
	hda_write16(d, HDA_REG_STATESTS, hda_read16(d, HDA_REG_STATESTS));
	hda_write8(d, HDA_REG_RIRBSTS, hda_read8(d, HDA_REG_RIRBSTS));

	/* Position buffer, in case firmware left one programmed. */
	hda_write32(d, HDA_REG_DPLBASE, 0);
	hda_write32(d, HDA_REG_DPUBASE, 0);
}

static int hda_controller_reset(hda_dev_t *d)
{
	uint32_t budget;

	hda_quiesce(d);

	/* Drop CRST to enter reset, wait for clear.  3.3.7: "After the
	 * hardware has completed sequencing into the reset state, it will
	 * report a 0 in this bit.  Software must read a 0 from this bit to
	 * verify that the controller is in reset." */
	hda_write32(d, HDA_REG_GCTL,
	            hda_read32(d, HDA_REG_GCTL) & ~HDA_GCTL_CRST);
	for (budget = 0; budget < 1000000; budget++) {
		if ((hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) == 0) {
			break;
		}
	}
	if (hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) {
		kprintf("hda: controller will not enter reset\n");
		return -EIO;
	}

	/*
	 * Hold the link in reset.  3.3.7 makes the pulse width our problem:
	 * "Software is responsible for setting/clearing this bit such that
	 * the minimum link RESET# signal assertion pulse width specification
	 * is met."  There was no delay here at all -- CRST was lowered and
	 * raised back to back.  FreeBSD waits 100 us, NetBSD 1 ms.
	 */
	timer_busywait_ms(HDA_RESET_HOLD_MS);

	/* Raise CRST to leave reset, wait for set. */
	hda_write32(d, HDA_REG_GCTL,
	            hda_read32(d, HDA_REG_GCTL) | HDA_GCTL_CRST);
	for (budget = 0; budget < 1000000; budget++) {
		if (hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) {
			break;
		}
	}
	if ((hda_read32(d, HDA_REG_GCTL) & HDA_GCTL_CRST) == 0) {
		kprintf("hda: controller stuck in reset\n");
		return -EIO;
	}

	/*
	 * Give the codecs time to enumerate themselves before STATESTS is
	 * believed.  4.3: "From RESET# de-assertion until codecs requesting
	 * the enumeration can be as late as 25 frames.  The software must
	 * wait at least 521 us (25 frames) after reading CRST as a 1 before
	 * assuming that codecs have all made status change requests and have
	 * been registered by the controller."  Revision 1.0a lists this as an
	 * erratum fix -- earlier text said 250 us.
	 *
	 * What was here was a 1000-iteration `pause` loop, on the order of
	 * tens of microseconds on a modern part: short by more than an order
	 * of magnitude, and the reason codec probing was occasionally coming
	 * up empty.
	 */
	timer_busywait_ms(HDA_CODEC_DISCOVERY_MS);
	return 0;
}

/*
 * Pick a ring size the controller actually implements.
 *
 * 3.3.24 / 3.3.31: bits 7:4 are a capability *bit mask*, not a maximum
 * -- "This is implemented as a bit mask; for example, if the controller
 * supported two entries and 256 entries, this register would have a
 * value of 0101b" -- and "There is no requirement to support more than
 * one CORB Size."  Programming an unsupported value is undefined:
 * "Setting this field to an unsupported size will produce unspecified
 * results."  The field may even be read-only when only one size exists.
 *
 * The driver used to hardcode 256 entries.  That is the common case and
 * happens to be right on every part it has run on, but it was never
 * checked.  Returns the entry count and stores the register encoding.
 */
static unsigned hda_ring_size(hda_dev_t *d, uint32_t reg, uint8_t *enc)
{
	uint8_t cap = (uint8_t)(hda_read8(d, reg) >> 4);

	if (cap & 0x4) {
		*enc = HDA_RBSIZE_256;
		return 256;
	}
	if (cap & 0x2) {
		*enc = HDA_RBSIZE_16;
		return 16;
	}
	if (cap & 0x1) {
		*enc = HDA_RBSIZE_2;
		return 2;
	}
	/*
	 * No capability bits at all.  Some emulated and older controllers
	 * leave the field zero; 256 entries is the near-universal default and
	 * what this driver has always assumed, so keep that behaviour rather
	 * than refusing to attach.
	 */
	*enc = HDA_RBSIZE_256;
	return 256;
}

static int hda_corb_rirb_setup(hda_dev_t *d)
{
	uint32_t budget;
	uint8_t corbsize_enc, rirbsize_enc;

	d->corb_entries = hda_ring_size(d, HDA_REG_CORBSIZE, &corbsize_enc);
	d->rirb_entries = hda_ring_size(d, HDA_REG_RIRBSIZE, &rirbsize_enc);

	d->corb = dma_alloc_coherent(d->corb_entries * sizeof(uint32_t),
	                             &d->corb_phys);
	if (d->corb == NULL) {
		return -ENOMEM;
	}
	d->rirb = dma_alloc_coherent(d->rirb_entries * sizeof(uint64_t),
	                             &d->rirb_phys);
	if (d->rirb == NULL) {
		dma_free_coherent(d->corb, d->corb_entries * sizeof(uint32_t));
		d->corb = NULL;
		return -ENOMEM;
	}

	memset(d->corb, 0, d->corb_entries * sizeof(uint32_t));
	memset(d->rirb, 0, d->rirb_entries * sizeof(uint64_t));

	/* Stop both before reprogramming. */
	hda_write8(d, HDA_REG_CORBCTL, 0);
	hda_write8(d, HDA_REG_RIRBCTL, 0);

	/* Program CORB. */
	hda_write32(d, HDA_REG_CORBLBASE, (uint32_t)d->corb_phys);
	hda_write32(d, HDA_REG_CORBUBASE, 0);
	hda_write8(d,  HDA_REG_CORBSIZE, corbsize_enc);
	hda_write16(d, HDA_REG_CORBWP, 0);

	/*
	 * Reset the CORB read pointer, verifying both halves of the
	 * handshake.  3.3.21: "The hardware will physically update this bit
	 * to 1 when the CORB pointer reset is complete.  Software must read a
	 * 1 to verify that the reset completed correctly.  Software must
	 * clear this bit back to 0, by writing a 0, and then read back the 0
	 * to verify that the clear completed correctly."  Both writes were
	 * being issued back to back with nothing checked in between.
	 *
	 * The engine being stopped first -- also required by 3.3.21, "The
	 * CORB DMA engine must be stopped prior to resetting the Read Pointer
	 * or else DMA transfer may be corrupted" -- was already handled above.
	 *
	 * Not every controller raises the bit: FreeBSD carries a note that
	 * "at least the 82801G doesn't reset the bit to zero", and older spec
	 * text said it always reads as zero.  So the assert half is advisory,
	 * while failing to clear it really does stall the engine.
	 */
	hda_write16(d, HDA_REG_CORBRP, HDA_CORBRP_RST);
	for (budget = 0; budget < HDA_RING_TIMEOUT; budget++) {
		if (hda_read16(d, HDA_REG_CORBRP) & HDA_CORBRP_RST) {
			break;
		}
	}
	hda_write16(d, HDA_REG_CORBRP, 0);
	for (budget = 0; budget < HDA_RING_TIMEOUT; budget++) {
		if ((hda_read16(d, HDA_REG_CORBRP) & HDA_CORBRP_RST) == 0) {
			break;
		}
	}
	if (hda_read16(d, HDA_REG_CORBRP) & HDA_CORBRP_RST) {
		kprintf("hda: CORB read pointer stuck in reset\n");
		return -EIO;
	}
	d->corb_wp = 0;

	/* Program RIRB.  The write pointer reset is write-only and always
	 * reads back 0 (3.3.27), so there is nothing to verify here. */
	hda_write32(d, HDA_REG_RIRBLBASE, (uint32_t)d->rirb_phys);
	hda_write32(d, HDA_REG_RIRBUBASE, 0);
	hda_write8(d,  HDA_REG_RIRBSIZE, rirbsize_enc);
	hda_write16(d, HDA_REG_RIRBWP, HDA_RIRBWP_RST);
	/*
	 * Interrupt after half the ring rather than after every response.
	 *
	 * RINTCNT=1 raised a controller interrupt per verb, which during the
	 * boot-time codec graph walk is hundreds of them, each one taking the
	 * shared INTx line and running the whole handler.  The synchronous
	 * verb path polls RIRBWP and never depended on the interrupt, so this
	 * only removes work.  FreeBSD uses rirb_size / 2.
	 *
	 * 3.3.28: "The DMA engine should be stopped when changing this field
	 * or else an interrupt may be lost" -- it is, RIRBCTL is not started
	 * until below.
	 */
	hda_write16(d, HDA_REG_RINTCNT, (uint16_t)(d->rirb_entries / 2));
	d->rirb_rp = 0;

	/* 3.3.22 on CORBRUN says plainly: "Must read the value back". */
	hda_write8(d, HDA_REG_CORBCTL, HDA_CORBCTL_RUN);
	for (budget = 0; budget < HDA_RING_TIMEOUT; budget++) {
		if (hda_read8(d, HDA_REG_CORBCTL) & HDA_CORBCTL_RUN) {
			break;
		}
	}
	if ((hda_read8(d, HDA_REG_CORBCTL) & HDA_CORBCTL_RUN) == 0) {
		kprintf("hda: CORB engine will not start\n");
		return -EIO;
	}

	/*
	 * RUN + RINTCTL.  The response interrupt is only safe because the IRQ
	 * handler clears RIRBSTS: leaving it unacknowledged latches the
	 * controller-interrupt summary on so the shared level-triggered INTx
	 * never drops -- which wedged the machine inside the interrupt
	 * handler and was why this driver hung the boot outright.
	 */
	hda_write8(d, HDA_REG_RIRBCTL, HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINTCTL);
	for (budget = 0; budget < HDA_RING_TIMEOUT; budget++) {
		if (hda_read8(d, HDA_REG_RIRBCTL) & HDA_RIRBCTL_RUN) {
			break;
		}
	}
	if ((hda_read8(d, HDA_REG_RIRBCTL) & HDA_RIRBCTL_RUN) == 0) {
		kprintf("hda: RIRB engine will not start\n");
		return -EIO;
	}
	return 0;
}

/*
 * Send a verb and synchronously wait for its response.  Stores the
 * response's low 32 bits through *resp and returns 0, or returns -EIO on
 * timeout leaving *resp untouched.  Caller must hold d->verb_lock.
 *
 * A zero response is meaningful -- 7.3.3.7 defines a Get against a
 * non-existent amplifier as returning 00000000h, and plenty of
 * parameters are legitimately zero -- so a timeout cannot be reported by
 * returning 0, which is what this used to do.
 */
static int hda_send_verb_locked(hda_dev_t *d, uint8_t cad, uint8_t nid,
                                uint16_t verb, uint16_t payload,
                                uint32_t *resp)
{
	uint32_t encoded = hda_pack_verb(cad, nid, verb, payload);
	uint16_t wp;
	uint32_t budget;

	wp = (uint16_t)((d->corb_wp + 1) % d->corb_entries);
	d->corb[wp] = encoded;
	/* Publish the CORB entry before ringing the doorbell. */
	__sync_synchronize();
	hda_write16(d, HDA_REG_CORBWP, wp);
	d->corb_wp = wp;

	/*
	 * Wait for the RIRB write pointer to move past our own read pointer,
	 * then consume responses until we find the one addressed to us.
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
		uint64_t entry;
		uint32_t ex;

		if (rwp == d->rirb_rp) {
			continue;
		}
		d->rirb_rp = (uint16_t)((d->rirb_rp + 1) % d->rirb_entries);
		/* The RIRBWP read above orders the DMA'd entry against us. */
		__sync_synchronize();
		entry = d->rirb[d->rirb_rp];
		ex = (uint32_t)(entry >> 32);

		/*
		 * Response Extended: bits 3:0 are the responding codec, bit 4
		 * marks an unsolicited response (spec 4.4.2).  Neither was
		 * being looked at, so an unsolicited event -- a jack sense or
		 * a docking change, which the codec may inject in any frame
		 * where a solicited response is not present -- would be handed
		 * back as the answer to whatever verb was outstanding.
		 * Discard it and keep waiting, like NetBSD's rirb_dequeue().
		 */
		if (ex & HDA_RIRB_EX_UNSOL) {
			continue;
		}
		if ((ex & HDA_RIRB_EX_CODEC_MASK) != (uint32_t)(cad & 0x0F)) {
			continue;   /* another codec's answer */
		}
		*resp = (uint32_t)entry;
		return 0;
	}
	d->verb_timeouts++;
	return -EIO;
}

/*
 * Serialised wrapper.  hda_send_verb() mutates corb_wp / rirb_rp and
 * rings the controller's doorbell, none of which is safe to do from two
 * threads at once -- and it is reachable concurrently, since
 * hda_set_params() binds the converter through the same path that the
 * boot-time graph walk uses.  NetBSD takes sc_corb_mtx around every
 * command for the same reason.
 *
 * Returns the response, or 0 if the verb timed out.  Callers that need
 * to tell those apart use hda_try_verb().
 */
static int hda_try_verb(hda_dev_t *d, uint8_t cad, uint8_t nid,
                        uint16_t verb, uint16_t payload, uint32_t *resp)
{
	unsigned long flags = spinlock_acquire_irq(&d->verb_lock);
	int rc = hda_send_verb_locked(d, cad, nid, verb, payload, resp);

	spinlock_release_irq(&d->verb_lock, flags);
	return rc;
}

static uint32_t hda_send_verb(hda_dev_t *d, uint8_t cad, uint8_t nid,
                              uint16_t verb, uint16_t payload)
{
	uint32_t resp = 0;

	(void)hda_try_verb(d, cad, nid, verb, payload, &resp);
	return resp;
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
 * Amplifier capabilities for `nid`, whose AUDIO_WIDGET_CAPS are `wcaps`.
 *
 * Spec 7.3.4.6: a widget only carries its own amp parameters when Amp
 * Param Override is set.  "If this bit is a 0, then the Audio Function
 * node must contain default amplifier parameters, and they should be
 * used to define all amplifier parameters (both input and output) in
 * this widget."
 *
 * Querying the widget unconditionally -- which is what this used to do
 * -- returns 0 on most real codecs, and an all-zero AMPCAP reads as
 * offset 0, num steps 0.  QEMU's codec happens to answer per widget, so
 * emulation never showed it.
 */
static uint32_t hda_amp_caps(hda_dev_t *d, uint8_t nid, uint32_t wcaps,
                             int output)
{
	if (wcaps & HDA_AW_AMP_OVERRIDE) {
		return hda_get_param(d, nid, output ?
		                     HDA_PARAM_OUTPUT_AMP_CAPS :
		                     HDA_PARAM_INPUT_AMP_CAPS);
	}
	return output ? d->afg_outamp_caps : d->afg_inamp_caps;
}

/*
 * Unmute a widget's output amp and park it on its 0 dB step.
 *
 * The mute bit is the thing that actually has to change here: spec
 * 7.3.3.7 says "generally, mute should default to 1 on codec reset",
 * while the gain "must default to the Offset value, meaning that all
 * amplifiers, by default, are configured to 0 dB gain".  So Offset is
 * the correct gain, not a starting point to be second-guessed -- the
 * old code substituted a fallback whenever Offset read 0, which on a
 * codec where step 0 genuinely is 0 dB drove the amp to maximum instead.
 *
 * Offset is clamped to NumSteps because "if a value outside the
 * amplifier's range is set, the results are undetermined".
 */
static void hda_amp_unmute_out(hda_dev_t *d, uint8_t nid, uint32_t wcaps)
{
	uint32_t caps = hda_amp_caps(d, nid, wcaps, 1);
	uint8_t gain = (uint8_t)HDA_AMPCAP_OFFSET(caps);
	uint8_t steps = (uint8_t)HDA_AMPCAP_NUMSTEPS(caps);

	if (gain > steps) {
		gain = steps;
	}
	/* Mute bit deliberately left clear. */
	hda_send_verb(d, d->codec_addr, nid, HDA_VERB_SET_AMP_GAIN_MUTE,
	              (uint16_t)(HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT |
	                         HDA_AMP_SET_RIGHT |
	                         (gain & HDA_AMP_GAIN_MASK)));
}

/*
 * Unmute one *input* of a widget -- the amp on the connection at
 * `index`, not the widget's single output amp.
 *
 * Spec 7.3.3.7: "Index is only used when programming the input
 * amplifiers on Selector Widgets and Sum Widgets, where each input may
 * have an individual amplifier.  The index corresponds to the input's
 * offset in the Connection List."
 *
 * A mixer sums every input it has, so selecting a source is not enough:
 * the input carrying our DAC has to be unmuted explicitly, and like
 * every other amp it comes up muted.  Nothing in this driver touched
 * input amps at all, which is the textbook "the graph is routed
 * correctly and there is still no sound" case on the Realtek and
 * Conexant codecs that interpose a mixer between DAC and pin.
 *
 * Setting an amp that does not exist is defined as a no-op ("Any attempt
 * to set a non-existent amplifier is ignored"), so a widget whose input
 * amp is really a capture-path amp loses nothing by this.
 */
static void hda_amp_unmute_in(hda_dev_t *d, uint8_t nid, uint32_t wcaps,
                              uint8_t index)
{
	uint32_t caps = hda_amp_caps(d, nid, wcaps, 0);
	uint8_t gain = (uint8_t)HDA_AMPCAP_OFFSET(caps);
	uint8_t steps = (uint8_t)HDA_AMPCAP_NUMSTEPS(caps);

	if (gain > steps) {
		gain = steps;
	}
	/* Mute bit deliberately left clear. */
	hda_send_verb(d, d->codec_addr, nid, HDA_VERB_SET_AMP_GAIN_MUTE,
	              (uint16_t)(HDA_AMP_SET_INPUT | HDA_AMP_SET_LEFT |
	                         HDA_AMP_SET_RIGHT |
	                         ((uint16_t)index << HDA_AMP_SET_INDEX_SHIFT) |
	                         (gain & HDA_AMP_GAIN_MASK)));
}

/*
 * Read `nid`'s connection list into `conns`, expanding ranges, and return
 * how many entries were stored.  That count is the index space that
 * SET_CONNECTION_SELECT and the amp Index field address.
 *
 * Entry width and the range flag both depend on the list's form (spec
 * figure 51): a short-form entry is 8 bits, range indicator at bit 7 over
 * a 7-bit NID; a long-form entry is 16 bits, flag at bit 15 over a 15-bit
 * NID.  One response carries four short entries or two long ones, and the
 * requested index has to land on that boundary -- 7.3.3.3: "n must be a
 * multiple of four" short-form, "n must be even" long-form.
 *
 * A set range indicator means this entry and the previous one bound a
 * continuous run of NIDs: "if the range bit were set on the third list
 * entry, then the second and third entries form a range".  The NIDs in
 * between occupy real connection indices.
 *
 * All of which the previous code got wrong in one line.  It masked every
 * entry -- long form included -- to 8 bits and compared the raw value
 * with the range flag still in it.  A long-form entry lost its high byte
 * outright, so 0x8005 compared equal to NID 5; a short-form range entry
 * could never match anything; and because ranges were not expanded, any
 * widget with a range ahead of the match reported an index short of the
 * real one, which then selected the wrong source and unmuted the wrong
 * input amp.
 */
#define HDA_MAX_CONNS 32

static int hda_conn_list(hda_dev_t *d, uint8_t nid, uint8_t *conns, int max)
{
	uint32_t lenr = hda_get_param(d, nid, HDA_PARAM_CONN_LIST_LEN);
	int len = (int)HDA_CONNLIST_LEN(lenr);
	int is_long = (lenr & HDA_CONNLIST_LONG) != 0;
	int per = is_long ? 2 : 4;
	uint16_t nmask = is_long ? 0x7FFF : 0x7F;
	uint16_t rmask = is_long ? 0x8000 : 0x80;
	uint16_t prev = 0;
	int n = 0;
	int i;

	for (i = 0; i < len && n < max; i += per) {
		uint32_t resp = hda_send_verb(d, d->codec_addr, nid,
		                              HDA_VERB_GET_CONN_LIST,
		                              (uint16_t)i);
		int j;

		for (j = 0; j < per && (i + j) < len && n < max; j++) {
			int shift = j * (is_long ? 16 : 8);
			uint16_t raw = (uint16_t)((resp >> shift) &
			                          (is_long ? 0xFFFFU : 0xFFU));
			uint16_t cnid = raw & nmask;
			uint16_t first;

			/* "the number of entries beyond the end of the list
			 * would be reported as 0's" */
			if (cnid == 0 || cnid > 0xFF) {
				continue;
			}
			if ((raw & rmask) == 0 || prev == 0 || prev >= cnid) {
				/* Plain entry, or a range the codec described
				 * backwards -- take it as a single NID. */
				first = cnid;
			} else {
				first = (uint16_t)(prev + 1);
			}
			while (first <= cnid && n < max) {
				conns[n++] = (uint8_t)first;
				first++;
			}
			prev = cnid;
		}
	}
	return n;
}

/* Offset of `target` in an already-read connection list, or -1. */
static int hda_conn_find(const uint8_t *conns, int n, uint8_t target)
{
	int i;

	for (i = 0; i < n; i++) {
		if (conns[i] == target) {
			return i;
		}
	}
	return -1;
}

/*
 * Select `idx` as `nid`'s input, unless the widget has nothing to select
 * between.  Spec 7.3.4.11: "If Connection List Length is 1, there is only
 * one hard-wired input possible, which is read from the Connection List,
 * and there is no Connection Select Control."  Sending the verb anyway
 * pokes a control the widget does not implement.
 */
static void hda_conn_select(hda_dev_t *d, uint8_t nid, int nconns, int idx)
{
	if (nconns > 1) {
		hda_send_verb(d, d->codec_addr, nid, HDA_VERB_SET_CONN_SELECT,
		              (uint16_t)idx);
	}
}

/*
 * Point `pin` at `dac`, directly if the pin lists it, otherwise through one
 * intermediate mixer/selector.  Depth two covers the real topologies: QEMU
 * wires pin straight to DAC, while physical codecs usually interpose a
 * mixer.  Mixers take every input at once and need no selection, so only a
 * selector's index is actually committed.
 *
 * With `commit` clear this only reports whether the path exists, touching
 * nothing.  Pin and DAC used to be chosen independently and the pairing
 * discovered afterwards -- if it did not hold, the driver had already
 * committed to both and simply shrugged ("assuming hard-wired").  The
 * caller now probes candidates first and only programs a pair it knows
 * connects.
 */
static int hda_route_pin_to_dac(hda_dev_t *d, uint8_t pin, uint8_t dac,
                                int commit)
{
	uint32_t pincaps = hda_get_param(d, pin, HDA_PARAM_AUDIO_WIDGET_CAPS);
	uint8_t pconns[HDA_MAX_CONNS];
	int pn;
	int i;
	int idx;

	pn = hda_conn_list(d, pin, pconns, HDA_MAX_CONNS);

	idx = hda_conn_find(pconns, pn, dac);
	if (idx >= 0) {
		if (!commit) {
			return 0;
		}
		hda_conn_select(d, pin, pn, idx);
		if (pincaps & HDA_AW_IN_AMP) {
			hda_amp_unmute_in(d, pin, pincaps, (uint8_t)idx);
		}
		return 0;
	}

	for (i = 0; i < pn; i++) {
		uint8_t mid = pconns[i];
		uint8_t mconns[HDA_MAX_CONNS];
		uint32_t caps;
		int type, mn, midx;

		if (mid == 0) {
			continue;
		}
		caps = hda_get_param(d, mid, HDA_PARAM_AUDIO_WIDGET_CAPS);
		type = HDA_AW_TYPE(caps);
		if (type != HDA_AW_TYPE_MIXER && type != HDA_AW_TYPE_SELECTOR) {
			continue;
		}
		mn = hda_conn_list(d, mid, mconns, HDA_MAX_CONNS);
		midx = hda_conn_find(mconns, mn, dac);
		if (midx < 0) {
			continue;
		}
		if (!commit) {
			return 0;
		}
		/* A mixer sums everything it has and offers no selection; only
		 * a selector picks one input. */
		if (type == HDA_AW_TYPE_SELECTOR) {
			hda_conn_select(d, mid, mn, midx);
		}
		/* Open the intermediate's input for the DAC, then its output. */
		if (caps & HDA_AW_IN_AMP) {
			hda_amp_unmute_in(d, mid, caps, (uint8_t)midx);
		}
		if (caps & HDA_AW_OUT_AMP) {
			hda_amp_unmute_out(d, mid, caps);
		}
		hda_conn_select(d, pin, pn, i);
		if (pincaps & HDA_AW_IN_AMP) {
			hda_amp_unmute_in(d, pin, pincaps, (uint8_t)i);
		}
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

/*
 * Does the chosen converter actually support this format?
 *
 * SDnFMT being able to express a rate says nothing about the codec being
 * able to render it.  Nothing consulted the converter's capabilities at
 * all, so an unsupported rate was programmed and simply came out wrong.
 *
 * The capability bits (7.3.4.7 table 139) only cover the eleven standard
 * rates, so a rate outside that set cannot be advertised and is refused
 * here even though hda_encode_format() can encode it -- the encoder
 * describes the register, this describes the hardware.
 */
static int hda_codec_supports(hda_dev_t *d, uint32_t rate, uint32_t bits,
                              uint32_t channels)
{
	static const struct { uint32_t rate; uint8_t bit; } ratebits[] = {
		{   8000,  0 }, {  11025,  1 }, {  16000,  2 }, {  22050,  3 },
		{  32000,  4 }, {  44100,  5 }, {  48000,  6 }, {  88200,  7 },
		{  96000,  8 }, { 176400,  9 }, { 192000, 10 },
	};
	uint32_t sizebit;
	size_t i;

	/* Nothing reported: the codec did not answer, so do not second-guess
	 * a format the encoder already accepted. */
	if (d->dac_pcm_caps == 0) {
		return 0;
	}

	switch (bits) {
	case 8:  sizebit = HDA_PCM_SIZE_8;  break;
	case 16: sizebit = HDA_PCM_SIZE_16; break;
	case 20: sizebit = HDA_PCM_SIZE_20; break;
	case 24: sizebit = HDA_PCM_SIZE_24; break;
	case 32: sizebit = HDA_PCM_SIZE_32; break;
	default: return -EINVAL;
	}
	if ((d->dac_pcm_caps & sizebit) == 0) {
		return -EINVAL;
	}

	for (i = 0; i < sizeof(ratebits) / sizeof(ratebits[0]); i++) {
		if (ratebits[i].rate != rate) {
			continue;
		}
		if ((d->dac_pcm_caps &
		     HDA_PCM_RATE_BIT(ratebits[i].bit)) == 0) {
			return -EINVAL;
		}
		break;
	}
	if (i == sizeof(ratebits) / sizeof(ratebits[0])) {
		return -EINVAL;   /* no capability bit exists for this rate */
	}

	/* Channel count is capped by the converter, not by SDnFMT's 4-bit
	 * field (7.3.4.6). */
	if (d->dac_max_chan != 0 && channels > d->dac_max_chan) {
		return -EINVAL;
	}
	if (d->dac_fmt_caps != 0 &&
	    (d->dac_fmt_caps & HDA_STREAM_FMT_PCM) == 0) {
		return -EINVAL;   /* converter does not do linear PCM */
	}
	return 0;
}

static int hda_configure_codec(hda_dev_t *d)
{
	uint32_t sub;
	uint8_t start, count, i;
	uint8_t afg = 0;
	uint8_t dac = 0;
	uint8_t pin = 0;
	uint8_t dacs[HDA_MAX_CANDIDATES];
	uint8_t pins[HDA_MAX_CANDIDATES];
	int pin_rank[HDA_MAX_CANDIDATES];
	int ndacs = 0, npins = 0;

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

	/*
	 * The AFG's amp capabilities are the defaults for every widget that
	 * does not set Amp Param Override, which on real codecs is most of
	 * them.  Cache them before walking the widgets.
	 */
	d->afg_outamp_caps = hda_get_param(d, afg, HDA_PARAM_OUTPUT_AMP_CAPS);
	d->afg_inamp_caps  = hda_get_param(d, afg, HDA_PARAM_INPUT_AMP_CAPS);

	/*
	 * The AFG's PCM and stream-format capabilities stand in for any
	 * converter that does not set Format Override, the same way its amp
	 * caps do (7.3.4.7 / 7.3.4.8: "Audio Function Group (as default for
	 * all widgets within the Audio Function)").
	 */
	d->afg_pcm_caps = hda_get_param(d, afg, HDA_PARAM_SUPPORTED_RATES);
	d->afg_fmt_caps = hda_get_param(d, afg, HDA_PARAM_SUPPORTED_FORMATS);

	/*
	 * Collect the candidates.  This used to take the numerically first
	 * DAC and, independently, the best-ranked pin, then discover
	 * afterwards whether the two were connected -- and shrug if they were
	 * not.  Gather both lists instead and pair them by actual
	 * reachability, best pin first.
	 */
	sub = hda_get_param(d, afg, HDA_PARAM_SUBNODE_COUNT);
	start = (uint8_t)HDA_SUBNODE_START(sub);
	count = (uint8_t)HDA_SUBNODE_COUNT(sub);
	for (i = 0; i < count; i++) {
		uint8_t nid = (uint8_t)(start + i);
		uint32_t caps = hda_get_param(d, nid,
		                              HDA_PARAM_AUDIO_WIDGET_CAPS);

		/*
		 * Skip digital widgets.  An HDMI/DisplayPort path needs the
		 * digital converter control, channel mapping and ELD handling
		 * this driver has none of, so adopting one would produce a
		 * device that looks configured and stays silent.  Analog only,
		 * and say so if that leaves nothing.
		 */
		if (caps & HDA_AW_DIGITAL) {
			continue;
		}

		switch (HDA_AW_TYPE(caps)) {
		case HDA_AW_TYPE_DAC:
			if (ndacs < HDA_MAX_CANDIDATES) {
				dacs[ndacs++] = nid;
			}
			break;
		case HDA_AW_TYPE_PIN: {
			uint32_t pcaps = hda_get_param(d, nid,
			                               HDA_PARAM_PIN_CAPS);
			uint32_t cfg;
			int rank, j;

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
			if (npins >= HDA_MAX_CANDIDATES) {
				break;
			}
			/* Insertion sort, best rank first. */
			for (j = npins; j > 0 && pin_rank[j - 1] < rank; j--) {
				pins[j] = pins[j - 1];
				pin_rank[j] = pin_rank[j - 1];
			}
			pins[j] = nid;
			pin_rank[j] = rank;
			npins++;
			break;
		}
		default:
			break;
		}
	}

	if (ndacs == 0 || npins == 0) {
		kprintf("hda: no analog output widgets (dacs=%d pins=%d)\n",
		        ndacs, npins);
		return -ENODEV;
	}

	/* Best-ranked pin that some DAC can actually reach. */
	for (i = 0; i < (uint8_t)npins && pin == 0; i++) {
		int j;

		for (j = 0; j < ndacs; j++) {
			if (hda_route_pin_to_dac(d, pins[i], dacs[j], 0) == 0) {
				pin = pins[i];
				dac = dacs[j];
				break;
			}
		}
	}
	if (pin == 0) {
		/*
		 * Nothing reachable within depth two.  A pin with a single
		 * hard-wired source and no connection list at all still works,
		 * so fall back to the best pin and first DAC rather than
		 * refusing outright -- but say that is what happened.
		 */
		pin = pins[0];
		dac = dacs[0];
		kprintf("hda: no explicit route to any pin; assuming pin %u "
		        "is hard-wired to dac %u\n", pin, dac);
	}

	d->dac_nid = dac;
	d->pin_nid = pin;

	hda_send_verb(d, d->codec_addr, dac, HDA_VERB_SET_POWER_STATE,
	              HDA_PS_D0);
	hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_POWER_STATE,
	              HDA_PS_D0);

	(void)hda_route_pin_to_dac(d, pin, dac, 1);

	/*
	 * Remember what the converter can actually do, so set_params can
	 * refuse a format the codec would silently mis-render.  Per 7.3.4.6 a
	 * widget's own rate/format parameters only apply when Format Override
	 * is set; otherwise the AFG's defaults do.
	 */
	{
		uint32_t dcaps = hda_get_param(d, dac,
		                               HDA_PARAM_AUDIO_WIDGET_CAPS);
		uint32_t pcm = 0, fmts = 0;

		if (dcaps & HDA_AW_FORMAT_OVERRIDE) {
			pcm = hda_get_param(d, dac, HDA_PARAM_SUPPORTED_RATES);
			fmts = hda_get_param(d, dac,
			                     HDA_PARAM_SUPPORTED_FORMATS);
		}
		d->dac_pcm_caps = pcm ? pcm : d->afg_pcm_caps;
		d->dac_fmt_caps = fmts ? fmts : d->afg_fmt_caps;
		/* Channel count is split across bits 15:13 and bit 0, and is
		 * the maximum minus one (7.3.4.6). */
		d->dac_max_chan = (uint8_t)HDA_AW_CHAN_COUNT(dcaps);
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
	 * usually silent without it.  BTL (bit 0) and L-R swap (bit 2) share
	 * this byte with the EAPD bit, so assigning 0x02 outright cleared
	 * whatever the firmware had configured for them. */
	if (hda_get_param(d, pin, HDA_PARAM_PIN_CAPS) & HDA_PINCAP_EAPD) {
		uint32_t eapd = hda_send_verb(d, d->codec_addr, pin,
		                              HDA_VERB_GET_EAPD_BTL, 0);

		eapd &= HDA_EAPD_MASK;
		eapd |= HDA_EAPD_ENABLE;
		hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_EAPD_BTL,
		              (uint16_t)eapd);
	}

	{
		uint32_t dcaps = hda_get_param(d, dac,
		                               HDA_PARAM_AUDIO_WIDGET_CAPS);
		uint32_t pcaps = hda_get_param(d, pin,
		                               HDA_PARAM_AUDIO_WIDGET_CAPS);

		if (dcaps & HDA_AW_OUT_AMP) {
			hda_amp_unmute_out(d, dac, dcaps);
		}
		if (pcaps & HDA_AW_OUT_AMP) {
			hda_amp_unmute_out(d, pin, pcaps);
		}
	}

	d->have_path = 1;
	hda_codec_bind_stream(d, d->fmt);

	kprintf("hda: codec %u afg=%u dac=%u pin=%u tag=%u chan=%u "
	        "pcm=0x%08x\n",
	        d->codec_addr, afg, dac, pin, d->stream_tag,
	        d->dac_max_chan, d->dac_pcm_caps);
	return 0;
}

/*
 * Try every codec the controller reported, not just the lowest-numbered
 * one.  A machine with an analog codec alongside an Intel HDMI codec can
 * present either first, and stopping at the first one meant a whole
 * working analog path could go unused because address 0 happened to be
 * the display audio.
 */
static int hda_codec_configure(hda_dev_t *d)
{
	int i;

	for (i = 0; i < HDA_MAX_CODECS; i++) {
		if ((d->codec_mask & (1u << i)) == 0) {
			continue;
		}
		d->codec_addr = (uint8_t)i;
		if (hda_configure_codec(d) == 0) {
			return 0;
		}
		kprintf("hda: codec %d has no usable output path\n", i);
	}
	return -ENODEV;
}

/* ------------------------------------------------------------------- */
/* Stream engine state machine                                         */
/* ------------------------------------------------------------------- */

/*
 * Clear RUN and wait for the engine to idle.
 *
 * Stopping is not instantaneous and the driver used to assume it was.
 * Spec 4.5.4: "The RUN bit will not immediately transition to a 0.
 * Rather, the DMA engine will continue receiving or transmitting data
 * normally for the rest of the current frame but will stop ... at the
 * beginning of the next frame.  When the DMA transfer has stopped and
 * the hardware has idled, the RUN bit will then be read as 0.  The run
 * bit should transition from a 1 to a 0 within 40 us."  And 4.5.5 makes
 * the readback mandatory before restarting: "the RUN bit must be checked
 * to make sure that it has transition[ed] back to a 0 to indicate that
 * the hardware is ready to restart."
 *
 * Returns 0 once idle, -EIO if it never idled.  Caller holds feed_lock.
 */
static int hda_stream_stop(hda_dev_t *d)
{
	uint8_t ctl = hda_read8(d, d->sd_base + HDA_SD_CTL);
	int budget;

	d->running = 0;
	ctl &= (uint8_t)~(HDA_SDCTL_RUN | HDA_SDCTL_IOCE | HDA_SDCTL_FEIE |
	                  HDA_SDCTL_DEIE);
	hda_write8(d, d->sd_base + HDA_SD_CTL, ctl);

	for (budget = 0; budget < HDA_STREAM_TIMEOUT; budget++) {
		if ((hda_read8(d, d->sd_base + HDA_SD_CTL) &
		     HDA_SDCTL_RUN) == 0) {
			return 0;
		}
	}
	kprintf("hda: stream did not stop (RUN stuck)\n");
	return -EIO;
}

/*
 * Put the stream descriptor through a reset.
 *
 * Spec 3.3.35: "Writing a 1 causes the corresponding stream to be reset
 * ... After the stream hardware has completed sequencing into the reset
 * state, it will report a 1 in this bit.  Software must read a 1 from
 * this bit to verify that the stream is in reset.  Writing a 0 causes
 * the corresponding stream to exit reset. ... Software must read a 0
 * from this bit before accessing any of the stream registers.  The RUN
 * bit must be cleared before SRST is asserted."
 *
 * Both polls used to run their budget and discard the result, so a
 * descriptor that never acknowledged reset was treated as if it had.
 * Caller holds feed_lock.
 */
static int hda_stream_reset(hda_dev_t *d)
{
	int budget;

	(void)hda_stream_stop(d);   /* RUN must be clear before SRST */

	hda_write8(d, d->sd_base + HDA_SD_CTL, HDA_SDCTL_SRST);
	for (budget = 0; budget < HDA_STREAM_TIMEOUT; budget++) {
		if (hda_read8(d, d->sd_base + HDA_SD_CTL) & HDA_SDCTL_SRST) {
			break;
		}
	}
	if ((hda_read8(d, d->sd_base + HDA_SD_CTL) & HDA_SDCTL_SRST) == 0) {
		kprintf("hda: stream reset never asserted\n");
		return -EIO;
	}

	hda_write8(d, d->sd_base + HDA_SD_CTL, 0);
	for (budget = 0; budget < HDA_STREAM_TIMEOUT; budget++) {
		if ((hda_read8(d, d->sd_base + HDA_SD_CTL) &
		     HDA_SDCTL_SRST) == 0) {
			return 0;
		}
	}
	kprintf("hda: stream stuck in reset\n");
	return -EIO;
}

/*
 * Reset, reprogram the descriptor, and start the engine.
 *
 * The descriptor has to be (re)written after the reset and before RUN,
 * never while running.  Spec 3.3.38 on CBL: "Software may only write to
 * this register after Global Reset, Controller Reset, or Stream Reset
 * has occurred.  Once the RUN bit has been set to enable the engine,
 * software must not write to this register until after the next reset is
 * asserted, or undefined events will occur."  The restart path used to
 * rewrite BDPL/LVI/CBL with the engine merely stopped, not reset.
 *
 * The reset also means the DMA resumes at BDL entry 0, which is why the
 * caller stages from slot 0 on a cold start -- otherwise the first
 * buffers play in the wrong order.
 *
 * Caller holds feed_lock.
 */
static int hda_stream_start(hda_dev_t *d)
{
	uint8_t ctl2;
	int rc;

	rc = hda_stream_reset(d);
	if (rc != 0) {
		return rc;
	}

	hda_write32(d, d->sd_base + HDA_SD_BDPL, (uint32_t)d->bdl_phys);
	hda_write32(d, d->sd_base + HDA_SD_BDPU, 0);
	hda_write16(d, d->sd_base + HDA_SD_LVI, HDA_BDL_ENTRIES - 1);
	hda_write32(d, d->sd_base + HDA_SD_CBL,
	            (uint32_t)(HDA_BDL_ENTRIES * HDA_CHUNK_BYTES));
	hda_write16(d, d->sd_base + HDA_SD_FMT, d->fmt);

	/* Stream tag, in the third control byte.  Leave DIR / stripe / TP
	 * as the reset left them. */
	ctl2 = hda_read8(d, d->sd_base + HDA_SD_CTL2);
	ctl2 = (uint8_t)((ctl2 & (uint8_t)~HDA_SDCTL2_STRM_MASK) |
	                 (uint8_t)(d->stream_tag << HDA_SDCTL2_STRM_SHIFT));
	hda_write8(d, d->sd_base + HDA_SD_CTL2, ctl2);

	/* Drop anything latched from the previous run before arming. */
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);
	/*
	 * IOCE for buffer completions, plus FEIE and DEIE so FIFO and
	 * descriptor errors are actually reported.  Both were defined and
	 * never enabled, and the handler acknowledged their status bits in
	 * silence -- so a descriptor error, which 3.3.36 says "is treated as
	 * a fatal stream error as the stream cannot continue running.  The
	 * RUN bit will be cleared and the stream will stop", looked from
	 * userland like playback mysteriously stopping.  FreeBSD arms all
	 * three.
	 */
	hda_write8(d, d->sd_base + HDA_SD_CTL,
	           HDA_SDCTL_RUN | HDA_SDCTL_IOCE | HDA_SDCTL_FEIE |
	           HDA_SDCTL_DEIE);
	d->running = 1;
	d->halt_pending = 0;
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
/*
 * Stage PCM from the software FIFO into free ring slots.  Caller must hold
 * d->feed_lock (IRQ-safe).
 *
 * Only ever queues a FULL slot unless flush_tail says otherwise.  Every BDL
 * entry is a fixed HDA_CHUNK_BYTES and the controller plays all of it, so
 * topping up a short slot with zeros splices silence into the middle of the
 * stream.  Writers hand us whatever size they like -- a player doing 2 KiB
 * writes against 4 KiB slots produced a stream that was half silence, one
 * gap every slot, which at 44.1 kHz is a ~43 Hz chop: audibly a cyclic
 * guttural stutter, and it halves the apparent pitch.  Waiting for a full
 * chunk costs at most one slot of latency and keeps the audio contiguous.
 *
 * Genuine underrun is handled elsewhere: the completion interrupt zeroes the
 * slot it just drained, so a starved ring plays silence rather than looping
 * over stale audio.
 */
static void hda_feed(hda_dev_t *d, int flush_tail)
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

		/*
		 * Leave one slot between the producer and the engine.  An
		 * HDA stream is cyclic and never stops while RUN is set, so
		 * the controller is always somewhere in the ring; filling all
		 * HDA_BDL_ENTRIES of them means next_idx wraps onto the slot
		 * being DMA'd right now and overwrites it mid-fetch.  The
		 * softc's own back-pressure comment said BDL_ENTRIES - 1 all
		 * along, and ac97_feed() uses that bound -- AC'97 just
		 * happens to be LVI-bounded, so it stops at the end of the
		 * queue instead of lapping it.
		 */
		if (in_flight >= (HDA_BDL_ENTRIES - 1)) {
			break;
		}
		avail = audio_fifo_used(&d->fifo);
		if (avail == 0) {
			break;
		}
		if (avail < HDA_CHUNK_BYTES && !flush_tail) {
			break;   /* wait for a whole slot's worth */
		}
		copy_len = (avail > HDA_CHUNK_BYTES) ? HDA_CHUNK_BYTES : avail;

		slot = d->next_idx;
		(void)audio_fifo_read(&d->fifo, (uint8_t *)d->chunk[slot],
		                      copy_len);
		/* Only reachable on the drain path, where the padding is the
		 * genuine end of the stream rather than a mid-stream gap. */
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

	/*
	 * Retire a halt the completion handler asked for.  This is the only
	 * place the engine is stopped on the playback path, and it runs in
	 * process context, which is what spec 4.5.6 wants: "The ISR should
	 * not attempt to write to the stream Control register, as there may
	 * be synchronization issues between the ISR and the non-ISR code
	 * both trying to perform Read-Modify-Write cycles on the register."
	 *
	 * Everything really has drained by now, so the ring can go back to a
	 * known position: the next start resets the descriptor, and the DMA
	 * resumes at BDL entry 0 -- staging anywhere else would play the
	 * first buffers out of order.
	 */
	if (d->halt_pending) {
		if (d->running) {
			(void)hda_stream_stop(d);
		}
		d->halt_pending = 0;
		d->next_idx      = 0;
		d->writes_queued = 0;
		d->slots_played  = 0;
	}

	/*
	 * Always feed, running or not.  This used to sit inside the
	 * !running branch, so once the stream started the producer never
	 * staged another buffer and the ring could only be refilled from the
	 * completion interrupt -- one missed IOC and playback deadlocks
	 * permanently with the software FIFO full and the ring starved.
	 */
	hda_feed(d, 0);

	if (!d->running) {
		uint32_t in_flight;
		in_flight = d->writes_queued -
		            __atomic_load_n(&d->slots_played, __ATOMIC_ACQUIRE);
		if (in_flight >= HDA_PREBUFFER_SLOTS ||
		    (in_flight > 0 && audio_fifo_used(&d->fifo) == 0)) {
			if (hda_stream_start(d) != 0) {
				kprintf("hda: failed to start output stream\n");
			}
		}
	}

	spinlock_release_irq(&d->feed_lock, flags);
}

/* ------------------------------------------------------------------- */
/* IRQ                                                                 */
/* ------------------------------------------------------------------- */

/* One pass over a nonzero INTSTS.  Every acknowledgement here goes to the
 * source register, never to INTSTS itself, which is read-only. */
static void hda_one_intr(hda_dev_t *d, uint32_t status)
{
	uint8_t  sdsts;

	/*
	 * Controller interrupt: a codec state change (STATESTS).  This MUST be
	 * acknowledged at the source.  INTSTS bit 30 is only a summary of it,
	 * so clearing nothing else leaves STATESTS set, the summary re-asserts
	 * immediately, and because PCI INTx is level triggered the line never
	 * drops -- the handler is re-entered forever and the machine wedges
	 * inside it.  STATESTS is RW1C: write the bits back to clear.
	 *
	 * It fires the instant CIE is armed, because reset leaves the
	 * codec-present bit latched, so this is not a rare path -- it is the
	 * first interrupt the controller ever raises.
	 */
	if (status & HDA_INTSTS_CIS) {
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
			hda_feed(d, 0);
			/*
			 * A cyclic stream never stops on its own: with RUN set
			 * the controller keeps walking the ring and raising a
			 * completion per slot forever.  Once nothing is
			 * outstanding it has to be halted, or a 2 s clip plays
			 * for as long as the machine is up.
			 *
			 * But not from here, and not immediately.
			 *
			 * Not from here, because 4.5.6 says "The ISR should not
			 * attempt to write to the stream Control register, as
			 * there may be synchronization issues between the ISR
			 * and the non-ISR code both trying to perform
			 * Read-Modify-Write cycles on the register."  Flag it
			 * and let hda_kick() / hda_drain() / hda_close() do the
			 * stop in process context.
			 *
			 * Not immediately, because BCIS does not mean the audio
			 * was played.  3.3.36: for an output engine the bit is
			 * set "after the last byte of data for the current
			 * descriptor has been fetched from memory and put into
			 * the DMA FIFO" -- so at the final completion there is
			 * still up to a FIFO's worth of real audio inside the
			 * controller that has not reached the codec.  Cutting
			 * RUN right here chopped that off the end of every
			 * clip.  Deferring the stop lets the ring cycle a
			 * little longer; the slots were zeroed above, so what
			 * follows the tail is silence rather than stale audio.
			 *
			 * Tested signed and <=, not ==: BCIS is a single status
			 * bit, so two buffers completing before the handler
			 * runs coalesce into one interrupt and slots_played
			 * permanently lags the ring.  An equality test would
			 * never land -- the counters cross instead of meeting.
			 * The FIFO check keeps this from flagging a stream that
			 * still has data staged but not yet in the ring.
			 */
			if (audio_fifo_used(&d->fifo) == 0 &&
			    (int32_t)(d->writes_queued -
			              __atomic_load_n(&d->slots_played,
			                              __ATOMIC_ACQUIRE)) <= 0) {
				/*
				 * Park the counters level.  Completions keep
				 * arriving until the deferred stop lands, and
				 * hda_feed() derives in_flight as an unsigned
				 * difference -- letting slots_played run past
				 * writes_queued would wrap it to ~4 billion and
				 * wedge the feeder permanently.
				 */
				__atomic_store_n(&d->slots_played,
				                 d->writes_queued,
				                 __ATOMIC_RELEASE);
				d->halt_pending = 1;
			}
			spinlock_release_irq(&d->feed_lock, f);
			(void)sleepq_wake_all(d);
		}
		/*
		 * Stream errors used to be acknowledged without a word.  A
		 * FIFO underrun means the feeder fell behind; a descriptor
		 * error is fatal -- 3.3.36: "This error is treated as a fatal
		 * stream error as the stream cannot continue running.  The RUN
		 * bit will be cleared and the stream will stop.  Software may
		 * attempt to restart the stream engine after addressing the
		 * cause of the error" -- and from userland that looked like
		 * playback simply stopping for no reason.
		 *
		 * Counted rather than printed per event: these arrive from
		 * interrupt context and an underrun storm would bury the
		 * console.  DESE additionally forces a restart through the
		 * normal halt path, since the hardware has already dropped RUN.
		 */
		if (sdsts & HDA_SDSTS_FIFOE) {
			d->fifo_errors++;
		}
		if (sdsts & HDA_SDSTS_DESE) {
			unsigned long f = spinlock_acquire_irq(&d->feed_lock);

			d->desc_errors++;
			d->running = 0;
			d->halt_pending = 1;
			spinlock_release_irq(&d->feed_lock, f);
		}
		hda_write8(d, d->sd_base + HDA_SD_STS,
		           sdsts & (HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE |
		                    HDA_SDSTS_DESE));
	}
	/*
	 * INTSTS deliberately not written.  All of GIS, CIS and SIS are RO
	 * (spec table 15); they are an OR of the real status bits and go away
	 * only when those do.  The write that used to be here did nothing --
	 * every acknowledgement that matters happened above.
	 */
}

static int hda_irq_handler(unsigned int irq, void *dev_id, void *frame)
{
	uint32_t status;
	hda_dev_t *d = dev_id;
	int handled = 0;
	int rounds;

	(void)irq;
	(void)frame;
	if (d == NULL) {
		return 0;
	}

	/*
	 * Re-read INTSTS until GIS goes away rather than servicing one
	 * snapshot.  FreeBSD's hdac_intr_handler() explains why: "It is
	 * plausible that hardware interrupts a host only when GIS goes from
	 * zero to one.  GIS is formed by OR-ing multiple hardware statuses,
	 * so it's possible that a previously cleared status gets set again
	 * while another status has not been cleared yet.  Thus, there will be
	 * no new interrupt as GIS always stayed set.  If we don't re-examine
	 * GIS then we can leave it set and never get an interrupt again."
	 *
	 * All-ones means the device has gone away (surprise removal, or the
	 * BAR unmapped); stop rather than spin on it.  The round cap is a
	 * backstop -- a source this driver cannot clear would otherwise hang
	 * the CPU in here, which this controller has managed before.
	 */
	for (rounds = 0; rounds < HDA_INTR_MAX_ROUNDS; rounds++) {
		status = hda_read32(d, HDA_REG_INTSTS);
		if (status == 0xFFFFFFFFu || (status & HDA_INTSTS_GIS) == 0) {
			break;
		}
		hda_one_intr(d, status);
		handled = 1;
	}
	return handled;
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
	if (hda_stream_reset(d) != 0) {
		return -EIO;
	}

	/*
	 * Build the whole ring once.  An HDA output stream is cyclic over a
	 * fixed set of descriptors: CBL is the total byte length and must equal
	 * the sum of the valid entries, and LVI is the index of the last one.
	 * Refilling happens by rewriting slot CONTENTS on IOC completion, never
	 * by appending entries and moving LVI while running -- 4.5.6: "the
	 * software should only modify the BDL before the RUN bit has been set
	 * for the first time after a Stream Reset."
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

	/* The compiled-in default must be encodable; if it somehow is not,
	 * fall back to something the controller will accept rather than
	 * leaving SDnFMT at zero. */
	if (hda_encode_format(HDA_DEFAULT_RATE, 16, 2, &d->fmt) != 0) {
		kprintf("hda: default rate %u not encodable\n",
		        HDA_DEFAULT_RATE);
		return -EINVAL;
	}

	/*
	 * Publish the descriptor now as well as at every start.  The codec
	 * configuration that follows binds the converter to this format, and
	 * SDnFMT has to agree with it before anything runs.
	 */
	hda_write32(d, d->sd_base + HDA_SD_BDPL, (uint32_t)d->bdl_phys);
	hda_write32(d, d->sd_base + HDA_SD_BDPU, 0);
	hda_write16(d, d->sd_base + HDA_SD_LVI, HDA_BDL_ENTRIES - 1);
	hda_write32(d, d->sd_base + HDA_SD_CBL,
	            (uint32_t)(HDA_BDL_ENTRIES * HDA_CHUNK_BYTES));
	hda_write16(d, d->sd_base + HDA_SD_FMT, d->fmt);
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

static int hda_drain(audio_dev_t *adev);

static int hda_close(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;

	/*
	 * Play out what the application already handed us before stopping.
	 * Without this, close() truncates: the writer only blocks once the
	 * 256 KiB software FIFO fills, so a short clip is fully accepted, the
	 * process exits, and killing the stream here discards nearly all of
	 * it -- a two second tone came out as 720 bytes.
	 *
	 * hda_drain() is the whole wait now.  close() used to follow it with
	 * a second, weaker loop of its own -- no re-kick, no stall detection,
	 * and it tested the drained condition before registering on the sleep
	 * channel, so a completion landing in that window was lost and the
	 * thread only woke on the scheduler's ~250 ms lost-wakeup fallback.
	 */
	(void)hda_drain(adev);

	/*
	 * Stop the stream and reset the ring/FIFO state under feed_lock
	 * (IRQ-masked).  hda_feed() runs from the IRQ handler holding this
	 * lock; without it, a completion IRQ firing between CTL=0 and the
	 * reset below re-arms the ring / advances slots_played against the
	 * counters we are zeroing, corrupting the next stream's back-pressure.
	 */
	unsigned long flags = spinlock_acquire_irq(&d->feed_lock);

	/* Clear RUN and wait for the engine to idle before touching anything
	 * else -- 4.5.4, the bit does not drop on the write. */
	(void)hda_stream_stop(d);
	/* Drop latched status bits so stale BCIS doesn't bump the next
	 * stream's slots_played at open. */
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);

	/* Reset ring back-pressure state.  Otherwise the second cat
	 * inherits writes_queued from this stream but slots_played
	 * never catches up (no more IRQs after the stop), so the
	 * back-pressure spin thinks the ring is permanently full. */
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->next_idx      = 0;
	d->halt_pending  = 0;
	audio_fifo_reset(&d->fifo);

	spinlock_release_irq(&d->feed_lock, flags);

	/* Surface anything the completion handler counted but could not
	 * print.  Underruns point at the feeder, descriptor errors at the
	 * BDL or the bus. */
	if (d->fifo_errors != 0 || d->desc_errors != 0) {
		kprintf("hda: %u FIFO underrun(s), %u descriptor error(s)\n",
		        d->fifo_errors, d->desc_errors);
		d->fifo_errors = 0;
		d->desc_errors = 0;
	}
	return 0;
}

static int hda_set_params(audio_dev_t *adev, audio_info_t *info)
{
	hda_dev_t *d = adev->driver_data;
	unsigned long flags;
	uint16_t fmt;
	int rc;

	rc = hda_encode_format(info->play.sample_rate,
	                       info->play.precision, info->play.channels,
	                       &fmt);
	if (rc != 0) {
		return rc;
	}
	rc = hda_codec_supports(d, info->play.sample_rate,
	                        info->play.precision, info->play.channels);
	if (rc != 0) {
		return rc;
	}
	flags = spinlock_acquire_irq(&d->feed_lock);

	if (fmt == d->fmt && d->running) {
		/* Nothing to change; do not disturb a running stream. */
		spinlock_release_irq(&d->feed_lock, flags);
		return 0;
	}

	/*
	 * Stop before touching SDnFMT.  The register is only writable with
	 * the engine idle -- 3.3.38 and 3.3.41 both restrict descriptor
	 * programming to a stopped stream, and FreeBSD only writes SDFMT
	 * inside stream_start with RUN clear.  Rewriting it underneath a
	 * running engine also left the PCM already queued in the ring to be
	 * played at the new rate and channel count.
	 *
	 * Anything still buffered belongs to the old format, so it goes:
	 * a format change mid-stream is the application telling us the old
	 * audio is finished with.
	 */
	if (d->running) {
		(void)hda_stream_stop(d);
	}
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);
	d->next_idx      = 0;
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->halt_pending  = 0;
	audio_fifo_reset(&d->fifo);

	/* Remembered because a stream reset clears SDnFMT, and every start
	 * goes through one. */
	d->fmt = fmt;
	hda_write16(d, d->sd_base + HDA_SD_FMT, fmt);

	spinlock_release_irq(&d->feed_lock, flags);

	/* The converter has its own copy of the format; leaving it on the
	 * old one makes the codec decode the stream wrongly (wrong rate /
	 * channel count) rather than fall silent, which is worse.  Sent
	 * outside the feed lock because it is a CORB round trip. */
	hda_codec_bind_stream(d, fmt);
	return 0;
}

static int hda_write(audio_dev_t *adev, const void *buf, size_t len)
{
	hda_dev_t *d = adev->driver_data;
	const uint8_t *src = buf;
	size_t total_consumed = 0;

	if (len == 0) {
		return 0;
	}
	/* No ring and no FIFO means nothing can ever be played.  Returning
	 * len here reported a full successful write and threw the audio
	 * away. */
	if (d->chunk[0] == NULL || d->fifo_buf == NULL) {
		return -ENXIO;
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

/*
 * Block until everything queued has been played out.
 *
 * This used to stage the FIFO tail into the ring and return immediately,
 * which is not a drain at all -- AUDIO_DRAIN and SNDCTL_DSP_SYNC are
 * defined to block until output completes, and hda_close() had to
 * open-code a weaker wait of its own to stop truncating clips.
 *
 * Same shape as ac97_drain(): re-kick every pass so the queue actually
 * moves, track a monotonically decreasing byte count so a wedged
 * controller gives up instead of hanging, and bail on a pending unmasked
 * signal so a killed player exits promptly.
 */
static int hda_drain(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	uint32_t poll;
	uint32_t stall = 0;
	uint32_t last_remaining = 0xFFFFFFFFu;
	uint32_t hz = get_hz();
	uint64_t step = hz ? (hz * HDA_DRAIN_POLL_MS) / 1000U : 1U;

	if (d == NULL || d->fifo_buf == NULL || d->chunk[0] == NULL) {
		return 0;
	}
	if (step == 0) {
		step = 1;
	}

	for (poll = 0; poll < HDA_DRAIN_POLL_MAX; poll++) {
		unsigned long f;
		int32_t in_flight;
		size_t used;
		uint32_t remaining;

		/*
		 * End of stream: the residue left in the FIFO is shorter than
		 * a slot and the normal path only queues whole slots, so it
		 * would sit there forever.  Padding it out is correct here --
		 * it really is the end of the audio.  Then kick, which starts
		 * or restarts the engine and retires any pending halt.
		 */
		f = spinlock_acquire_irq(&d->feed_lock);
		hda_feed(d, 1);
		spinlock_release_irq(&d->feed_lock, f);
		hda_kick(d);

		f = spinlock_acquire_irq(&d->feed_lock);
		used = audio_fifo_used(&d->fifo);
		/* Signed: a completion racing past writes_queued must read as
		 * drained, not as ~4 billion still outstanding. */
		in_flight = (int32_t)(d->writes_queued -
		                      __atomic_load_n(&d->slots_played,
		                                      __ATOMIC_ACQUIRE));
		spinlock_release_irq(&d->feed_lock, f);

		if (used == 0 && in_flight <= 0) {
			break;
		}
		if (current_thread &&
		    (current_thread->sig_pending & ~current_thread->sig_mask)) {
			break;                 /* interrupted -- drop the tail */
		}

		remaining = (uint32_t)used +
		            (uint32_t)in_flight * HDA_CHUNK_BYTES;
		if (remaining < last_remaining) {
			last_remaining = remaining;
			stall = 0;
		} else if (++stall >= HDA_DRAIN_STALL_POLLS) {
			break;                 /* controller wedged */
		}

		if (current_thread) {
			(void)sched_sleep_until((void *)d, get_ticks() + step);
		} else {
			for (volatile int i = 0; i < 200000; i++) {
				__asm__ volatile("pause");
			}
		}
	}
	return 0;
}

static int hda_flush(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	unsigned long flags = spinlock_acquire_irq(&d->feed_lock);
	/* Stop, do not park in reset: asserting SRST and leaving it set wedges
	 * the stream descriptor for every later start.  hda_stream_stop()
	 * waits for RUN to actually drop before we discard the ring state. */
	(void)hda_stream_stop(d);
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);
	d->next_idx      = 0;
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->halt_pending  = 0;
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

/*
 * Undo a partially-completed attach.
 *
 * Every failure return after the IRQ is claimed used to just return.
 * hda_device_count is only bumped on success, so the next controller
 * reused hda_devices[0] and memset() it out from under a handler that
 * was still registered and still pointing at it: d->mmio became NULL and
 * the first shared-line interrupt faulted inside the handler.  The DMA
 * rings and the software FIFO leaked along with it.
 *
 * Disarm the controller before dropping the handler, not after, so the
 * level-triggered INTx cannot be left asserted with nothing to service
 * it.
 */
static void hda_detach_partial(hda_dev_t *d)
{
	int i;

	if (d->mmio != NULL) {
		hda_write32(d, HDA_REG_INTCTL, 0);
		hda_write8(d, HDA_REG_CORBCTL, 0);
		hda_write8(d, HDA_REG_RIRBCTL, 0);
	}
	if (d->irq_claimed) {
		free_irq((unsigned int)d->irq, d);
		d->irq_claimed = 0;
	}

	for (i = 0; i < HDA_BDL_ENTRIES; i++) {
		if (d->chunk[i] != NULL) {
			dma_free_coherent(d->chunk[i], HDA_CHUNK_BYTES);
			d->chunk[i] = NULL;
		}
	}
	if (d->bdl != NULL) {
		dma_free_coherent(d->bdl,
		                  HDA_BDL_ENTRIES * sizeof(hda_bdl_entry_t));
		d->bdl = NULL;
	}
	if (d->corb != NULL) {
		dma_free_coherent(d->corb,
		                  d->corb_entries * sizeof(uint32_t));
		d->corb = NULL;
	}
	if (d->rirb != NULL) {
		dma_free_coherent(d->rirb,
		                  d->rirb_entries * sizeof(uint64_t));
		d->rirb = NULL;
	}
	if (d->fifo_buf != NULL) {
		kfree(d->fifo_buf, HDA_FIFO_BYTES);
		d->fifo_buf = NULL;
	}
}

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
	spinlock_init(&d->verb_lock, "hda_verb");
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

	/*
	 * Capabilities are only trustworthy once the controller is out of
	 * reset.  3.3.7: "Software must read a 1 from this bit before
	 * accessing any controller registers", and while CRST is 0 "most
	 * registers will return their default values on reads."
	 *
	 * GCAP is RO/HwInit, so on most parts its "default" is the real
	 * capability value and reading early happens to work -- but nothing
	 * guarantees that, and both BSDs reset before reading it (FreeBSD
	 * hdac_reset() then hdac_get_capabilities(), NetBSD hdaudio_reset()
	 * then hdaudio_init()).  hda_quiesce() takes its own provisional
	 * read to bound the loop that stops the stream engines; this is the
	 * authoritative one.
	 */
	if (hda_controller_reset(d) != 0) {
		kprintf("hda: controller reset timed out\n");
		hda_detach_partial(d);
		return -EIO;
	}

	gcap = hda_read16(d, HDA_REG_GCAP);
	d->oss = (uint8_t)((gcap >> 12) & 0x0F);
	d->iss = (uint8_t)((gcap >> 8) & 0x0F);
	d->bss = (uint8_t)((gcap >> 3) & 0x1F);
	if (d->oss == 0) {
		kprintf("hda: controller advertises no output streams\n");
		hda_detach_partial(d);
		return -ENODEV;
	}
	/* Output stream 0 sits after the input descriptors. */
	d->sd_index = d->iss;
	d->sd_base = HDA_SD_BASE + ((uint32_t)d->sd_index * HDA_SD_STRIDE);

	d->codec_mask = hda_read16(d, HDA_REG_STATESTS);
	if (d->codec_mask == 0) {
		kprintf("hda: no codec detected\n");
		hda_detach_partial(d);
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
		hda_detach_partial(d);
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
	/*
	 * No usable interrupt line means the controller must not be armed at
	 * all.  GIE with no handler registered is the wedge described above,
	 * just with nobody at all to service the line instead of the wrong
	 * driver -- STATESTS and RIRBSTS would never be acknowledged and the
	 * level-triggered INTx would stay asserted forever.  Since this
	 * driver refills the DMA ring from the completion path, it cannot run
	 * usefully without interrupts anyway; fail the attach rather than
	 * register a device that can never play past its first buffers.
	 */
	if (d->irq < 0) {
		kprintf("hda: no usable IRQ line; not attaching\n");
		hda_detach_partial(d);
		return -ENXIO;
	}
	/* Shared PCI INTx -- see the note in ac97.c. */
	if (request_irq((unsigned int)d->irq, hda_irq_handler,
	                IRQF_SHARED, "hda", d) != 0) {
		kprintf("hda: could not claim IRQ %d\n", d->irq);
		hda_detach_partial(d);
		return -EBUSY;
	}
	d->irq_claimed = 1;

	/* Now safe to arm.  Some controllers only latch a codec response with
	 * CIE armed, so this has to precede the first verb.
	 *
	 * SIE for our output descriptor has to be armed here too.  Without it
	 * SDCTL.IOCE still sets SDSTS.BCIS on every completed buffer, but the
	 * controller never raises the interrupt, so no completion is ever
	 * credited: the ring is refilled from the completion path, so a cyclic
	 * stream just replays its 32 slots forever and a writer wedges as soon
	 * as the FIFO fills. */
	hda_write32(d, HDA_REG_INTCTL,
	            HDA_INTCTL_GIE | HDA_INTCTL_CIE |
	            HDA_INTCTL_SIE(d->sd_index));

	/* First real conversation with the codec.  A timeout here is not the
	 * same as a vendor ID of zero, and it means nothing that follows will
	 * work either. */
	if (hda_try_verb(d, d->codec_addr, 0, HDA_VERB_GET_PARAMETER,
	                 HDA_PARAM_VENDOR_ID, &vendor_id) != 0) {
		kprintf("hda: codec %u did not answer\n", d->codec_addr);
		hda_detach_partial(d);
		return -EIO;
	}

	if (hda_output_stream_init(d) != 0) {
		hda_detach_partial(d);
		return -ENOMEM;
	}

	/* Must follow output_stream_init: the codec is bound to the stream
	 * tag and format that call establishes.  A codec we cannot route is
	 * not a usable audio device -- registering it would give userland a
	 * /dev/audio0 that silently swallows everything. */
	if (hda_codec_configure(d) != 0) {
		kprintf("hda: codec configuration failed; not registering\n");
		hda_detach_partial(d);
		return -ENODEV;
	}

	/* The IRQ was claimed before INTCTL was armed, further up. */

	d->audio.ops = &hda_ops;
	d->audio.driver_data = d;
	snprintf(d->audio.name, sizeof(d->audio.name), "hda");
	if (audio_register_device(&d->audio) != 0) {
		hda_detach_partial(d);
		return -EBUSY;
	}

	hda_device_count++;
	kprintf("hda: %04x:%04x oss=%u iss=%u codecs=0x%04x cad=%u "
	        "vid=0x%08x corb=%u rirb=%u\n",
	        pdev->vendor_id, pdev->device_id, d->oss, d->iss,
	        d->codec_mask, d->codec_addr, vendor_id,
	        d->corb_entries, d->rirb_entries);
	/* Silence with a healthy-looking graph usually means verbs are being
	 * dropped, so say when any were. */
	if (d->verb_timeouts != 0) {
		kprintf("hda: %u verb timeout(s) during configuration\n",
		        d->verb_timeouts);
	}
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
