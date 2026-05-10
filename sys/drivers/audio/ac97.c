/*
 * ac97.c - AC'97 audio controller driver.
 *
 * One-shot playback path: each audio_dev_ops_t.write() copies user data
 * into a coherent DMA buffer, builds a BDL entry, advances LVI, and
 * starts the bus master.  IRQ handler simply acks; full ring-buffered
 * streaming with per-block IOC and userspace blocking is left as a
 * TODO once mmap and per-process audio buffering arrive.
 */

#include "ac97.h"
#include "audio.h"

#include <kern/console.h>
#include <kern/device.h>
#include <kern/pci.h>
#include <kern/sched.h>
#include "../../kern/sleepq.h"
#include <sys/audioio.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <arch/x86-common/io.h>
#include <arch/i386/intr.h>
#include <stdio.h>
#include <string.h>

#define AC97_PCI_CLASS_MULTIMEDIA  0x04
#define AC97_PCI_SUBCLASS_AUDIO    0x01

#define AC97_CHUNK_BYTES           4096U  /* per-BDL-entry buffer size */
#define AC97_DEFAULT_RATE          48000U

/* ------------------------------------------------------------------- */
/* Pure helpers (also reachable from host tests)                       */
/* ------------------------------------------------------------------- */

void ac97_build_bdl_entry(ac97_bdl_entry_t *entry, uint32_t buf_phys,
                          uint16_t samples, int ioc)
{
	if (entry == NULL) {
		return;
	}
	entry->buf_phys = buf_phys;
	entry->samples  = samples;
	entry->flags    = (uint16_t)(ioc ? AC97_BDL_F_IOC : 0);
}

uint16_t ac97_mixer_volume(uint8_t left, uint8_t right, int mute)
{
	uint16_t v;

	if (left > 0x3F)  left = 0x3F;
	if (right > 0x3F) right = 0x3F;
	v = (uint16_t)((left & 0x3F) | ((right & 0x3F) << 8));
	if (mute) {
		v |= 0x8000;
	}
	return v;
}

uint16_t ac97_encode_rate(uint32_t hz, int has_vra)
{
	if (!has_vra) {
		return 48000;
	}
	if (hz < 8000)   hz = 8000;
	if (hz > 48000)  hz = 48000;
	return (uint16_t)hz;
}

/* ------------------------------------------------------------------- */
/* Driver state                                                        */
/* ------------------------------------------------------------------- */

typedef struct ac97_dev {
	pci_device_t   *pdev;
	uint16_t        nambar;       /* I/O port base for mixer */
	uint16_t        nabmbar;      /* I/O port base for bus master */
	uint16_t        ext_audio_id;
	int             has_vra;
	int             irq;

	ac97_bdl_entry_t *bdl;        /* 32-entry BDL (page-aligned) */
	dma_addr_t      bdl_phys;
	void           *chunk_buf;    /* pool of chunk buffers */
	dma_addr_t      chunk_phys;
	size_t          chunk_count;
	uint8_t         next_idx;     /* next BDL slot to fill */
	uint8_t         lvi;          /* most recently programmed Last Valid */

	/*
	 * Back-pressure counters maintained by the kernel — independent
	 * of the controller's CIV register, which under QEMU runs ahead
	 * of LVI in ways that broke our previous CIV-based ring math.
	 *
	 *   writes_queued = total slots we've programmed (monotonic)
	 *   slots_played  = total BCIS interrupts seen   (monotonic)
	 *
	 * "In flight" = writes_queued - slots_played.  Cap at
	 * BDL_ENTRIES - 1 to leave one slot of breathing room for the
	 * controller's PIV prefetch.
	 */
	volatile uint32_t writes_queued;
	volatile uint32_t slots_played;

	int             running;      /* RPBM has been asserted; don't keep
	                               * rewriting CR every queue */

	audio_dev_t     audio;
} ac97_dev_t;

