/*
 * hda.c - Intel HDA controller driver.
 *
 * Discovers HDA controllers on the PCI bus, performs the standard
 * controller reset, brings up the CORB / RIRB ring buffers for codec
 * communication, and registers the first detected codec's audio
 * function group as an audio_dev_t.
 *
 * Like the AC'97 driver this is a one-shot playback path: the write()
 * callback copies user PCM into a coherent DMA buffer, builds a single
 * BDL entry, programs stream descriptor 0 (output stream 0 by spec
 * convention), and starts the engine.  Full ring-buffered streaming
 * with IRQ-driven refill belongs to a follow-up commit.
 */

#include "hda.h"
#include "audio.h"

#include <kern/console.h>
#include <kern/device.h>
#include <kern/pci.h>
#include <sys/audioio.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <stdio.h>
#include <string.h>

#define HDA_PCI_CLASS_MULTIMEDIA   0x04
#define HDA_PCI_SUBCLASS_HDA       0x03

#define HDA_CORB_ENTRIES           256
#define HDA_RIRB_ENTRIES           256
#define HDA_BDL_ENTRIES            32
#define HDA_CHUNK_BYTES            4096U
#define HDA_DEFAULT_RATE           48000U

/* ------------------------------------------------------------------- */
/* Pure helpers (also reachable from host tests)                       */
/* ------------------------------------------------------------------- */

