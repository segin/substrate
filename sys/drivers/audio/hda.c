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
#include <kern/time.h>
#include <sys/audioio.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/irq.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>

#define HDA_PCI_CLASS_MULTIMEDIA   0x04
#define HDA_PCI_SUBCLASS_HDA       0x03

/* Vendors whose controllers need coaxing before DMA; see hda_pci_quirks(). */
#define HDA_PCI_VENDOR_INTEL       0x8086
#define HDA_PCI_VENDOR_ATI         0x1002
#define HDA_PCI_VENDOR_AMD         0x1022
#define HDA_PCI_VENDOR_NVIDIA      0x10DE
/* Intel: Traffic Class Select, in device-specific PCI config space. */
#define HDA_PCI_REG_TCSEL          0x44

#define HDA_BDL_ENTRIES            32
#define HDA_CHUNK_BYTES            4096U
#define HDA_DEFAULT_RATE           48000U
#define HDA_FIFO_BYTES             (256U * 1024U) /* deep software PCM FIFO */
#define HDA_PREBUFFER_SLOTS        8U     /* DMA slots staged before start */
/*
 * How long staged audio may wait for more before it is padded out and
 * started anyway -- a short effect written with the descriptor left open
 * must not wait for the prebuffer to fill.  A producer pacing itself in
 * real time writes again well within this.
 */
#define HDA_IDLE_START_MS          30U
/* Worker poll period while a stream is active. */
#define HDA_WORKER_MS              10U
/*
 * Slots the completion handler may refill in one interrupt.  Each
 * completion frees exactly one, so one is enough to keep pace; the point
 * of the cap is that hda_feed() runs there with interrupts masked and an
 * unbounded refill could copy the whole 128 KiB ring in one go.
 */
#define HDA_FEED_SLOTS_PER_IRQ     2
/* Per-verb spin budget for the interrupt proof's wait. */
#define HDA_VERB_TIMEOUT           20000U
/* Per-verb wait: a response takes a frame or two; this is generous. */
#define HDA_VERB_TIMEOUT_US        50000U
#define HDA_VERB_POLL_US           10U
/* Consecutive timeouts after which a codec is taken as gone. */
#define HDA_CODEC_DEAD_TIMEOUTS    3U
/* hda_send_verb()'s "no answer". */
#define HDA_VERB_FAILED            0xFFFFFFFFU
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
/* Converters one stream may be bound to. */
#define HDA_MAX_OUT_DACS           4
/* STATESTS reports codec presence on SDI[14:0]. */
#define HDA_MAX_CODECS             15

/* ------------------------------------------------------------------- */
/* Pure helpers (also reachable from host tests)                       */
/* ------------------------------------------------------------------- */

/*
 * Which payload width a command uses.  The HDA spec gives a 16-bit payload
 * to the 4-bit commands -- 2h and 3h (set converter format / amp gain),
 * 4h and 5h (set processing coefficient / coefficient index), and their
 * Ah/Bh/Ch/Dh getters -- and an 8-bit payload to every other, 12-bit,
 * command.  The constants spell them as 0xN00.
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
	/* Coefficient index and processing coefficient, set and get. */
	case 0x4: case 0x5: case 0xC: case 0xD:
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
	/* Set when d->irq came from pci_route_intx() rather than firmware:
	 * that mapping is a convention and has to be proven before use. */
	int             intx_routed;
	/* Set when d->irq is an MSI vector (from irq_alloc_vector()); MSI
	 * is enabled in config space only once the handler is registered. */
	int             msi;
	int             msi_enabled;
	volatile uint32_t intr_count;
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
	/* Solicited responses still owed, per codec address. */
	uint8_t          pending[16];
	/* Consecutive timeouts per codec, and codecs given up on. */
	uint8_t          codec_timeouts[16];
	uint16_t         codec_dead;
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
	uint8_t          dac_nid;       /* primary converter */
	uint8_t          pin_nid;       /* primary output pin */
	/* Every converter bound to the stream (pins that cannot reach the
	 * primary one get another). */
	uint8_t          dacs[HDA_MAX_OUT_DACS];
	int              ndacs;
	uint32_t         codec_vid;     /* configured codec's vendor/device */
	int              have_path;

	/* Back-pressure: writes_queued bumps in hda_write after we
	 * advance LVI; slots_played bumps in IRQ handler on BCIS.
	 * Block when in_flight = (writes_queued - slots_played) hits
	 * BDL_ENTRIES - 1 to leave a slot for the controller's
	 * prefetch.  Same template as ac97 — independent of any
	 * hardware position register. */
	volatile uint32_t writes_queued;
	volatile uint32_t slots_played;
	/* writes_queued just after the last slot that held real audio. */
	uint32_t         data_end;
	/* SDnFIFOS + 1: how far the engine's fetch can run ahead of LPIB. */
	uint32_t         fifo_size;
	/*
	 * The stream worker (hda_worker()): starts staged audio the producer
	 * has stopped adding to, and retires halts the completion handler
	 * flagged when nobody else is around to.  last_write is when the
	 * producer last appended.
	 */
	int              worker_chan;
	volatile uint64_t last_write;
	uint64_t         wait_ticks;    /* writer's sleep bound */
	/* Serialises format changes (set_params) end to end. */
	mutex_t          cfg_lock;
	/* A format change is between SDnFMT and the converter: no start. */
	int              binding;
	/*
	 * A stream descriptor that would not acknowledge SRST is unusable
	 * (3.3.35).  Latched here and returned to writers instead of retrying
	 * the reset on every write; cleared by flush, close or a format change.
	 */
	int              stream_error;

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

/* Stop both command engines and wait for them to idle (3.3.22, 3.3.29). */
static void hda_corb_rirb_stop(hda_dev_t *d)
{
	uint32_t budget;

	hda_write8(d, HDA_REG_CORBCTL, 0);
	hda_write8(d, HDA_REG_RIRBCTL, 0);
	for (budget = 0; budget < HDA_RING_TIMEOUT; budget++) {
		if ((hda_read8(d, HDA_REG_CORBCTL) & HDA_CORBCTL_RUN) == 0 &&
		    (hda_read8(d, HDA_REG_RIRBCTL) & HDA_RIRBCTL_RUN) == 0) {
			return;
		}
	}
	kprintf("hda: command engines did not stop\n");
}

static int hda_corb_rirb_start(hda_dev_t *d);

static int hda_corb_rirb_setup(hda_dev_t *d)
{
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
	hda_corb_rirb_stop(d);

	hda_write32(d, HDA_REG_CORBLBASE, (uint32_t)d->corb_phys);
	hda_write32(d, HDA_REG_CORBUBASE, 0);
	hda_write8(d,  HDA_REG_CORBSIZE, corbsize_enc);
	hda_write32(d, HDA_REG_RIRBLBASE, (uint32_t)d->rirb_phys);
	hda_write32(d, HDA_REG_RIRBUBASE, 0);
	hda_write8(d,  HDA_REG_RIRBSIZE, rirbsize_enc);
	return hda_corb_rirb_start(d);
}

/*
 * With both engines stopped and the rings' bases and sizes programmed:
 * reset both pointers and start both engines.  Also how the rings are
 * resynchronised after a verb timeout (hda_send_verb_locked()).
 */