#define AC97_MAX_CONTROLLERS 2
static ac97_dev_t ac97_devices[AC97_MAX_CONTROLLERS];
static int ac97_device_count;

/* ------------------------------------------------------------------- */
/* Low-level register access                                           */
/* ------------------------------------------------------------------- */

static uint16_t ac97_mixer_read(ac97_dev_t *d, uint16_t reg)
{
	return inw((uint16_t)(d->nambar + reg));
}

static void ac97_mixer_write(ac97_dev_t *d, uint16_t reg, uint16_t val)
{
	outw((uint16_t)(d->nambar + reg), val);
}

static void ac97_bm_write8(ac97_dev_t *d, uint16_t reg, uint8_t val)
{
	outb((uint16_t)(d->nabmbar + reg), val);
}

static uint16_t ac97_bm_read16(ac97_dev_t *d, uint16_t reg)
{
	return inw((uint16_t)(d->nabmbar + reg));
}

static void ac97_bm_write16(ac97_dev_t *d, uint16_t reg, uint16_t val)
{
	outw((uint16_t)(d->nabmbar + reg), val);
}

static void ac97_bm_write32(ac97_dev_t *d, uint16_t reg, uint32_t val)
{
	outl((uint16_t)(d->nabmbar + reg), val);
}

/* ------------------------------------------------------------------- */
/* IRQ                                                                 */
/* ------------------------------------------------------------------- */

static int ac97_irq_handler(unsigned int irq, void *dev_id, void *frame)
{
	ac97_dev_t *d = dev_id;
	uint16_t sr;

	(void)irq;
	(void)frame;
	if (d == NULL) {
		return 0;
	}
	sr = ac97_bm_read16(d, AC97_BM_PO_BASE + AC97_BM_SR);
	if (sr & (AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE)) {
		/* Ack the latched status bits.  BCIS fires once per BDL slot
		 * with IOC=1; track it as our "slot drained" event so the
		 * write path's back-pressure can release. */
		if (sr & AC97_SR_BCIS) {
			__atomic_add_fetch(&d->slots_played, 1, __ATOMIC_ACQ_REL);
			/* Wake any producer that's blocked in ac97_write
			 * waiting for a ring slot to free up.  Cheap when
			 * nobody is sleeping (sleepq_wake_all is a no-op
			 * for empty channels) so safe to call every BCIS. */
			(void)sleepq_wake_all(d);
		}
		ac97_bm_write16(d, AC97_BM_PO_BASE + AC97_BM_SR,
		                sr & (AC97_SR_BCIS | AC97_SR_LVBCI |
		                      AC97_SR_FIFOE));
	}
	return 1;
}

/* ------------------------------------------------------------------- */
/* Backend ops                                                         */
/* ------------------------------------------------------------------- */

static int ac97_open(audio_dev_t *adev, int mode)
{
	(void)adev;
	(void)mode;
	return 0;
}

static int ac97_close(audio_dev_t *adev)
{
	ac97_dev_t *d = adev->driver_data;

	/* Stop the bus master if anything was running. */
	ac97_bm_write8(d, AC97_BM_PO_BASE + AC97_BM_CR, 0);

	/* Drop any latched status bits so a stale BCIS doesn't bump the
	 * NEXT stream's slots_played at open time. */
	ac97_bm_write16(d, AC97_BM_PO_BASE + AC97_BM_SR,
	                AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE);

	/* Reset the ring back-pressure counters and BDL position.
	 * Otherwise the next open inherits writes_queued from this
	 * stream while slots_played is stuck (no more IRQs after CR=0),
	 * making the back-pressure spin in ac97_write think the ring is
	 * permanently full and never accept new data. */
	d->writes_queued = 0;
	d->slots_played  = 0;
	d->next_idx      = 0;
	d->lvi           = 0;
	d->running       = 0;
	return 0;
}