uint32_t hda_pack_verb(uint8_t cad, uint8_t nid, uint16_t verb,
                       uint16_t payload)
{
	uint32_t v = 0;

	v |= ((uint32_t)(cad & 0x0F)) << 28;
	v |= ((uint32_t)nid) << 20;
	if (verb >= 0xF00) {
		/* Long form: 12-bit verb + 8-bit data. */
		v |= ((uint32_t)(verb & 0x0FFF)) << 8;
		v |= (uint32_t)(payload & 0x00FF);
	} else {
		/* Short form: 4-bit verb + 16-bit data.  Encoded with the
		 * verb id occupying bits 16..19 of the 20-bit verb field. */
		v |= ((uint32_t)(verb & 0xF00)) << 8;
		v |= (uint32_t)(payload & 0xFFFF);
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

	hda_bdl_entry_t *bdl;
	dma_addr_t       bdl_phys;
	void            *chunk_buf;
	dma_addr_t       chunk_phys;
	uint8_t          stream_tag;
	uint8_t          next_idx;

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
	uint16_t budget;
	uint16_t target_wp;

	wp = (uint16_t)((d->corb_wp + 1) % HDA_CORB_ENTRIES);
	d->corb[wp] = encoded;
	hda_write16(d, HDA_REG_CORBWP, wp);
	d->corb_wp = wp;

	target_wp = wp;
	for (budget = 0; budget < 0x8000; budget++) {
		uint16_t rwp = hda_read16(d, HDA_REG_RIRBWP) & 0xFF;
		if (rwp == target_wp) {
			uint64_t resp = d->rirb[rwp];
			d->rirb_rp = rwp;
			return (uint32_t)resp;
		}
	}
	return 0;
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
	/* ACK output stream 0 status if it fired.  BCIS = buffer
	 * completion (one IOC-marked BDL slot drained); track for the
	 * write-path back-pressure. */
	sdsts = hda_read8(d, HDA_SD_BASE + HDA_SD_STS);
	if (sdsts & (HDA_SDSTS_BCIS | HDA_SDSTS_FIFOE | HDA_SDSTS_DESE)) {
		if (sdsts & HDA_SDSTS_BCIS) {
			__atomic_add_fetch(&d->slots_played, 1,
			                   __ATOMIC_ACQ_REL);
		}
		hda_write8(d, HDA_SD_BASE + HDA_SD_STS,
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

	d->chunk_buf = dma_alloc_coherent((size_t)HDA_BDL_ENTRIES *
	                                  HDA_CHUNK_BYTES,
	                                  &d->chunk_phys);
	if (d->chunk_buf == NULL) {
		dma_free_coherent(d->bdl,
		                  HDA_BDL_ENTRIES * sizeof(hda_bdl_entry_t));
		d->bdl = NULL;
		return -ENOMEM;
	}

	d->stream_tag = 1;   /* tag 0 is reserved per spec */
	d->next_idx = 0;

	/* Reset stream descriptor 0 (output stream 0). */
	hda_write32(d, HDA_SD_BASE + HDA_SD_CTL, HDA_SDCTL_SRST);
	{
		int budget;
		for (budget = 0; budget < 1000; budget++) {
			if (hda_read32(d, HDA_SD_BASE + HDA_SD_CTL) &
			    HDA_SDCTL_SRST) {
				break;
			}
		}
	}
	hda_write32(d, HDA_SD_BASE + HDA_SD_CTL, 0);

	hda_write32(d, HDA_SD_BASE + HDA_SD_BDPL, (uint32_t)d->bdl_phys);
	hda_write32(d, HDA_SD_BASE + HDA_SD_BDPU, 0);
	hda_write16(d, HDA_SD_BASE + HDA_SD_LVI, 0);
	hda_write32(d, HDA_SD_BASE + HDA_SD_CBL, 0);
	hda_write16(d, HDA_SD_BASE + HDA_SD_FMT,
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

	hda_write32(d, HDA_SD_BASE + HDA_SD_CTL, 0);
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
	hda_write16(d, HDA_SD_BASE + HDA_SD_FMT, fmt);
	return 0;
}

static int hda_write(audio_dev_t *adev, const void *buf, size_t len)
{
	hda_dev_t *d = adev->driver_data;
	size_t copy_len;
	uint8_t slot;
	uint32_t ctl;

	if (len == 0 || d->chunk_buf == NULL) {
		return (int)len;
	}
	copy_len = (len > HDA_CHUNK_BYTES) ? HDA_CHUNK_BYTES : len;

	/*
	 * Back-pressure: cap in-flight slots at BDL_ENTRIES-1 so the
	 * controller always has at least one new slot to prefetch.
	 * IRQ handler bumps slots_played on each BCIS; we bump
	 * writes_queued after committing this slot.
	 */
	for (;;) {
		uint32_t played = __atomic_load_n(&d->slots_played,
		                                  __ATOMIC_ACQUIRE);
		uint32_t in_flight = d->writes_queued - played;
		if (in_flight < (HDA_BDL_ENTRIES - 1)) {
			break;
		}
		__asm__ volatile("pause");
	}

	slot = d->next_idx;
	memcpy((uint8_t *)d->chunk_buf + (size_t)slot * HDA_CHUNK_BYTES,
	       buf, copy_len);

	hda_build_bdl_entry(&d->bdl[slot],
	                    (uint64_t)d->chunk_phys +
	                    (uint64_t)slot * HDA_CHUNK_BYTES,
	                    (uint32_t)copy_len, 1 /* IOC */);

	/* Make the BDL store globally visible before LVI / CBL update —
	 * the controller may fetch this entry the moment LVI changes. */
	__sync_synchronize();

	hda_write16(d, HDA_SD_BASE + HDA_SD_LVI, slot);
	/* CBL is the cyclic buffer length — the total byte count the
	 * controller streams before wrapping.  For a true ring we'd
	 * compute the sum of all live BDL entries, but with uniform
	 * CHUNK_BYTES slots that simplifies to entries * CHUNK_BYTES.
	 * Use the full ring so the controller never thinks we've
	 * "ended" mid-stream. */
	hda_write32(d, HDA_SD_BASE + HDA_SD_CBL,
	            (uint32_t)(HDA_BDL_ENTRIES * HDA_CHUNK_BYTES));

	if (!d->running) {
		ctl = HDA_SDCTL_RUN | HDA_SDCTL_IOCE |
		      ((uint32_t)d->stream_tag << HDA_SDCTL_STREAM_SHIFT);
		hda_write32(d, HDA_SD_BASE + HDA_SD_CTL, ctl);
		d->running = 1;
	}

	d->writes_queued++;
	d->next_idx = (uint8_t)((slot + 1U) % HDA_BDL_ENTRIES);
	return (int)copy_len;
}

static int hda_drain(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static int hda_flush(audio_dev_t *adev)
{
	hda_dev_t *d = adev->driver_data;
	hda_write32(d, HDA_SD_BASE + HDA_SD_CTL, HDA_SDCTL_SRST);
	d->next_idx = 0;
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

	if (hda_corb_rirb_setup(d) != 0) {
		return -ENOMEM;
	}

	/* Enable global + controller interrupts before we try to talk to
	 * the codec — some controllers latch the response only when CIE is
	 * armed. */
	hda_write32(d, HDA_REG_INTCTL,
	            HDA_INTCTL_GIE | HDA_INTCTL_CIE | 0x40000000U);

	vendor_id = hda_send_verb(d, d->codec_addr, 0,
	                          HDA_VERB_GET_PARAMETER,
	                          HDA_PARAM_VENDOR_ID);

	if (hda_output_stream_init(d) != 0) {
		return -ENOMEM;
	}

	if (d->irq >= 0) {
		(void)request_irq((unsigned int)d->irq, hda_irq_handler,
		                  0, "hda", d);
	}

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