static int hda_corb_rirb_start(hda_dev_t *d)
{
	uint32_t budget;

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

	/* The RIRB write pointer reset is write-only and always reads back 0
	 * (3.3.27), so there is nothing to verify here.  Drop anything latched
	 * against the old position too. */
	hda_write16(d, HDA_REG_RIRBWP, HDA_RIRBWP_RST);
	hda_write8(d, HDA_REG_RIRBSTS, hda_read8(d, HDA_REG_RIRBSTS));
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
	uint32_t us;

	cad &= 0x0F;
	/* A codec that has stopped answering costs one timeout, not one per
	 * verb of a graph walk. */
	if (d->codec_dead & (1u << cad)) {
		return -EIO;
	}

	wp = (uint16_t)((d->corb_wp + 1) % d->corb_entries);
	d->corb[wp] = encoded;
	/* Publish the CORB entry before ringing the doorbell. */
	__sync_synchronize();
	hda_write16(d, HDA_REG_CORBWP, wp);
	d->corb_wp = wp;
	d->pending[cad]++;

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
	for (us = 0; us < HDA_VERB_TIMEOUT_US; ) {
		uint16_t rwp = hda_read16(d, HDA_REG_RIRBWP) & 0xFF;
		uint64_t entry;
		uint32_t ex, rcad;

		if (rwp == d->rirb_rp) {
			/* A response takes a frame or two (20.8 us each);
			 * bound the wait in time, not in MMIO reads. */
			timer_busywait_us(HDA_VERB_POLL_US);
			us += HDA_VERB_POLL_US;
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
		/*
		 * Solicited responses carry no tag, only the codec address,
		 * and come strictly in order (4.4.1).  One a codec sends after
		 * we gave up waiting would otherwise be taken as the answer to
		 * that codec's next verb, and every response after it would
		 * be one verb behind for the rest of the boot.  So count what
		 * is outstanding per codec and drop answers nobody is owed.
		 */
		rcad = ex & HDA_RIRB_EX_CODEC_MASK;
		if (d->pending[rcad] == 0) {
			continue;   /* late answer to a verb we abandoned */
		}
		d->pending[rcad]--;
		if (rcad != cad) {
			continue;   /* another codec's answer */
		}
		d->codec_timeouts[cad] = 0;
		*resp = (uint32_t)entry;
		return 0;
	}
	d->verb_timeouts++;

	/*
	 * Nothing came.  An answer could still arrive late; rather than
	 * trust the count to absorb it, put both rings back to a known
	 * position (a late answer then lands as an entry nobody is owed and
	 * is dropped).  After a few timeouts in a row the codec is taken as
	 * gone.
	 */
	memset(d->pending, 0, sizeof(d->pending));
	hda_corb_rirb_stop(d);
	if (hda_corb_rirb_start(d) != 0) {
		kprintf("hda: command rings would not restart after a verb "
		        "timeout\n");
	}
	if (++d->codec_timeouts[cad] >= HDA_CODEC_DEAD_TIMEOUTS) {
		d->codec_dead |= (uint16_t)(1u << cad);
		kprintf("hda: codec %u stopped answering\n", cad);
	}
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
	/*
	 * Not an IRQ-masking lock: the interrupt handler never touches the
	 * command rings, and a verb that gets no answer waits tens of
	 * milliseconds -- with interrupts masked that lost timer ticks.
	 */
	spinlock_acquire(&d->verb_lock);
	int rc = hda_send_verb_locked(d, cad, nid, verb, payload, resp);

	spinlock_release(&d->verb_lock);
	return rc;
}

/*
 * As hda_try_verb(), returning the response -- or HDA_VERB_FAILED when the
 * codec did not answer.  A zero response is meaningful and decodes as a
 * plausible analog output widget with an empty connection list, so a
 * timeout must not look like one; all-ones decodes as a vendor-defined
 * digital widget, which every caller skips.
 */
static uint32_t hda_send_verb(hda_dev_t *d, uint8_t cad, uint8_t nid,
                              uint16_t verb, uint16_t payload)
{
	uint32_t resp;

	if (hda_try_verb(d, cad, nid, verb, payload, &resp) != 0) {
		return HDA_VERB_FAILED;
	}
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

	/* Unanswered caps read all-ones: offset and steps would say
	 * maximum gain. */
	if (caps == HDA_VERB_FAILED) {
		return;
	}
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

	if (caps == HDA_VERB_FAILED) {
		return;
	}
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

	if (lenr == HDA_VERB_FAILED) {
		return 0;
	}
	for (i = 0; i < len && n < max; i += per) {
		uint32_t resp;
		int j;

		if (hda_try_verb(d, d->codec_addr, nid, HDA_VERB_GET_CONN_LIST,
		                 (uint16_t)i, &resp) != 0) {
			break;   /* a partial list, not a list of garbage */
		}

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
 * The audio function group's widgets, read once per configuration: the
 * path search revisits nodes for every pin and converter it tries, and
 * re-asking the codec each time multiplied the verb count.
 */
#define HDA_ROUTE_DEPTH  10

typedef struct hda_graph {
	uint8_t  start, count;               /* the AFG's subnode range */
	uint32_t caps[256];                  /* AUDIO_WIDGET_CAPS */
	uint32_t pincap[256];                /* PIN_CAPS, pins only */
	uint32_t cfg[256];                   /* CONFIG_DEFAULT, pins only */
	uint8_t  nconn[256];
	uint8_t  conn[256][HDA_MAX_CONNS];
} hda_graph_t;

/* One step of a route: `nid`, and the connection index it takes. */
struct hda_hop {
	uint8_t nid;
	uint8_t idx;
};

static int hda_graph_has(const hda_graph_t *g, uint8_t nid)
{
	return nid >= g->start && nid - g->start < g->count &&
	       g->caps[nid] != HDA_VERB_FAILED;
}

/*
 * Depth-first search from `nid` towards converter `dac`, through mixers
 * and selectors, recording the connection index taken at each hop.
 * Connection lists are the only way a converter reaches a pin -- even a
 * single hard-wired source is listed with length 1 (7.3.4.11) -- so no
 * route means no output.  Returns the number of hops, or -1.
 */
static int hda_path_dfs(const hda_graph_t *g, uint8_t nid, uint8_t dac,
                        int depth, uint32_t *visited, struct hda_hop *path)
{
	int i;

	if (depth >= HDA_ROUTE_DEPTH ||
	    (visited[nid >> 5] & (1u << (nid & 31)))) {
		return -1;
	}
	visited[nid >> 5] |= 1u << (nid & 31);

	for (i = 0; i < g->nconn[nid]; i++) {
		if (g->conn[nid][i] == dac) {
			path[depth].nid = nid;
			path[depth].idx = (uint8_t)i;
			return depth + 1;
		}
	}
	for (i = 0; i < g->nconn[nid]; i++) {
		uint8_t c = g->conn[nid][i];
		int type, r;

		if (!hda_graph_has(g, c) || (g->caps[c] & HDA_AW_DIGITAL)) {
			continue;
		}
		type = HDA_AW_TYPE(g->caps[c]);
		if (type != HDA_AW_TYPE_MIXER && type != HDA_AW_TYPE_SELECTOR) {
			continue;
		}
		path[depth].nid = nid;
		path[depth].idx = (uint8_t)i;
		r = hda_path_dfs(g, c, dac, depth + 1, visited, path);
		if (r > 0) {
			return r;
		}
	}
	return -1;
}

static int hda_find_path(const hda_graph_t *g, uint8_t pin, uint8_t dac,
                         struct hda_hop *path)
{
	uint32_t visited[256 / 32];

	memset(visited, 0, sizeof(visited));
	return hda_path_dfs(g, pin, dac, 0, visited, path);
}

/*
 * Program a route found by hda_find_path(): select the source on every
 * pin or selector that has a choice (a mixer sums all its inputs and has
 * none), and open the input amp on the connection used plus the output
 * amp of every widget between pin and converter.  Everything comes up
 * muted after reset (7.3.3.7), so a correctly routed graph is still
 * silent without the unmuting.
 */
static void hda_commit_path(hda_dev_t *d, const hda_graph_t *g,
                            const struct hda_hop *path, int n)
{
	int k;

	for (k = 0; k < n; k++) {
		uint8_t nid = path[k].nid;
		uint32_t caps = g->caps[nid];

		if (HDA_AW_TYPE(caps) != HDA_AW_TYPE_MIXER) {
			hda_conn_select(d, nid, g->nconn[nid], path[k].idx);
		}
		if (caps & HDA_AW_IN_AMP) {
			hda_amp_unmute_in(d, nid, caps, path[k].idx);
		}
		if (k > 0 && (caps & HDA_AW_OUT_AMP)) {
			hda_amp_unmute_out(d, nid, caps);
		}
	}
}

/* Tell every converter in use which format and stream tag to consume.
 * Must match SDnFMT and the tag programmed into SDCTL, and must be re-sent
 * whenever either changes -- a converter left on stream 0 is disconnected.
 * Several converters may consume one stream; each renders it. */
static void hda_codec_bind_stream(hda_dev_t *d, uint16_t fmt)
{
	int i;

	if (!d->have_path) {
		return;
	}
	for (i = 0; i < d->ndacs; i++) {
		hda_send_verb(d, d->codec_addr, d->dacs[i],
		              HDA_VERB_SET_CONV_FORMAT, fmt);
		hda_send_verb(d, d->codec_addr, d->dacs[i],
		              HDA_VERB_SET_CONV_STREAM,
		              (uint16_t)((d->stream_tag << 4) | 0));
	}
}

/*
 * Does the chosen converter actually support this format?
 *
 * SDnFMT being able to express a rate says nothing about the codec being
 * able to render it.  The capability bits (7.3.4.7 table 139) only cover
 * the eleven standard rates, so a rate outside that set cannot be
 * advertised and is refused here even though hda_encode_format() can
 * encode it -- the encoder describes the register, this the hardware.
 *
 * 48 kHz needs no bit: the spec makes it mandatory, and the converter is
 * bound to it at attach.  A partial capability word, or a Format Override
 * word without R7, must not disable the one guaranteed rate.
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

	if (rate != 48000) {
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
			return -EINVAL;   /* no capability bit for this rate */
		}
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

/* A parameter, or 0 if the codec did not answer (0 means "unknown" to
 * every capability field the caller keeps). */
static uint32_t hda_get_param_or0(hda_dev_t *d, uint8_t nid, uint8_t param)
{
	uint32_t v = hda_get_param(d, nid, param);

	return v == HDA_VERB_FAILED ? 0 : v;
}

/* Output pin ranking: speakers, then line out, then headphones. */
static int hda_pin_rank(uint32_t cfg)
{
	switch (HDA_CONFIG_DEVICE(cfg)) {
	case HDA_DEVICE_SPEAKER:  return 3;
	case HDA_DEVICE_LINE_OUT: return 2;
	case HDA_DEVICE_HP_OUT:   return 1;
	default:                  return 0;   /* not an analog output */
	}
}

/*
 * Drive one output pin: a route to a converter (the primary one if it can
 * be reached, else any other), output enabled, its output amp open.
 * Returns the converter used, or 0.
 */
static uint8_t hda_drive_pin(hda_dev_t *d, const hda_graph_t *g, uint8_t pin,
                             const uint8_t *dacs, int ndacs)
{
	struct hda_hop path[HDA_ROUTE_DEPTH];
	uint32_t ctrl;
	int i, n = -1;
	uint8_t dac = 0;

	for (i = 0; i < ndacs && n < 0; i++) {
		n = hda_find_path(g, pin, dacs[i], path);
		dac = dacs[i];
	}
	if (n < 0) {
		return 0;
	}
	hda_commit_path(d, g, path, n);

	/*
	 * OUT_EN, and HP_EN only where the pin can drive headphones.  The
	 * input enable and VRef (mic bias) bits a retaskable jack may have
	 * been left with are cleared: an output must not carry bias or an
	 * active input path.
	 */
	ctrl = hda_send_verb(d, d->codec_addr, pin,
	                     HDA_VERB_GET_PIN_WIDGET_CONTROL, 0);
	ctrl = ctrl == HDA_VERB_FAILED ? 0 : (ctrl & 0xFF);
	ctrl &= ~(uint32_t)(HDA_PIN_CTRL_IN_ENABLE | HDA_PIN_CTRL_VREF_MASK |
	                    HDA_PIN_CTRL_HP_ENABLE);
	ctrl |= HDA_PIN_CTRL_OUT_ENABLE;
	if (HDA_CONFIG_DEVICE(g->cfg[pin]) == HDA_DEVICE_HP_OUT &&
	    (g->pincap[pin] & HDA_PINCAP_HP_DRIVE)) {
		ctrl |= HDA_PIN_CTRL_HP_ENABLE;
	}
	hda_send_verb(d, d->codec_addr, pin, HDA_VERB_SET_PIN_WIDGET_CONTROL,
	              (uint16_t)ctrl);
	if (g->caps[pin] & HDA_AW_OUT_AMP) {
		hda_amp_unmute_out(d, pin, g->caps[pin]);
	}
	return dac;
}

/*
 * Codec-specific initialisation after the generic graph is committed.
 *
 * Realtek ALC255/ALC256-class codecs carry headset-jack behaviour in
 * vendor coefficients of processing node 0x20 (index 0x46, bits 13:12).
 * Which value headphones need has not been confirmed on these machines,
 * so the coefficient is reported rather than changed; the plumbing is
 * here for when it has been.
 */
static void hda_codec_vendor_init(hda_dev_t *d)
{
	uint32_t coef;

	switch (d->codec_vid) {
	case 0x10ec0255:
	case 0x10ec0256:
	case 0x10ec0236:
	case 0x10ec0295:
		if (hda_send_verb(d, d->codec_addr, 0x20,
		                  HDA_VERB_SET_COEF_INDEX, 0x46) ==
		    HDA_VERB_FAILED) {
			return;
		}
		coef = hda_send_verb(d, d->codec_addr, 0x20,
		                     HDA_VERB_GET_PROC_COEF, 0);
		if (coef != HDA_VERB_FAILED) {
			kprintf("hda: ALC%x headset coefficient 0x46 = 0x%04x\n",
			        (unsigned)(d->codec_vid & 0xFFFF),
			        (unsigned)(coef & 0xFFFF));
		}
		break;
	default:
		break;
	}
}

static int hda_configure_codec(hda_dev_t *d)
{
	hda_graph_t *g;
	uint32_t sub;
	uint8_t start, count, i;
	uint8_t afg = 0;
	uint8_t dacs[HDA_MAX_CANDIDATES];
	uint8_t pins[HDA_MAX_CANDIDATES];
	int ndacs = 0, npins = 0;
	int primary = -1, assoc0_only = 1;
	uint8_t pdac = 0;
	int rc = -ENODEV;
	int j;

	d->codec_vid = hda_get_param_or0(d, 0, HDA_PARAM_VENDOR_ID);

	/* Node 0's subnodes are the function groups; we want the audio one. */
	sub = hda_get_param(d, 0, HDA_PARAM_SUBNODE_COUNT);
	if (sub == HDA_VERB_FAILED) {
		return -EIO;
	}
	start = (uint8_t)HDA_SUBNODE_START(sub);
	count = (uint8_t)HDA_SUBNODE_COUNT(sub);
	for (i = 0; i < count; i++) {
		uint8_t nid = (uint8_t)(start + i);
		uint32_t t = hda_get_param(d, nid,
		                           HDA_PARAM_FUNCTION_GROUP_TYPE);
		/* NodeType is all of bits 7:0 (7.3.4.4): 0x81 is a
		 * vendor-defined group, not audio. */
		if (t != HDA_VERB_FAILED && (t & 0xFF) == HDA_FGT_AUDIO) {
			afg = nid;
			break;
		}
	}
	if (afg == 0) {
		kprintf("hda: no audio function group\n");
		return -ENODEV;
	}
	d->afg_nid = afg;

	/*
	 * Power: the function group first, then -- after it has had time to
	 * come up -- every widget that keeps its own power state, then a
	 * settle before anything is configured (7.3.3.10).  Only the AFG,
	 * the converter and the pin used to be powered, and nothing waited:
	 * a mixer or selector with its own power control stayed in D3.
	 */
	hda_send_verb(d, d->codec_addr, afg, HDA_VERB_SET_POWER_STATE,
	              HDA_PS_D0);
	timer_busywait_us(100);

	/*
	 * The AFG's amp, rate and format capabilities are the defaults for
	 * every widget without Amp Param / Format Override, which on real
	 * codecs is most of them (7.3.4.6-8).
	 */
	d->afg_outamp_caps = hda_get_param_or0(d, afg, HDA_PARAM_OUTPUT_AMP_CAPS);
	d->afg_inamp_caps  = hda_get_param_or0(d, afg, HDA_PARAM_INPUT_AMP_CAPS);
	d->afg_pcm_caps    = hda_get_param_or0(d, afg, HDA_PARAM_SUPPORTED_RATES);
	d->afg_fmt_caps    = hda_get_param_or0(d, afg,
	                                       HDA_PARAM_SUPPORTED_FORMATS);

	g = kmalloc(sizeof(*g));
	if (g == NULL) {
		return -ENOMEM;
	}
	memset(g, 0, sizeof(*g));
	sub = hda_get_param(d, afg, HDA_PARAM_SUBNODE_COUNT);
	if (sub == HDA_VERB_FAILED) {
		rc = -EIO;
		goto out;
	}
	g->start = (uint8_t)HDA_SUBNODE_START(sub);
	g->count = (uint8_t)HDA_SUBNODE_COUNT(sub);
	if ((unsigned)g->start + g->count > 256) {
		g->count = (uint8_t)(256 - g->start);
	}

	for (i = 0; i < g->count; i++) {
		uint8_t nid = (uint8_t)(g->start + i);
		uint32_t caps = hda_get_param(d, nid,
		                              HDA_PARAM_AUDIO_WIDGET_CAPS);

		g->caps[nid] = caps;
		if (caps == HDA_VERB_FAILED) {
			continue;
		}
		if (caps & HDA_AW_POWER_CNTRL) {
			hda_send_verb(d, d->codec_addr, nid,
			              HDA_VERB_SET_POWER_STATE, HDA_PS_D0);
		}
		if (caps & HDA_AW_CONN_LIST) {
			g->nconn[nid] = (uint8_t)hda_conn_list(d, nid,
			                                       g->conn[nid],
			                                       HDA_MAX_CONNS);
		}
		if (HDA_AW_TYPE(caps) == HDA_AW_TYPE_PIN) {
			g->pincap[nid] = hda_get_param_or0(d, nid,
			                                   HDA_PARAM_PIN_CAPS);
			g->cfg[nid] = hda_send_verb(d, d->codec_addr, nid,
			                            HDA_VERB_GET_CONFIG_DEFAULT,
			                            0);
		}
	}
	timer_busywait_ms(1);

	/*
	 * Candidates.  Digital widgets are skipped: an HDMI/DisplayPort path
	 * needs digital converter control, channel mapping and ELD handling
	 * this driver has none of.  Output pins must be wired to something
	 * and be an analog output device; association 0 is reserved and
	 * marks a pin firmware did not assign, so those count only when no
	 * pin has a real association.
	 */
	for (i = 0; i < g->count; i++) {
		uint8_t nid = (uint8_t)(g->start + i);
		uint32_t caps = g->caps[nid];
		uint32_t cfg = g->cfg[nid];

		if (caps == HDA_VERB_FAILED || (caps & HDA_AW_DIGITAL)) {
			continue;
		}
		if (HDA_AW_TYPE(caps) == HDA_AW_TYPE_DAC) {
			if (ndacs < HDA_MAX_CANDIDATES) {
				dacs[ndacs++] = nid;
			}
			continue;
		}
		if (HDA_AW_TYPE(caps) != HDA_AW_TYPE_PIN ||
		    (g->pincap[nid] & HDA_PINCAP_OUTPUT) == 0 ||
		    cfg == HDA_VERB_FAILED ||
		    HDA_CONFIG_PORTCONN(cfg) == HDA_PORTCONN_NONE ||
		    hda_pin_rank(cfg) == 0 || npins >= HDA_MAX_CANDIDATES) {
			continue;
		}
		if (HDA_CONFIG_ASSOC(cfg) != 0) {
			assoc0_only = 0;
		}
		pins[npins++] = nid;
	}
	if (ndacs == 0 || npins == 0) {
		kprintf("hda: no analog output widgets (dacs=%d pins=%d)\n",
		        ndacs, npins);
		goto out;
	}

	/* The primary output: best device rank, then lowest association,
	 * then lowest sequence -- among pins some converter reaches. */
	for (j = 0; j < npins; j++) {
		struct hda_hop path[HDA_ROUTE_DEPTH];
		uint32_t cfg = g->cfg[pins[j]];
		int k;

		if (HDA_CONFIG_ASSOC(cfg) == 0 && !assoc0_only) {
			continue;
		}
		if (primary >= 0) {
			uint32_t pc = g->cfg[pins[primary]];

			if (hda_pin_rank(cfg) < hda_pin_rank(pc) ||
			    (hda_pin_rank(cfg) == hda_pin_rank(pc) &&
			     (HDA_CONFIG_ASSOC(cfg) > HDA_CONFIG_ASSOC(pc) ||
			      (HDA_CONFIG_ASSOC(cfg) == HDA_CONFIG_ASSOC(pc) &&
			       HDA_CONFIG_SEQ(cfg) >= HDA_CONFIG_SEQ(pc))))) {
				continue;
			}
		}
		for (k = 0; k < ndacs; k++) {
			if (hda_find_path(g, pins[j], dacs[k], path) > 0) {
				primary = j;
				pdac = dacs[k];
				break;
			}
		}
	}
	if (primary < 0) {
		/* Nothing reaches an output pin: there is no output path. */
		kprintf("hda: no converter reaches an output pin\n");
		goto out;
	}

	/*
	 * Drive every pin of the primary output's association (a woofer
	 * beside the speakers, say), and every headphone pin -- there is no
	 * jack detection, and this codec class does not mute the speakers in
	 * hardware, so headphones simply play alongside.  A pin that cannot
	 * reach the primary converter gets another one bound to the same
	 * stream.
	 */
	d->ndacs = 0;
	d->dacs[d->ndacs++] = pdac;
	for (j = -1; j < npins; j++) {
		uint8_t pin = (j < 0) ? pins[primary] : pins[j];
		uint32_t cfg = g->cfg[pin];
		uint8_t order[HDA_MAX_CANDIDATES];
		uint8_t used;
		int k, n = 0;

		if (j == primary) {
			continue;
		}
		if (j >= 0 &&
		    HDA_CONFIG_ASSOC(cfg) != HDA_CONFIG_ASSOC(g->cfg[pins[primary]]) &&
		    HDA_CONFIG_DEVICE(cfg) != HDA_DEVICE_HP_OUT) {
			continue;
		}
		/* The converters already in use first, then the rest. */
		for (k = 0; k < d->ndacs; k++) {
			order[n++] = d->dacs[k];
		}
		for (k = 0; k < ndacs; k++) {
			int m, dup = 0;

			for (m = 0; m < d->ndacs; m++) {
				dup |= (dacs[k] == d->dacs[m]);
			}
			if (!dup) {
				order[n++] = dacs[k];
			}
		}
		used = hda_drive_pin(d, g, pin, order, n);
		if (used == 0) {
			kprintf("hda: pin %u reaches no converter; left off\n",
			        pin);
			continue;
		}
		for (k = 0; k < d->ndacs && d->dacs[k] != used; k++) {
		}
		if (k == d->ndacs && d->ndacs < HDA_MAX_OUT_DACS) {
			d->dacs[d->ndacs++] = used;
		}
	}

	/*
	 * EAPD on every pin that has it.  The external amplifier's enable pad
	 * often follows another pin's EAPD bit -- headphone, line-out or dock
	 * -- or an OR of several, so asserting it on the chosen pin alone
	 * left the speakers silent on such boards.  BTL and L/R swap (bits 0
	 * and 2) are kept.
	 */
	for (i = 0; i < g->count; i++) {
		uint8_t nid = (uint8_t)(g->start + i);
		uint32_t eapd;

		if (g->caps[nid] == HDA_VERB_FAILED ||
		    HDA_AW_TYPE(g->caps[nid]) != HDA_AW_TYPE_PIN ||
		    (g->pincap[nid] & HDA_PINCAP_EAPD) == 0) {
			continue;
		}
		eapd = hda_send_verb(d, d->codec_addr, nid,
		                     HDA_VERB_GET_EAPD_BTL, 0);
		if (eapd == HDA_VERB_FAILED) {
			continue;
		}
		hda_send_verb(d, d->codec_addr, nid, HDA_VERB_SET_EAPD_BTL,
		              (uint16_t)((eapd & HDA_EAPD_MASK) |
		                         HDA_EAPD_ENABLE));
	}

	for (j = 0; j < d->ndacs; j++) {
		uint32_t dcaps = g->caps[d->dacs[j]];

		if (dcaps & HDA_AW_OUT_AMP) {
			hda_amp_unmute_out(d, d->dacs[j], dcaps);
		}
	}

	/*
	 * Remember what the primary converter can do, so set_params can
	 * refuse a format the codec would silently mis-render.  Per 7.3.4.6 a
	 * widget's own rate/format parameters only apply when Format Override
	 * is set; otherwise the AFG's defaults do.
	 */
	{
		uint32_t dcaps = g->caps[pdac];
		uint32_t pcm = 0, fmts = 0;

		if (dcaps & HDA_AW_FORMAT_OVERRIDE) {
			pcm = hda_get_param_or0(d, pdac,
			                        HDA_PARAM_SUPPORTED_RATES);
			fmts = hda_get_param_or0(d, pdac,
			                         HDA_PARAM_SUPPORTED_FORMATS);
		}
		d->dac_pcm_caps = pcm ? pcm : d->afg_pcm_caps;
		d->dac_fmt_caps = fmts ? fmts : d->afg_fmt_caps;
		/* Channel count is split across bits 15:13 and bit 0, and is
		 * the maximum minus one (7.3.4.6). */
		d->dac_max_chan = (uint8_t)HDA_AW_CHAN_COUNT(dcaps);
	}

	d->dac_nid = pdac;
	d->pin_nid = pins[primary];
	d->have_path = 1;
	hda_codec_bind_stream(d, d->fmt);
	hda_codec_vendor_init(d);

	kprintf("hda: codec %u vid=0x%08x afg=%u dac=%u pin=%u assoc=%u "
	        "dacs=%d tag=%u chan=%u pcm=0x%08x\n",
	        d->codec_addr, (unsigned)d->codec_vid, afg, pdac,
	        pins[primary],
	        (unsigned)HDA_CONFIG_ASSOC(g->cfg[pins[primary]]),
	        d->ndacs, d->stream_tag, d->dac_max_chan, d->dac_pcm_caps);
	rc = 0;
out:
	kfree(g, sizeof(*g));
	return rc;
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
	uint32_t i;
	int rc;

	rc = hda_stream_reset(d);
	if (rc != 0) {
		return rc;
	}

	/*
	 * With RUN set the engine fetches every descriptor in CBL, cyclically.
	 * Slots 0..writes_queued-1 hold what was just staged; every other slot
	 * may still hold audio from before a flush, format change or stop, and
	 * would be replayed -- looping, since the counters park level once the
	 * new data runs out.  Silence them before arming.
	 */
	for (i = d->writes_queued; i < HDA_BDL_ENTRIES; i++) {
		memset(d->chunk[i], 0, HDA_CHUNK_BYTES);
	}
	__sync_synchronize();

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

	/* FIFOS is valid once the format is programmed (3.3.40). */
	d->fifo_size = (uint32_t)hda_read16(d, d->sd_base + HDA_SD_FIFOSIZE) + 1U;
	if (d->fifo_size < 64 || d->fifo_size > HDA_CHUNK_BYTES / 2) {
		d->fifo_size = 256;
	}

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
 *
 * max_slots caps how much copying one call will do.  This runs from the
 * completion handler with local interrupts masked, and unbounded it could
 * memcpy the better part of 128 KiB -- the whole ring -- out of the software
 * FIFO in one interrupt, on top of the 4 KiB memset the handler already does
 * per completion.  One completion frees one slot, so refilling one slot per
 * interrupt keeps up by construction; the producer path passes the full ring
 * because it runs in process context where a long copy is merely slow.
 */
static void hda_feed(hda_dev_t *d, int flush_tail, int max_slots)
{
	if (d->chunk[0] == NULL || d->fifo_buf == NULL) {
		return;
	}
	for (; max_slots > 0; max_slots--) {
		uint32_t played = __atomic_load_n(&d->slots_played,
		                                  __ATOMIC_ACQUIRE);
		/* Signed: completions can run past what was queued. */
		int32_t in_flight = (int32_t)(d->writes_queued - played);
		size_t avail;
		size_t copy_len;
		uint8_t slot;

		/*
		 * Leave one slot between the producer and the engine.  An
		 * HDA stream is cyclic and never stops while RUN is set, so
		 * the controller is always somewhere in the ring; filling all
		 * HDA_BDL_ENTRIES of them means next_idx wraps onto the slot
		 * being DMA'd right now and overwrites it mid-fetch.
		 */
		if (in_flight >= (int32_t)(HDA_BDL_ENTRIES - 1)) {
			break;
		}
		avail = audio_fifo_used(&d->fifo);
		if (avail == 0) {
			break;
		}
		if (avail < HDA_CHUNK_BYTES && !flush_tail) {
			break;   /* wait for a whole slot's worth */
		}

		/*
		 * Near an underrun the engine may already be at or past the
		 * slot next_idx names: completions are credited only at the
		 * next interrupt, so the counters trail it.  Writing there
		 * would overwrite a buffer mid-fetch and skip the start of the
		 * new audio.  Ask LPIB where the engine is, and resume after
		 * it -- one slot further if it is within a FIFO's worth of the
		 * end of its slot and may already be fetching the next.  The
		 * slots skipped play as the silence they hold.
		 */
		if (d->running && in_flight <= 2) {
			uint32_t pos = hda_read32(d, d->sd_base + HDA_SD_LPIB);
			uint32_t eng = (pos / HDA_CHUNK_BYTES) % HDA_BDL_ENTRIES;
			uint32_t ahead = (eng + HDA_BDL_ENTRIES -
			                  played % HDA_BDL_ENTRIES) %
			                 HDA_BDL_ENTRIES;

			if (pos % HDA_CHUNK_BYTES + d->fifo_size >=
			    HDA_CHUNK_BYTES) {
				ahead++;
			}
			if (in_flight <= (int32_t)ahead) {
				d->writes_queued = played + ahead + 1;
				d->next_idx = (uint8_t)(d->writes_queued %
				                        HDA_BDL_ENTRIES);
				continue;
			}
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
		d->data_end = d->writes_queued;
		d->next_idx = (uint8_t)((slot + 1U) % HDA_BDL_ENTRIES);
	}
}

/*
 * Stop the engine and put the ring back to a known position, in process
 * context: spec 4.5.6 keeps the ISR off the stream Control register.  BCIS,
 * FIFOE and DESE are sticky across clearing RUN (3.3.36), so they are
 * cleared once RUN reads back 0; otherwise a completion latched in the last
 * frame would be credited to the next run's fresh counters.  The next start
 * resets the descriptor and the DMA resumes at entry 0, so staging restarts
 * there.  Caller holds feed_lock.
 */
static void hda_ring_reset(hda_dev_t *d)
{
	/* Unconditionally: after a descriptor error running is already 0
	 * while the control register still holds IOCE and friends. */
	(void)hda_stream_stop(d);
	hda_write8(d, d->sd_base + HDA_SD_STS,
	           HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE);
	d->halt_pending  = 0;
	d->next_idx      = 0;
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->data_end      = 0;
}

/* Start the engine on what is staged.  Caller holds feed_lock. */
static void hda_try_start(hda_dev_t *d)
{
	int rc;

	/* Mid format change: the converter may still be on the old one. */
	if (d->binding) {
		return;
	}
	rc = hda_stream_start(d);
	if (rc != 0) {
		/* Not retried on every write: see stream_error. */
		d->stream_error = rc;
		kprintf("hda: output stream will not start\n");
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
	 * known position.
	 */
	if (d->halt_pending) {
		hda_ring_reset(d);
	}

	/*
	 * Always feed, running or not.  This used to sit inside the
	 * !running branch, so once the stream started the producer never
	 * staged another buffer and the ring could only be refilled from the
	 * completion interrupt -- one missed IOC and playback deadlocks
	 * permanently with the software FIFO full and the ring starved.
	 */
	hda_feed(d, 0, HDA_BDL_ENTRIES);

	/*
	 * Start once a real cushion is staged -- including after an
	 * underrun, so a producer paced at real time does not cycle through
	 * stop / reset / restart on one slot at a time.  Audio the producer
	 * stops adding to before the cushion fills is started by the worker
	 * (HDA_IDLE_START_MS) or by drain / SNDCTL_DSP_POST.
	 */
	if (!d->running && d->stream_error == 0) {
		int32_t in_flight;
		in_flight = (int32_t)(d->writes_queued -
		            __atomic_load_n(&d->slots_played, __ATOMIC_ACQUIRE));
		if (in_flight >= (int32_t)HDA_PREBUFFER_SLOTS) {
			hda_try_start(d);
		}
	}

	spinlock_release_irq(&d->feed_lock, flags);
}

/*
 * Pad the FIFO's sub-slot residue out to a whole slot, stage it, and start
 * the engine if anything is staged: the end of a burst, as opposed to the
 * middle of a stream.  Caller holds feed_lock.
 */
static void hda_flush_and_start(hda_dev_t *d)
{
	hda_feed(d, 1, HDA_BDL_ENTRIES);
	if (!d->running && d->stream_error == 0 &&
	    (int32_t)(d->writes_queued -
	              __atomic_load_n(&d->slots_played,
	                              __ATOMIC_ACQUIRE)) > 0) {
		hda_try_start(d);
	}
}

/*
 * Stream worker.  Two jobs need process context and a clock but no
 * producer:
 *
 *  - Staged audio the producer has stopped adding to: a 10 KiB effect
 *    written with the descriptor left open never reaches the prebuffer,
 *    so after HDA_IDLE_START_MS of no writes it is padded and started (or,
 *    with the engine running and the ring dry, the residue padded and
 *    queued).
 *  - A halt the completion handler asked for, when the producer has gone
 *    idle with the descriptor open: otherwise the engine cycles silent
 *    slots forever, an interrupt and a 4 KiB memset per slot.
 *
 * Polls every HDA_WORKER_MS while a stream is active; otherwise sleeps
 * until a write wakes it.
 */
static void hda_worker(void *arg)
{
	hda_dev_t *d = arg;
	uint32_t hz = get_hz();
	uint64_t poll = hz ? (hz * HDA_WORKER_MS + 999U) / 1000U : 1U;
	uint64_t idle = hz ? (hz * HDA_IDLE_START_MS + 999U) / 1000U : 1U;

	for (;;) {
		unsigned long f = spinlock_acquire_irq(&d->feed_lock);
		uint64_t now = get_ticks();
		int32_t in_flight = (int32_t)(d->writes_queued -
		                    __atomic_load_n(&d->slots_played,
		                                    __ATOMIC_ACQUIRE));
		size_t used = audio_fifo_used(&d->fifo);
		int active;

		if (d->halt_pending) {
			hda_ring_reset(d);
			in_flight = 0;
		}
		if (used > 0 || in_flight > 0) {
			if (now - d->last_write >= idle &&
			    (!d->running || in_flight <= 0)) {
				hda_flush_and_start(d);
			}
		}
		active = d->running || audio_fifo_used(&d->fifo) > 0;
		spinlock_release_irq(&d->feed_lock, f);

		(void)sched_sleep_until(&d->worker_chan,
		                        get_ticks() + (active ? poll : hz * 10U));
	}
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
		/*
		 * Acknowledge exactly the bits observed, at once.  Doing it
		 * after the memset and copies below erased a completion that
		 * latched meanwhile without it ever being counted.
		 */
		hda_write8(d, d->sd_base + HDA_SD_STS,
		           sdsts & (HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE |
		                    HDA_SDSTS_DESE));
		if (sdsts & HDA_SDSTS_BCIS) {
			unsigned long f = spinlock_acquire_irq(&d->feed_lock);
			uint32_t done;
			int32_t in_flight;

			/*
			 * A completion latched just before RUN was cleared can
			 * arrive after the stop; it belongs to no stream and
			 * must not be credited to the next one's counters.
			 */
			if (!d->running) {
				spinlock_release_irq(&d->feed_lock, f);
				goto bcis_done;
			}
			/*
			 * Credit the slots the engine has finished with, from
			 * its position rather than one per interrupt.  BCIS is
			 * a single status bit, so completions coalesce, and a
			 * count that drifts from the engine makes the handler
			 * zero, and the feeder fill, the wrong slots.  LPIB
			 * trails the fetch position by at most the FIFO, so a
			 * slot before LPIB's is certainly fetched; the one LPIB
			 * is in is credited at the next completion.
			 */
			done = __atomic_load_n(&d->slots_played,
			                       __ATOMIC_ACQUIRE);
			{
				uint32_t pos = hda_read32(d, d->sd_base +
				                          HDA_SD_LPIB);
				uint32_t eng = (pos / HDA_CHUNK_BYTES) %
				               HDA_BDL_ENTRIES;
				uint32_t n = (eng + HDA_BDL_ENTRIES -
				              done % HDA_BDL_ENTRIES) %
				             HDA_BDL_ENTRIES;
				uint32_t i;

				/* The ring is cyclic and never stops while RUN
				 * is set, so a slot we do not refill plays
				 * again.  Zero what was consumed; hda_feed
				 * below overwrites it if data is waiting. */
				for (i = 0; i < n && d->chunk[0] != NULL; i++) {
					memset(d->chunk[(done + i) %
					                HDA_BDL_ENTRIES], 0,
					       HDA_CHUNK_BYTES);
				}
				__atomic_store_n(&d->slots_played, done + n,
				                 __ATOMIC_RELEASE);
			}
			/* Autonomously refill the slots this completion
			 * freed, so playback survives producer jitter.
			 * Bounded: this runs with interrupts masked. */
			hda_feed(d, 0, HDA_FEED_SLOTS_PER_IRQ);
			in_flight = (int32_t)(d->writes_queued -
			                      __atomic_load_n(&d->slots_played,
			                                      __ATOMIC_ACQUIRE));
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
			 * Keep the counters level on every completion that
			 * finds the ring empty, whatever the software FIFO
			 * holds.  The engine walks the ring whether or not it
			 * was refilled, so with a sub-slot residue left in the
			 * FIFO and the producer paused, slots_played used to
			 * run past writes_queued with nothing to clamp it --
			 * and every later feed saw a negative ring and stopped
			 * for good.  writes_queued is brought up to the engine
			 * (the slots in between played as silence), never
			 * slots_played down, which would desynchronise it.
			 *
			 * Once a whole slot of silence has been consumed after
			 * the last audio, that audio is out of the controller's
			 * FIFO and the stream can be stopped.
			 */
			if (in_flight <= 0) {
				uint32_t sp = __atomic_load_n(&d->slots_played,
				                              __ATOMIC_ACQUIRE);

				d->writes_queued = sp;
				d->next_idx = (uint8_t)(sp % HDA_BDL_ENTRIES);
				if (audio_fifo_used(&d->fifo) == 0 &&
				    (int32_t)(sp - d->data_end) >= 1) {
					d->halt_pending = 1;
				}
			}
			spinlock_release_irq(&d->feed_lock, f);
			sched_wakeup(d);
		}
bcis_done:
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
			/* No completion will follow: wake the writer. */
			sched_wakeup(d);
		}
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
	d->intr_count++;      /* proof-of-life for the INTx routing probe */

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
	/* Writer wait bound: about three slot periods. */
	d->wait_ticks = get_hz() ? (get_hz() * 64U + 999U) / 1000U : 1U;
	if (d->wait_ticks == 0) {
		d->wait_ticks = 1;
	}
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

	/* Stop (RUN verified clear, 4.5.4), drop latched status and reset the
	 * ring counters, so the next stream does not inherit them. */
	hda_ring_reset(d);
	audio_fifo_reset(&d->fifo);
	d->stream_error = 0;

	spinlock_release_irq(&d->feed_lock, flags);
	sched_wakeup(d);

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
	/*
	 * One format change at a time, start to finish: SDnFMT and the
	 * converter's own copy (verb 2h) must describe the same stream, and
	 * two changes interleaving could leave the descriptor on one format
	 * and the converter on the other.
	 */
	mutex_lock(&d->cfg_lock);

	/*
	 * An unchanged format is not a change, running or not.  Gain or
	 * block-size updates arrive here too, and resetting an idle ring
	 * for them threw away PCM already buffered.  SDnFMT is rewritten
	 * from d->fmt at every start anyway.
	 */
	if (fmt == d->fmt) {
		mutex_unlock(&d->cfg_lock);
		return 0;
	}

	flags = spinlock_acquire_irq(&d->feed_lock);

	/*
	 * Stop before touching SDnFMT.  The register is only writable with
	 * the engine idle -- 3.3.38 and 3.3.41 both restrict descriptor
	 * programming to a stopped stream.  Rewriting it underneath a
	 * running engine also left the PCM already queued in the ring to be
	 * played at the new rate and channel count.
	 *
	 * Anything still buffered belongs to the old format, so it goes:
	 * a format change mid-stream is the application telling us the old
	 * audio is finished with.
	 */
	hda_ring_reset(d);
	audio_fifo_reset(&d->fifo);
	d->stream_error = 0;

	/* Remembered because a stream reset clears SDnFMT, and every start
	 * goes through one. */
	d->fmt = fmt;
	hda_write16(d, d->sd_base + HDA_SD_FMT, fmt);
	/* No start until the converter has the new format too. */
	d->binding = 1;

	spinlock_release_irq(&d->feed_lock, flags);
	/* With RUN clear no completion will come to wake a blocked writer. */
	sched_wakeup(d);

	/* The converter has its own copy of the format; leaving it on the
	 * old one makes the codec decode the stream wrongly (wrong rate /
	 * channel count) rather than fall silent, which is worse.  Sent
	 * outside the feed lock because it is a CORB round trip. */
	hda_codec_bind_stream(d, fmt);

	flags = spinlock_acquire_irq(&d->feed_lock);
	d->binding = 0;
	spinlock_release_irq(&d->feed_lock, flags);
	mutex_unlock(&d->cfg_lock);
	return 0;
}

/*
 * Bytes still to be played: the software FIFO plus the ring ahead of the
 * engine's link position.  GETOSPACE-derived figures left the ring out --
 * up to two-thirds of a second at 48 kHz, which is what A/V sync is off by.
 */
static int hda_get_odelay(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	unsigned long f;
	int64_t ring;
	size_t used;

	if (d->fifo_buf == NULL) {
		return 0;
	}
	f = spinlock_acquire_irq(&d->feed_lock);
	used = audio_fifo_used(&d->fifo);
	ring = (int32_t)(d->writes_queued -
	                 __atomic_load_n(&d->slots_played, __ATOMIC_ACQUIRE));
	ring *= HDA_CHUNK_BYTES;
	if (d->running) {
		/* Already through the slot the counters say the engine is in. */
		uint32_t ring_bytes = HDA_BDL_ENTRIES * HDA_CHUNK_BYTES;
		uint32_t base = (d->slots_played % HDA_BDL_ENTRIES) *
		                HDA_CHUNK_BYTES;
		uint32_t pos = hda_read32(d, d->sd_base + HDA_SD_LPIB);

		ring -= (int64_t)((pos + ring_bytes - base) % ring_bytes);
	}
	spinlock_release_irq(&d->feed_lock, f);
	if (ring < 0) {
		ring = 0;
	}
	return (int)(used + (size_t)ring);
}

/* The calling descriptor is O_NONBLOCK (see tty_read_nonblock()). */
static int hda_nonblock(void)
{
	return current_thread && current_thread->io_file &&
	       (current_thread->io_file->f_flag & FNONBLOCK);
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
		if (n > 0) {
			d->last_write = get_ticks();
			if (!d->running) {
				sched_wakeup(&d->worker_chan);
			}
		}

		hda_kick(d);   /* prime / restart; no-op while IRQ feeds */

		/* A descriptor that will not reset cannot play: say so rather
		 * than block forever waiting for completions. */
		if (d->stream_error != 0) {
			return total_consumed ? (int)total_consumed :
			                        d->stream_error;
		}
		if (total_consumed >= len) {
			break;
		}

		/* A non-blocking descriptor takes what fits and returns. */
		if (hda_nonblock()) {
			break;
		}
		if (current_thread) {
			/*
			 * Wait for a completion to free space.  Interruptible,
			 * marked before the space and signal checks so a signal
			 * posted in between is not lost (sched_sleep() rechecks
			 * it before blocking), and bounded by a deadline a few
			 * slot periods out: a wakeup landing between the check
			 * and the block costs at most that, not the scheduler's
			 * 250 ms fallback.  No sleep queue, so nothing is left
			 * linked when the deadline or a signal ends the wait.
			 */
			current_thread->flags |= THREAD_F_INTERRUPTIBLE;
			if ((current_thread->sig_pending &
			     ~current_thread->sig_mask) == 0 &&
			    audio_fifo_free(&d->fifo) == 0) {
				(void)sched_sleep_until(d,
				                        get_ticks() + d->wait_ticks);
			}
			current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
			/* Killable: break the wait on a pending unmasked
			 * signal so the player can be ^C'd / kill(1)ed. */
			if (current_thread->sig_pending &
			    ~current_thread->sig_mask) {
				break;
			}
		} else {
			__asm__ volatile("pause");
		}
	}

	if (total_consumed == 0 && hda_nonblock()) {
		return -EAGAIN;
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
 *
 * Drained means the engine has stopped on its own: the completion handler
 * asks for that only once a whole slot of silence has followed the last
 * audio, and BCIS for the last audio slot only means it reached the
 * controller's FIFO, not the codec -- stopping there clipped the tail.
 *
 * Returns 0 once drained, -EINTR if a signal cut it short, -EIO if the
 * controller stopped making progress or the stream cannot run.
 */
static int hda_drain(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	uint32_t poll;
	uint32_t stall = 0;
	uint32_t last_remaining = 0xFFFFFFFFu;
	uint32_t hz = get_hz();
	uint64_t step = hz ? (hz * HDA_DRAIN_POLL_MS) / 1000U : 1U;
	/*
	 * Padding the residue marks the end of the stream, which only its
	 * producer can declare.  Another thread or process draining while
	 * the owner still writes must wait without splicing silence into
	 * the owner's audio.
	 */
	int is_end = adev->play_owner == NULL ||
	             adev->play_owner == (void *)current_thread;

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
		int stopped;

		/*
		 * End of stream: the residue left in the FIFO is shorter than
		 * a slot and the normal path only queues whole slots, so it
		 * would sit there forever.  Padding it out is correct here --
		 * it really is the end of the audio -- and the engine is
		 * started whatever the prebuffer holds.  The kick retires a
		 * halt the completion handler asked for.
		 */
		hda_kick(d);
		f = spinlock_acquire_irq(&d->feed_lock);
		if (is_end) {
			hda_flush_and_start(d);
		}
		used = audio_fifo_used(&d->fifo);
		in_flight = (int32_t)(d->writes_queued -
		                      __atomic_load_n(&d->slots_played,
		                                      __ATOMIC_ACQUIRE));
		stopped = !d->running;
		spinlock_release_irq(&d->feed_lock, f);

		if (d->stream_error != 0) {
			return d->stream_error;
		}
		if (used == 0 && stopped && in_flight <= 0) {
			return 0;
		}
		if (current_thread &&
		    (current_thread->sig_pending & ~current_thread->sig_mask)) {
			return -EINTR;         /* interrupted -- drop the tail */
		}

		remaining = (uint32_t)used +
		            (uint32_t)(in_flight > 0 ? in_flight : 0) *
		            HDA_CHUNK_BYTES;
		if (remaining < last_remaining) {
			last_remaining = remaining;
			stall = 0;
		} else if (++stall >= HDA_DRAIN_STALL_POLLS) {
			kprintf("hda: drain made no progress; giving up\n");
			return -EIO;           /* controller wedged */
		}

		if (current_thread) {
			(void)sched_sleep_until((void *)d, get_ticks() + step);
		} else {
			for (volatile int i = 0; i < 200000; i++) {
				__asm__ volatile("pause");
			}
		}
	}
	kprintf("hda: drain hit its %u s ceiling\n",
	        (unsigned)(HDA_DRAIN_POLL_MAX * HDA_DRAIN_POLL_MS / 1000U));
	return -EIO;
}

/*
 * SNDCTL_DSP_POST: the application has reached a boundary in its output
 * (the end of an effect, say) and wants what it wrote played now rather
 * than when the prebuffer fills.
 */
static int hda_post(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	unsigned long f;

	if (d->fifo_buf == NULL || d->chunk[0] == NULL) {
		return 0;
	}
	hda_kick(d);
	f = spinlock_acquire_irq(&d->feed_lock);
	hda_flush_and_start(d);
	spinlock_release_irq(&d->feed_lock, f);
	return d->stream_error;
}

static int hda_flush(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	unsigned long flags = spinlock_acquire_irq(&d->feed_lock);
	/* Stop, do not park in reset: asserting SRST and leaving it set wedges
	 * the stream descriptor for every later start.  hda_stream_stop()
	 * waits for RUN to actually drop before we discard the ring state. */
	hda_ring_reset(d);
	audio_fifo_reset(&d->fifo);
	d->stream_error = 0;
	spinlock_release_irq(&d->feed_lock, flags);
	/* With RUN clear no completion will come to wake a blocked writer. */
	sched_wakeup(d);
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
	/*
	 * Playback only.  This claimed CAPTURE, FULLDUPLEX and INDEPENDENT
	 * as well, with hda_ops.read left NULL -- so AUDIO_GETPROPS told
	 * applications they could record, and every read() on /dev/audio0
	 * then failed in the framework.  Nothing here drives an input
	 * stream; the descriptor this driver programs is an output one.
	 *
	 * ac97.c makes the same overstatement with the same NULL read op.
	 */
	return AUDIO_PROP_PLAYBACK;
}

/*
 * Report the software PCM FIFO's free space.  The framework turns this into
 * AUDIO_GETINFO's play.seek (what is still queued) and OSS GETOSPACE; a
 * "fragment" is one DMA chunk.
 */
static int hda_get_ospace(audio_dev_t *adev, int *fragsize, int *fragstotal,
			  int *fragments, int *bytes)
{
	hda_dev_t *d = adev->driver_data;
	size_t freeb;

	if (d->fifo_buf == NULL) {
		return -EINVAL;
	}
	freeb = audio_fifo_free(&d->fifo);
	*fragsize   = (int)HDA_CHUNK_BYTES;
	*fragstotal = (int)(HDA_FIFO_BYTES / HDA_CHUNK_BYTES);
	*fragments  = (int)(freeb / HDA_CHUNK_BYTES);
	*bytes      = (int)freeb;
	return 0;
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
	.get_ospace  = hda_get_ospace,
	.post        = hda_post,
	.get_odelay  = hda_get_odelay,
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
	if (d->msi_enabled) {
		/* Stop the controller signalling before its vector goes. */
		(void)pci_disable_msi(d->pdev);
		d->msi_enabled = 0;
	}
	if (d->irq_claimed) {
		free_irq((unsigned int)d->irq, d);
		d->irq_claimed = 0;
	}
	if (d->msi) {
		irq_free_vector(d->irq);
		d->msi = 0;
		d->irq = -1;
	}
	if (d->intx_routed) {
		/* Mask the I/O APIC input and set INTx-disable, so a line this
		 * driver routed by convention cannot sit asserted with no
		 * handler behind it. */
		pci_unroute_intx(d->pdev, d->irq);
		d->intx_routed = 0;
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

/*
 * Vendor-specific setup that has to happen before any DMA.
 *
 * TCSEL: Intel controllers default to a traffic class other than TC0 on
 * some chipsets, which can be routed to a virtual channel the chipset
 * does not service for audio.  FreeBSD forces TC0 unconditionally on
 * Intel parts and so do we.
 *
 * Snooping: this driver allocates its rings as ordinary write-back
 * cacheable memory, which is only safe if the controller's DMA snoops
 * the caches.  Intel parts do.  ATI, AMD and NVIDIA need a
 * vendor-specific bit set, and if that cannot be done the correct answer
 * is uncacheable DMA memory -- which this kernel has no way to allocate
 * yet.  Warn rather than pretend: a controller reading stale CORB or BDL
 * contents fails in ways that look nothing like a coherency problem.
 */
static void hda_pci_quirks(hda_dev_t *d, pci_device_t *pdev)
{
	static const struct {
		uint16_t vendor;
		uint8_t  reg;     /* 0 = snoops by default, nothing to do */
		uint8_t  mask;
		uint8_t  enable;
	} snoop[] = {
		{ HDA_PCI_VENDOR_INTEL,  0x00, 0x00, 0x00 },
		{ HDA_PCI_VENDOR_ATI,    0x42, 0xf8, 0x02 },
		{ HDA_PCI_VENDOR_AMD,    0x42, 0xf8, 0x02 },
		{ HDA_PCI_VENDOR_NVIDIA, 0x4e, 0xf0, 0x0f },
	};
	uint16_t vendor = pdev->vendor_id;
	size_t i;
	uint8_t v;

	if (vendor == HDA_PCI_VENDOR_INTEL) {
		v = pci_read_config8(pdev->bus, pdev->slot, pdev->func,
		                     HDA_PCI_REG_TCSEL);
		pci_write_config8(pdev->bus, pdev->slot, pdev->func,
		                  HDA_PCI_REG_TCSEL, (uint8_t)(v & 0xF8));
	}

	for (i = 0; i < sizeof(snoop) / sizeof(snoop[0]); i++) {
		if (snoop[i].vendor != vendor) {
			continue;
		}
		if (snoop[i].reg == 0x00) {
			return;          /* coherent without help */
		}
		v = pci_read_config8(pdev->bus, pdev->slot, pdev->func,
		                     snoop[i].reg);
		if ((v & snoop[i].enable) == snoop[i].enable) {
			return;          /* firmware already enabled it */
		}
		v = (uint8_t)((v & snoop[i].mask) | snoop[i].enable);
		pci_write_config8(pdev->bus, pdev->slot, pdev->func,
		                  snoop[i].reg, v);
		v = pci_read_config8(pdev->bus, pdev->slot, pdev->func,
		                     snoop[i].reg);
		if ((v & snoop[i].enable) != snoop[i].enable) {
			kprintf("hda: could not enable PCIe snoop on "
			        "%04x; DMA may read stale data\n", vendor);
		}
		return;
	}

	kprintf("hda: unknown vendor %04x; assuming coherent DMA\n", vendor);
	(void)d;
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
	mutex_init(&d->cfg_lock, "hda_cfg");
	d->pdev = pdev;

	cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
	                        PCI_CONFIG_COMMAND);
	cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	pci_write_config16(pdev->bus, pdev->slot, pdev->func,
	                   PCI_CONFIG_COMMAND, cmd);

	/* Traffic class and cache-snooping, before anything is DMA'd. */
	hda_pci_quirks(d, pdev);

	d->mmio = pci_iomap(pdev, 0, 16384);
	if (d->mmio == NULL) {
		kprintf("hda: failed to map BAR0\n");
		return -ENODEV;
	}
	/*
	 * Prefer MSI.  It goes straight to the local APIC, so it needs
	 * neither the firmware's Interrupt Line byte (0xFF under UEFI) nor
	 * an I/O APIC and a guess at the chipset's INTx routing -- the
	 * PIRQ convention below is admittedly weakest for PCH-internal
	 * functions like this one, and it needs MADT discovery to have
	 * registered an I/O APIC at all.  Every Intel HDA controller since
	 * ICH6 has the capability.  The vector is only claimed here; MSI is
	 * switched on in config space once the handler is registered.
	 */
	d->irq = -1;
	if (pci_find_capability(pdev, PCI_CAP_ID_MSI) != 0) {
		int vec = irq_alloc_vector();

		if (vec >= 0) {
			d->irq = vec;
			d->msi = 1;
		}
	}
	if (!d->msi) {
		d->irq = pci_get_irq(pdev);
	}
	if (!d->msi && d->irq < 0) {
		/*
		 * No firmware-provided line.  On a UEFI/APIC machine that is
		 * the normal case, not a fault: config byte 0x3C is a
		 * PIC-era field the firmware leaves at 0xFF because the real
		 * routing lives in the ACPI _PRT.  Fall back to the
		 * conventional PIRQ swizzle and PROVE it below — if no
		 * interrupt arrives, the attach is abandoned.
		 */
		d->irq = pci_route_intx(pdev);
		if (d->irq >= 0) {
			d->intx_routed = 1;
		}
	}

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

	/* SDI[14:0]; bit 15 is reserved. */
	d->codec_mask = hda_read16(d, HDA_REG_STATESTS) & 0x7FFF;
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
	if (d->msi) {
		if (pci_enable_msi(d->pdev, (uint8_t)d->irq) != 0) {
			kprintf("hda: could not enable MSI\n");
			hda_detach_partial(d);
			return -ENXIO;
		}
		d->msi_enabled = 1;
		kprintf("hda: using MSI vector 0x%x\n", (unsigned)d->irq);
	}

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

	/*
	 * First real conversation with each codec.  STATESTS records which
	 * SDI lines signalled during enumeration; it does not promise every
	 * one will answer -- a display codec behind a powered-down graphics
	 * well, or a phantom slot, may not.  Probe each, drop the silent ones,
	 * and give up only if none answers.
	 */
	{
		int i, first = -1;
		uint32_t vid;

		vendor_id = 0;
		for (i = 0; i < HDA_MAX_CODECS; i++) {
			if ((d->codec_mask & (1u << i)) == 0) {
				continue;
			}
			if (hda_try_verb(d, (uint8_t)i, 0,
			                 HDA_VERB_GET_PARAMETER,
			                 HDA_PARAM_VENDOR_ID, &vid) != 0) {
				kprintf("hda: codec %d did not answer; "
				        "skipped\n", i);
				d->codec_mask &= (uint16_t)~(1u << i);
				continue;
			}
			if (first < 0) {
				first = i;
				vendor_id = vid;
			}
		}
		if (first < 0) {
			kprintf("hda: no codec answered\n");
			hda_detach_partial(d);
			return -EIO;
		}
		d->codec_addr = (uint8_t)first;
	}

	/*
	 * If the IRQ came from pci_route_intx() rather than the firmware,
	 * the GSI is a guess from the conventional PIRQ swizzle and has to
	 * be proven before anything relies on it.  Nothing so far has: the
	 * synchronous verb path polls RIRBWP, so the codec answered above
	 * whether or not interrupts work.
	 *
	 * Provoke one deliberately.  RINTCNT is normally half the ring so
	 * the boot-time graph walk does not take an interrupt per verb;
	 * drop it to 1 for a single exchange, which must then raise the
	 * RIRB response interrupt, and put it back.  3.3.28 wants the DMA
	 * engine stopped while this field changes.
	 *
	 * A silent probe means the guess was wrong.  Give the line back and
	 * refuse the attach — the alternative is a /dev/audio that accepts
	 * writes and never completes a buffer, since this driver refills
	 * the ring from the completion path.
	 *
	 * MSI gets the same proof.  It is not a guess, but it is the first
	 * interrupt path this driver takes on real UEFI hardware, and a
	 * silent one would wedge the same way.
	 */
	if (d->intx_routed || d->msi) {
		uint32_t before = d->intr_count;
		uint32_t dummy;
		unsigned int budget;

		hda_write8(d, HDA_REG_RIRBCTL, 0);
		hda_write16(d, HDA_REG_RINTCNT, 1);
		hda_write8(d, HDA_REG_RIRBCTL,
		           HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINTCTL);

		(void)hda_try_verb(d, d->codec_addr, 0, HDA_VERB_GET_PARAMETER,
		                   HDA_PARAM_VENDOR_ID, &dummy);
		for (budget = 0; budget < HDA_VERB_TIMEOUT; budget++) {
			if (d->intr_count != before) {
				break;
			}
		}

		hda_write8(d, HDA_REG_RIRBCTL, 0);
		hda_write16(d, HDA_REG_RINTCNT, (uint16_t)(d->rirb_entries / 2));
		hda_write8(d, HDA_REG_RIRBCTL,
		           HDA_RIRBCTL_RUN | HDA_RIRBCTL_RINTCTL);

		if (d->intr_count == before) {
			if (d->msi) {
				kprintf("hda: no MSI arrived; not attaching\n");
			} else {
				kprintf("hda: no interrupt on the routed GSI; "
				        "the PIRQ convention does not hold "
				        "for this device — not attaching\n");
			}
			/* Disarm before dropping the handler: a level-triggered
			 * INTx with nobody to acknowledge it is the wedge the
			 * note above describes. */
			hda_write32(d, HDA_REG_INTCTL, 0);
			hda_detach_partial(d);
			return -ENXIO;
		}
		kprintf("hda: %s verified (%u interrupt(s))\n",
		        d->msi ? "MSI" : "routed GSI",
		        d->intr_count - before);
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
	/* Completions and stop paths sched_wakeup(d): poll can wait on it. */
	d->audio.wait_chan = d;
	/* A stereo converter gets stereo even for a mono stream (which would
	 * otherwise play on the left only; the framework duplicates it), and
	 * more channels than the converter takes are folded down.  A mono
	 * converter's minimum is one: a minimum above the maximum made every
	 * negotiation fail. */
	d->audio.hw_chan_max = d->dac_max_chan ? d->dac_max_chan : 2;
	d->audio.hw_chan_min = d->audio.hw_chan_max < 2 ?
	                       d->audio.hw_chan_max : 2;
	snprintf(d->audio.name, sizeof(d->audio.name), "hda");
	if (audio_register_device(&d->audio) != 0) {
		hda_detach_partial(d);
		return -EBUSY;
	}

	hda_device_count++;
	{
		thread_t *wt = NULL;

		if (kthread_create(hda_worker, d, &wt, "hda") != 0) {
			kprintf("hda: no stream worker; short writes start only "
			        "once the prebuffer fills\n");
		}
	}
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