static int ac97_set_params(audio_dev_t *adev, audio_info_t *info)
{
	ac97_dev_t *d = adev->driver_data;
	extern int kprintf(const char *fmt, ...);
	uint16_t enc = ac97_encode_rate(info->play.sample_rate, d->has_vra);
	uint16_t back;

	/* Re-assert VRA before each rate change — some codecs only
	 * accept rate writes while the VRA bit is freshly asserted. */
	if (d->has_vra) {
		ac97_mixer_write(d, AC97_EXT_AUDIO_CTRL, AC97_EXT_VRA);
	}

	ac97_mixer_write(d, AC97_PCM_FRONT_DAC_RATE, enc);
	back = ac97_mixer_read(d, AC97_PCM_FRONT_DAC_RATE);
	if (back != enc) {
		kprintf("ac97: WARN: rate %u Hz rejected (wrote 0x%04x, "
		        "readback 0x%04x); falling back to codec rate\n",
		        info->play.sample_rate, enc, back);
		/* Report back what the codec accepted so audio_info matches
		 * reality. */
		info->play.sample_rate = back;
	}

	ac97_mixer_write(d, AC97_PCM_LR_ADC_RATE,
	                 ac97_encode_rate(info->record.sample_rate, d->has_vra));
	/*
	 * AC'97 mixer is 6-bit attenuation per channel; treat audio gain
	 * (0..255) as a linear approximation.  Higher gain = lower
	 * attenuation, hence the inversion.
	 */
	{
		uint8_t att = (uint8_t)((255 - info->play.gain) >> 2);
		ac97_mixer_write(d, AC97_MASTER_VOLUME,
		                 ac97_mixer_volume(att, att, 0));
		ac97_mixer_write(d, AC97_PCM_OUT_VOLUME,
		                 ac97_mixer_volume(att, att, 0));
	}
	return 0;
}

static int ac97_write(audio_dev_t *adev, const void *buf, size_t len)
{
	ac97_dev_t *d = adev->driver_data;
	const uint8_t *src = buf;
	size_t total_consumed = 0;
	uint8_t  slot;
	uint16_t samples;
	size_t copy_len;
	int started_or_running = 0;

	if (len == 0 || d->chunk_buf == NULL) {
		return (int)len;
	}

	/*
	 * Drain as much of the userspace buffer as we can in one syscall.
	 * Each iteration queues one BDL slot (up to AC97_CHUNK_BYTES =
	 * 4 KB ≈ 23 ms at 44.1 kHz stereo 16-bit).  The ring holds 32
	 * slots, so back-to-back queuing here stages ~720 ms of audio
	 * without bouncing through a fresh write() syscall per slot —
	 * which is what produced the choppy playback.
	 */
	while (total_consumed < len) {
		size_t remaining = len - total_consumed;

		/*
		 * Back-pressure based on our own counters, not the controller's
		 * CIV.  CIV under QEMU runs ahead of LVI in ways the spec doesn't
		 * describe, breaking ring math; but BCIS interrupts (one per IOC
		 * BDL entry completed) are reliable.  We track:
		 *   writes_queued = total slots we've programmed
		 *   slots_played  = total BCIS the IRQ handler has counted
		 * In-flight = writes_queued - slots_played.  Cap at BDL-1 so the
		 * controller's PIV prefetch always has a slot to grab.
		 */
		for (;;) {
			uint32_t played = __atomic_load_n(&d->slots_played,
			                                  __ATOMIC_ACQUIRE);
			uint32_t in_flight = d->writes_queued - played;
			if (in_flight < (AC97_BDL_ENTRIES - 1)) {
				break;
			}
			/*
			 * Ring full — block until the IRQ handler bumps
			 * slots_played and wakes us.  We use the device
			 * pointer as the sleep channel; the wake side is
			 * `sleepq_wake_all(d)` in ac97_irq_handler when it
			 * sees BCIS.
			 *
			 * Race-free pattern: enqueue THEN re-check the
			 * condition with acquire ordering.  If the IRQ
			 * fires between our enqueue and our read of
			 * slots_played, the read sees the bumped value and
			 * we drop straight through without sleeping.  If we
			 * sleep, the wakeup is guaranteed to come from a
			 * later IRQ (slots_played still has not advanced
			 * past in_flight, so the next BCIS will).
			 */
			if (current_thread) {
				sleepq_add(d, current_thread);
				played = __atomic_load_n(&d->slots_played,
				                         __ATOMIC_ACQUIRE);
				in_flight = d->writes_queued - played;
				if (in_flight < (AC97_BDL_ENTRIES - 1)) {
					sleepq_wake_all(d);  /* dequeue */
					break;
				}
				sched_sleep(d);
			} else {
				/* No thread context — fall back to spin. */
				__asm__ volatile("pause");
			}
		}

		copy_len = (remaining > AC97_CHUNK_BYTES)
		           ? AC97_CHUNK_BYTES : remaining;

		slot = d->next_idx;
		memcpy((uint8_t *)d->chunk_buf + (size_t)slot * AC97_CHUNK_BYTES,
		       src + total_consumed, copy_len);

		samples = (uint16_t)(copy_len / 2);  /* 16-bit samples */
		ac97_build_bdl_entry(&d->bdl[slot],
		                     (uint32_t)d->chunk_phys +
		                     (uint32_t)slot * AC97_CHUNK_BYTES,
		                     samples, 1 /* IOC */);
		/* Set BUP (Buffer Underrun Policy): if the controller drains
		 * faster than we refill, it emits silence past the buffer
		 * instead of hard-halting and forcing a CR restart. */
		d->bdl[slot].flags |= AC97_BDL_F_BUP;

		/* Make the BDL store globally visible BEFORE bumping LVI —
		 * the BM may fetch this entry the instant LVI changes. */
		__sync_synchronize();

		d->lvi = slot;
		ac97_bm_write8(d, AC97_BM_PO_BASE + AC97_BM_LVI, d->lvi);

		d->writes_queued++;
		d->next_idx = (uint8_t)((slot + 1U) % AC97_BDL_ENTRIES);
		total_consumed += copy_len;
		started_or_running = 1;
	}

	/* Start the bus master.  Avoid re-writing CR while RPBM=1
	 * (perturbs the engine and broke QEMU CIV reporting in earlier
	 * iterations) — but DO restart if the controller has halted
	 * (DCH=1).  That happens after a previous cat finished: the BM
	 * drains through LVI, fires LVBCI, and clears RPBM.  Without a
	 * re-arm here the second cat queues bytes that never play. */
	if (started_or_running && d->running) {
		uint16_t sr = ac97_bm_read16(d, AC97_BM_PO_BASE + AC97_BM_SR);
		if (sr & AC97_SR_DCH) {
			/* Ack any latched terminal-reach bits before restart. */
			ac97_bm_write16(d, AC97_BM_PO_BASE + AC97_BM_SR,
			                AC97_SR_BCIS | AC97_SR_LVBCI |
			                AC97_SR_FIFOE);
			d->running = 0;
		}
	}
	/* Pre-buffer before kicking the bus master so the producer has
	 * runway.  Each BDL slot covers ~23 ms at 44.1 kHz stereo 16-bit;
	 * waiting for AC97_PREBUFFER_SLOTS gives ~185 ms of head-start
	 * before the controller starts draining.  Once the BM is running
	 * (d->running stays set across writes), this branch is a no-op. */
#define AC97_PREBUFFER_SLOTS 8U
	if (started_or_running && !d->running) {
		uint32_t in_flight = d->writes_queued -
		                     __atomic_load_n(&d->slots_played,
		                                     __ATOMIC_ACQUIRE);
		if (in_flight >= AC97_PREBUFFER_SLOTS) {
			ac97_bm_write8(d, AC97_BM_PO_BASE + AC97_BM_CR,
			               AC97_CR_RPBM | AC97_CR_IOCE);
			d->running = 1;
		}
	}

	return (int)total_consumed;
}

static int ac97_drain(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static int ac97_flush(audio_dev_t *adev)
{
	ac97_dev_t *d = adev->driver_data;

	ac97_bm_write8(d, AC97_BM_PO_BASE + AC97_BM_CR, AC97_CR_RR);
	d->next_idx = 0;
	d->lvi = 0;
	return 0;
}

static void ac97_get_devinfo(audio_dev_t *adev, audio_device_t *out)
{
	ac97_dev_t *d = adev->driver_data;

	memset(out, 0, sizeof(*out));
	snprintf(out->name, sizeof(out->name), "ac97");
	snprintf(out->version, sizeof(out->version), "1.0");
	snprintf(out->config, sizeof(out->config), "%04x:%04x",
	         d->pdev != NULL ? d->pdev->vendor_id : 0,
	         d->pdev != NULL ? d->pdev->device_id : 0);
}

static int ac97_get_props(audio_dev_t *adev)
{
	(void)adev;
	return AUDIO_PROP_PLAYBACK | AUDIO_PROP_CAPTURE |
	       AUDIO_PROP_FULLDUPLEX | AUDIO_PROP_INDEPENDENT;
}

static audio_dev_ops_t ac97_ops = {
	.open        = ac97_open,
	.close       = ac97_close,
	.write       = ac97_write,
	.read        = NULL,
	.set_params  = ac97_set_params,
	.drain       = ac97_drain,
	.flush       = ac97_flush,
	.get_devinfo = ac97_get_devinfo,
	.get_props   = ac97_get_props,
};

/* ------------------------------------------------------------------- */
/* Discovery / init                                                    */
/* ------------------------------------------------------------------- */

static uint16_t ac97_pci_io_bar(pci_device_t *pdev, int bar)
{
	uint32_t v = pci_read_config32(pdev->bus, pdev->slot, pdev->func,
	                               (uint16_t)(0x10 + bar * 4));
	return (uint16_t)(v & ~0x3U);
}

static int ac97_attach(pci_device_t *pdev)
{
	ac97_dev_t *d;
	uint16_t cmd;
	uint32_t glob_sta;
	uint64_t spin_budget;

	if (ac97_device_count >= AC97_MAX_CONTROLLERS) {
		return -EBUSY;
	}
	d = &ac97_devices[ac97_device_count];
	memset(d, 0, sizeof(*d));
	d->pdev = pdev;

	cmd = pci_read_config16(pdev->bus, pdev->slot, pdev->func,
	                        PCI_CONFIG_COMMAND);
	cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
	pci_write_config16(pdev->bus, pdev->slot, pdev->func,
	                   PCI_CONFIG_COMMAND, cmd);

	d->nambar  = ac97_pci_io_bar(pdev, 0);
	d->nabmbar = ac97_pci_io_bar(pdev, 1);
	d->irq     = pci_get_irq(pdev);

	if (d->nambar == 0 || d->nabmbar == 0) {
		kprintf("ac97: missing I/O BAR; skipping\n");
		return -ENODEV;
	}

	/* Cold reset.  Some controllers latch the reset bit; spin until the
	 * primary codec reports ready or we exhaust a deliberately small
	 * budget so we don't wedge boot. */
	ac97_bm_write32(d, AC97_GLOB_CNT, AC97_GLOB_CNT_COLD_RESET);
	spin_budget = 100000;
	while (spin_budget-- > 0) {
		glob_sta = inl((uint16_t)(d->nabmbar + AC97_GLOB_STA));
		if (glob_sta & AC97_GLOB_STA_PCR) {
			break;
		}
	}

	/* Probe variable-rate audio support. */
	d->ext_audio_id = ac97_mixer_read(d, AC97_EXT_AUDIO_ID);
	d->has_vra = (d->ext_audio_id & AC97_EXT_VRA) != 0;
	if (d->has_vra) {
		ac97_mixer_write(d, AC97_EXT_AUDIO_CTRL, AC97_EXT_VRA);
	}

	/*
	 * Allocate the BDL ring (one page) and a contiguous chunk pool
	 * (32 × AC97_CHUNK_BYTES = 128 KB).  Keeping both physically
	 * contiguous keeps each BDL entry's buf_phys arithmetic trivial.
	 */
	d->bdl = dma_alloc_coherent(sizeof(ac97_bdl_entry_t) * AC97_BDL_ENTRIES,
	                            &d->bdl_phys);
	if (d->bdl == NULL) {
		return -ENOMEM;
	}
	memset(d->bdl, 0, sizeof(ac97_bdl_entry_t) * AC97_BDL_ENTRIES);

	d->chunk_count = AC97_BDL_ENTRIES;
	d->chunk_buf = dma_alloc_coherent(d->chunk_count * AC97_CHUNK_BYTES,
	                                  &d->chunk_phys);
	if (d->chunk_buf == NULL) {
		dma_free_coherent(d->bdl,
		                  sizeof(ac97_bdl_entry_t) * AC97_BDL_ENTRIES);
		d->bdl = NULL;
		return -ENOMEM;
	}

	/*
	 * Reset the PCM-out engine, then point it at the BDL.  RR
	 * (Reset Registers) clears CIV, LVI, PIV, PICB, SR.  Wait briefly
	 * for the bit to self-clear, then explicitly ack any latched SR
	 * status bits before pointing BDBAR at our table — otherwise a
	 * stale BCIS from a previous boot can fire as soon as IRQs are
	 * unmasked, prematurely bumping our slots_played counter and
	 * desyncing back-pressure.
	 */
	ac97_bm_write8(d, AC97_BM_PO_BASE + AC97_BM_CR, AC97_CR_RR);
	for (int spin = 0; spin < 100000; spin++) {
		uint8_t cr = inb((uint16_t)(d->nabmbar + AC97_BM_PO_BASE + AC97_BM_CR));
		if ((cr & AC97_CR_RR) == 0) break;
	}
	ac97_bm_write16(d, AC97_BM_PO_BASE + AC97_BM_SR,
	                AC97_SR_BCIS | AC97_SR_LVBCI | AC97_SR_FIFOE);
	ac97_bm_write8(d, AC97_BM_PO_BASE + AC97_BM_LVI, 0);

	/* BDL must be written before BDBAR points at it. */
	__sync_synchronize();
	ac97_bm_write32(d, AC97_BM_PO_BASE + AC97_BM_BDBAR,
	                (uint32_t)d->bdl_phys);

	if (d->irq >= 0) {
		(void)request_irq((unsigned int)d->irq, ac97_irq_handler,
		                  0, "ac97", d);
	}

	/* Hand off to the audio framework. */
	d->audio.ops = &ac97_ops;
	d->audio.driver_data = d;
	snprintf(d->audio.name, sizeof(d->audio.name), "ac97");
	if (audio_register_device(&d->audio) != 0) {
		dma_free_coherent(d->chunk_buf, d->chunk_count * AC97_CHUNK_BYTES);
		dma_free_coherent(d->bdl,
		                  sizeof(ac97_bdl_entry_t) * AC97_BDL_ENTRIES);
		d->bdl = NULL;
		d->chunk_buf = NULL;
		return -EBUSY;
	}

	ac97_device_count++;
	kprintf("ac97: %04x:%04x bound nambar=0x%x nabmbar=0x%x irq=%d vra=%d\n",
	        pdev->vendor_id, pdev->device_id, d->nambar, d->nabmbar,
	        d->irq, d->has_vra);
	return 0;
}

void ac97_init(void)
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
		if (cls != AC97_PCI_CLASS_MULTIMEDIA ||
		    sub != AC97_PCI_SUBCLASS_AUDIO) {
			continue;
		}
		(void)ac97_attach(pdev);
	}
}
