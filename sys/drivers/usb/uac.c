/*
 * uac.c - USB Audio Class (UAC) playback driver.
 *
 * Binds a USB audio function and exposes its playback path as a substrate
 * audio_dev_t (/dev/audioN), so the normal audio stack — mpg123, /dev/audio
 * writers, the audioio ioctls — drives a USB headset/DAC.
 *
 * Scope: playback (host -> device, isochronous OUT) only; capture is ignored.
 * Tested target is the Apple EarPods (05ac:110b), a UAC 2.0 full-speed
 * function whose playback path is interface 1, alt 2 (16-bit/48k/stereo) on
 * isochronous OUT endpoint 0x02; qemu's -device usb-audio (UAC 1.0) works the
 * same way.
 *
 * Data path.  USB audio endpoints are isochronous-synchronous: the device's
 * DAC clock is fixed (48 kHz here), and the host must feed exactly one packet
 * per 1 ms frame, continuously, or playback stutters.  Two things follow:
 *
 *   1. Rate.  Source PCM at any rate (e.g. a 44.1 kHz MP3) is resampled to the
 *      device's 48 kHz before streaming — otherwise it plays at the wrong
 *      pitch.  A small linear interpolator does this in uac_write().
 *   2. Continuity.  A deep software FIFO decouples bursty write()s from the
 *      steady iso clock, and a feeder kthread keeps a sliding window of iso
 *      packets scheduled a few frames ahead of the controller (via the HCD
 *      iso_schedule/reclaim ops), so the stream never gaps between writes.
 */

#include "usb.h"
#include <drivers/audio/audio.h>
#include <drivers/audio/audio_fifo.h>

#include <sys/audioio.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/dma.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Class-specific descriptor + request constants (UAC). */
#define UAC_CS_INTERFACE          0x24
#define UAC_AC_INPUT_TERMINAL     0x02
#define UAC_AC_CLOCK_SOURCE       0x0A
#define UAC_AS_FORMAT_TYPE        0x02
#define UAC_TT_USB_STREAMING      0x0101  /* wTerminalType: USB streaming   */
#define UAC_REQ_CUR               0x01    /* SET/GET CUR                    */
#define UAC_CS_SAM_FREQ_CONTROL   0x01    /* clock-source freq control      */
#define UAC_EP_XFER_ISO           0x01    /* bmAttributes transfer type     */

/* Fixed device clock and derived packet geometry: 48 kHz, S16, stereo ==
 * 48 sample-frames * 4 bytes == 192 bytes per 1 ms USB frame. */
#define UAC_RATE_HZ               48000U
#define UAC_PKT_BYTES             192U

/* Streaming window: keep [LEAD, WINDOW) frames scheduled ahead of the
 * controller; RING_SLOTS (> WINDOW) packet buffers cycle through it. */
#define UAC_LEAD                  4U
#define UAC_WINDOW                48U
#define UAC_RING_SLOTS            64U
#define UAC_NFRAMES               1024U   /* UHCI frame-list size           */

#define UAC_FIFO_BYTES            (64U * 1024U)   /* ~340 ms cushion          */
#define UAC_STAGE_FRAMES          256U
#define UAC_IDLE_POLLS            64      /* go idle after this many empty polls */
#define UAC_MAX_DEVICES           4

typedef struct uac_dev {
	int             in_use;
	usb_device_t   *udev;
	usb_endpoint_t  iso_ep;     /* iso OUT endpoint (built from descriptors) */
	uint8_t         as_iface;
	uint8_t         as_alt;
	audio_dev_t     audio;

	/* Resampler: src_rate -> 48 kHz, 16.16 fixed-point phase between
	 * consecutive input frames (rs_last -> next input frame). */
	uint32_t        src_rate;
	uint32_t        rs_step;     /* input frames per output frame, 16.16    */
	uint32_t        rs_phase;
	int16_t         rs_last[2];
	int             rs_primed;

	/* Deep FIFO of 48 kHz S16 stereo PCM (producer: uac_write). */
	audio_fifo_t    fifo;
	uint8_t        *fifo_buf;

	/* Feeder kthread + iso packet ring (consumer). */
	thread_t       *feeder;
	volatile int    running;
	volatile int    active;      /* set by writes, cleared when long idle   */
	uint8_t        *ring;        /* coherent DMA: RING_SLOTS * PKT bytes     */
	dma_addr_t      ring_phys;
	void           *handles[UAC_RING_SLOTS];
	uint32_t        sched;       /* next free-running frame to fill          */
	uint32_t        frame_hi;    /* high bits of the free-running dev frame  */
	uint16_t        last_fr;
	int             started;
	int             idle_polls;
	uint64_t        poll_ticks;
} uac_dev_t;

static uac_dev_t uac_devices[UAC_MAX_DEVICES];

/*
 * ------------------------------------------------------------------
 * Feeder kthread — keep the iso window full from the FIFO
 * ------------------------------------------------------------------
 */
static void uac_reclaim_all(uac_dev_t *d)
{
	for (uint32_t i = 0; i < UAC_RING_SLOTS; i++) {
		if (d->handles[i]) {
			usb_iso_reclaim(d->udev, d->handles[i]);
			d->handles[i] = NULL;
		}
	}
}

static void uac_feeder(void *arg)
{
	uac_dev_t *d = arg;

	while (d->running) {
		uint16_t fr;
		uint32_t dev_frame;

		if (!d->active) {
			/* Idle: stop scheduling (device plays silence), wait to be
			 * woken by a write. */
			uac_reclaim_all(d);
			d->started = 0;
			sched_sleep_until((void *)&d->running,
			    get_ticks() + (get_hz() ? get_hz() / 10 : 1));
			continue;
		}

		fr = usb_frame_number(d->udev);
		if (fr < d->last_fr) {
			d->frame_hi += UAC_NFRAMES;     /* frame counter wrapped */
		}
		d->last_fr = fr;
		dev_frame = d->frame_hi + fr;

		/* (Re)sync the scheduling cursor on start or after an underrun. */
		if (!d->started ||
		    (int32_t)(d->sched - (dev_frame + UAC_LEAD)) < 0) {
			d->sched = dev_frame + UAC_LEAD;
			d->started = 1;
		}

		/* Top the window back up to WINDOW frames ahead. */
		while ((int32_t)(d->sched - (dev_frame + UAC_WINDOW)) < 0) {
			uint32_t slot = d->sched % UAC_RING_SLOTS;
			uint8_t *buf = d->ring + slot * UAC_PKT_BYTES;
			size_t n;

			if (d->handles[slot]) {     /* free this slot's last packet */
				usb_iso_reclaim(d->udev, d->handles[slot]);
				d->handles[slot] = NULL;
			}
			n = audio_fifo_read(&d->fifo, buf, UAC_PKT_BYTES);
			if (n < UAC_PKT_BYTES) {
				memset(buf + n, 0, UAC_PKT_BYTES - n);  /* silence-pad */
			}
			usb_iso_schedule(d->udev, &d->iso_ep,
			    (uint16_t)(d->sched & (UAC_NFRAMES - 1)),
			    (uint32_t)(d->ring_phys + slot * UAC_PKT_BYTES),
			    UAC_PKT_BYTES, &d->handles[slot]);
			d->sched++;
		}

		/* Wake a writer parked on a full FIFO; decide whether to idle. */
		sched_wakeup(&d->fifo);
		if (audio_fifo_used(&d->fifo) == 0) {
			if (++d->idle_polls > UAC_IDLE_POLLS) {
				d->active = 0;
			}
		} else {
			d->idle_polls = 0;
		}

		sched_sleep_until((void *)&d->running, get_ticks() + d->poll_ticks);
	}
}

/*
 * ------------------------------------------------------------------
 * audio_dev_t backend ops
 * ------------------------------------------------------------------
 */
static int uac_open(audio_dev_t *adev, int mode)
{
	(void)adev;
	(void)mode;
	return 0;
}

static int uac_close(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static int uac_set_params(audio_dev_t *adev, audio_info_t *info)
{
	uac_dev_t *d = adev->driver_data;
	uint32_t rate = (info && info->play.sample_rate) ?
	                info->play.sample_rate : UAC_RATE_HZ;

	if (rate < 4000) rate = UAC_RATE_HZ;
	if (rate > 192000) rate = 192000;

	if (d) {
		d->src_rate = rate;
		d->rs_step = (uint32_t)(((uint64_t)rate << 16) / UAC_RATE_HZ);
		d->rs_phase = 0;
		d->rs_primed = 0;   /* re-prime the interpolator on the next write */
	}
	/* Report the accepted source rate back; we resample it to 48 kHz. */
	adev->current.play.sample_rate = rate;
	adev->current.play.channels    = 2;
	adev->current.play.precision   = 16;
	adev->current.play.encoding    = AUDIO_ENCODING_SLINEAR_LE;
	return 0;
}

/* Block (interruptibly) until the whole staging buffer is in the FIFO. */
static void uac_fifo_push(uac_dev_t *d, const void *data, size_t n)
{
	const uint8_t *p = data;
	size_t off = 0;

	while (off < n) {
		off += audio_fifo_write(&d->fifo, p + off, n - off);
		if (off >= n) {
			break;
		}
		/* FIFO full — make sure the feeder is draining, then wait. */
		d->active = 1;
		sched_wakeup((void *)&d->running);
		if (current_thread &&
		    (current_thread->sig_pending & ~current_thread->sig_mask)) {
			return;   /* killed — drop the remainder, write returns short */
		}
		if (current_thread) {
			current_thread->flags |= THREAD_F_INTERRUPTIBLE;
			sched_sleep_until(&d->fifo,
			    get_ticks() + (get_hz() ? get_hz() / 100 : 1));
			current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
		}
	}
}

static int uac_write(audio_dev_t *adev, const void *buf, size_t len)
{
	uac_dev_t *d = adev->driver_data;
	const int16_t *in = buf;
	int16_t stage[UAC_STAGE_FRAMES * 2];
	size_t nin, i;
	int sf = 0;

	if (d == NULL || d->udev == NULL || d->ring == NULL || len < 4) {
		return (int)len;
	}
	nin = len / 4;   /* S16 stereo frames */

	if (!d->rs_primed) {
		d->rs_last[0] = in[0];
		d->rs_last[1] = in[1];
		d->rs_phase = 0;
		d->rs_primed = 1;
	}

	/* Linear-interpolate src_rate -> 48 kHz, staging into the FIFO. */
	for (i = 0; i < nin; i++) {
		int cl = in[2 * i];
		int cr = in[2 * i + 1];
		while (d->rs_phase < 0x10000U) {
			int64_t f = d->rs_phase;
			stage[sf * 2] = (int16_t)(d->rs_last[0] +
			    (((int64_t)(cl - d->rs_last[0]) * f) >> 16));
			stage[sf * 2 + 1] = (int16_t)(d->rs_last[1] +
			    (((int64_t)(cr - d->rs_last[1]) * f) >> 16));
			sf++;
			if (sf == (int)UAC_STAGE_FRAMES) {
				uac_fifo_push(d, stage, (size_t)sf * 4);
				sf = 0;
			}
			d->rs_phase += d->rs_step;
		}
		d->rs_phase -= 0x10000U;
		d->rs_last[0] = (int16_t)cl;
		d->rs_last[1] = (int16_t)cr;
	}
	if (sf) {
		uac_fifo_push(d, stage, (size_t)sf * 4);
	}

	d->active = 1;
	sched_wakeup((void *)&d->running);   /* wake the feeder */
	return (int)len;
}

static int uac_drain(audio_dev_t *adev)
{
	uac_dev_t *d = adev->driver_data;
	int guard = 1000;   /* ~ up to a few seconds */

	if (d == NULL) {
		return 0;
	}
	while (audio_fifo_used(&d->fifo) > 0 && guard-- > 0) {
		if (current_thread &&
		    (current_thread->sig_pending & ~current_thread->sig_mask)) {
			break;
		}
		sched_sleep_until(&d->fifo,
		    get_ticks() + (get_hz() ? get_hz() / 100 : 1));
	}
	return 0;
}

static int uac_flush(audio_dev_t *adev)
{
	uac_dev_t *d = adev->driver_data;

	if (d) {
		audio_fifo_reset(&d->fifo);
	}
	return 0;
}

static void uac_get_devinfo(audio_dev_t *adev, audio_device_t *out)
{
	(void)adev;
	memset(out, 0, sizeof(*out));
	snprintf(out->name, sizeof(out->name), "USB Audio");
	snprintf(out->version, sizeof(out->version), "1.0");
	snprintf(out->config, sizeof(out->config), "uac");
}

static int uac_get_props(audio_dev_t *adev)
{
	(void)adev;
	return AUDIO_PROP_PLAYBACK;
}

static audio_dev_ops_t uac_ops = {
	.open        = uac_open,
	.close       = uac_close,
	.write       = uac_write,
	.read        = NULL,
	.set_params  = uac_set_params,
	.drain       = uac_drain,
	.flush       = uac_flush,
	.get_devinfo = uac_get_devinfo,
	.get_props   = uac_get_props,
};

/*
 * ------------------------------------------------------------------
 * Descriptor parsing
 * ------------------------------------------------------------------
 */
typedef struct uac_parse {
	int      ac_iface;
	uint8_t  clock_id;
	int      play_iface;
	int      play_alt;
	uint8_t  ep_addr;
	uint16_t ep_maxpkt;
	uint8_t  ep_interval;
	int      have_16bit;
} uac_parse_t;

static void uac_parse_config(usb_device_t *dev, uac_parse_t *p)
{
	const uint8_t *ptr = dev->config_data;
	const uint8_t *end = ptr + dev->config_len;

	int      cur_iface    = -1;
	int      cur_alt      = -1;
	uint8_t  cur_subclass = 0;
	uint8_t  cur_protocol = 0;
	uint8_t  cur_bits     = 0;

	memset(p, 0, sizeof(*p));
	p->ac_iface   = -1;
	p->play_iface = -1;
	p->play_alt   = -1;

	while (ptr + 2 <= end) {
		uint8_t blen = ptr[0];
		uint8_t btype = ptr[1];

		if (blen < 2 || ptr + blen > end) {
			break;
		}

		if (btype == USB_DT_INTERFACE && blen >= 9) {
			cur_iface    = ptr[2];
			cur_alt      = ptr[3];
			cur_subclass = ptr[6];
			cur_protocol = ptr[7];
			cur_bits     = 0;
			if (cur_subclass == USB_SUBCLASS_AUDIOCONTROL &&
			    p->ac_iface < 0) {
				p->ac_iface = cur_iface;
			}
		} else if (btype == UAC_CS_INTERFACE && blen >= 3) {
			uint8_t subtype = ptr[2];
			if (cur_subclass == USB_SUBCLASS_AUDIOCONTROL) {
				if (subtype == UAC_AC_INPUT_TERMINAL && blen >= 8) {
					uint16_t tt = (uint16_t)(ptr[4] | (ptr[5] << 8));
					if (tt == UAC_TT_USB_STREAMING) {
						p->clock_id = ptr[7];
					}
				} else if (subtype == UAC_AC_CLOCK_SOURCE && blen >= 4) {
					if (p->clock_id == 0) {
						p->clock_id = ptr[3];
					}
				}
			} else if (cur_subclass == USB_SUBCLASS_AUDIOSTREAMING) {
				if (subtype == UAC_AS_FORMAT_TYPE) {
					/* bBitResolution: UAC2 [5], UAC1 [6]. */
					if (cur_protocol == 0x20 && blen >= 6) {
						cur_bits = ptr[5];
					} else if (blen >= 7) {
						cur_bits = ptr[6];
					}
				}
			}
		} else if (btype == USB_DT_ENDPOINT && blen >= 7 &&
		           cur_subclass == USB_SUBCLASS_AUDIOSTREAMING) {
			uint8_t  addr = ptr[2];
			uint8_t  attr = ptr[3];
			uint16_t mps  = (uint16_t)(ptr[4] | (ptr[5] << 8));
			int is_iso = (attr & 0x03) == UAC_EP_XFER_ISO;
			int is_out = (addr & USB_EP_DIR_MASK) == USB_EP_DIR_OUT;

			if (is_iso && is_out) {
				int prefer = (cur_bits == 16 && !p->have_16bit);
				if (p->play_iface < 0 || prefer) {
					p->play_iface  = cur_iface;
					p->play_alt    = cur_alt;
					p->ep_addr     = addr;
					p->ep_maxpkt   = mps & 0x07FF;
					p->ep_interval = ptr[6];
					p->have_16bit  = (cur_bits == 16);
				}
			}
		}

		ptr += blen;
	}
}

static int uac_set_clock_rate(usb_device_t *dev, int ac_iface,
                              uint8_t clock_id, uint32_t hz)
{
	uint8_t buf[4];

	buf[0] = (uint8_t)(hz & 0xFF);
	buf[1] = (uint8_t)((hz >> 8) & 0xFF);
	buf[2] = (uint8_t)((hz >> 16) & 0xFF);
	buf[3] = (uint8_t)((hz >> 24) & 0xFF);

	return usb_control_transfer(dev,
	            USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
	            UAC_REQ_CUR,
	            (uint16_t)(UAC_CS_SAM_FREQ_CONTROL << 8),
	            (uint16_t)((clock_id << 8) | (ac_iface & 0xFF)),
	            buf, sizeof(buf));
}

/*
 * ------------------------------------------------------------------
 * Class driver callbacks
 * ------------------------------------------------------------------
 */
static int uac_probe(usb_device_t *dev)
{
	if (dev->if_class == USB_CLASS_AUDIO &&
	    dev->if_subclass == USB_SUBCLASS_AUDIOCONTROL) {
		return 0;
	}
	return -1;
}

static uac_dev_t *uac_alloc(void)
{
	for (int i = 0; i < UAC_MAX_DEVICES; i++) {
		if (!uac_devices[i].in_use) {
			memset(&uac_devices[i], 0, sizeof(uac_devices[i]));
			uac_devices[i].in_use = 1;
			return &uac_devices[i];
		}
	}
	return NULL;
}

static int uac_attach(usb_device_t *dev)
{
	uac_parse_t p;
	uac_dev_t  *d;
	int rc;

	uac_parse_config(dev, &p);

	if (p.play_iface < 0 || p.ep_maxpkt < UAC_PKT_BYTES) {
		kprintf("uac: %04x:%04x has no usable 48k iso OUT endpoint\n",
		        dev->vendor_id, dev->product_id);
		return -1;
	}
	if (!p.have_16bit) {
		kprintf("uac: %04x:%04x has no 16-bit playback alt; not attaching\n",
		        dev->vendor_id, dev->product_id);
		return -1;
	}

	d = uac_alloc();
	if (!d) {
		kprintf("uac: no free device slots\n");
		return -1;
	}
	d->udev     = dev;
	d->as_iface = (uint8_t)p.play_iface;
	d->as_alt   = (uint8_t)p.play_alt;
	d->iso_ep.address    = p.ep_addr;
	d->iso_ep.type       = USB_EP_TYPE_ISO;
	d->iso_ep.max_packet = p.ep_maxpkt;
	d->iso_ep.interval   = p.ep_interval;

	/* Allocate the FIFO and the coherent iso packet ring. */
	d->fifo_buf = kmalloc(UAC_FIFO_BYTES);
	d->ring = dma_alloc_coherent(UAC_RING_SLOTS * UAC_PKT_BYTES, &d->ring_phys);
	if (d->fifo_buf == NULL || d->ring == NULL) {
		kprintf("uac: out of memory for FIFO/ring\n");
		if (d->fifo_buf) kfree(d->fifo_buf, UAC_FIFO_BYTES);
		if (d->ring) dma_free_coherent(d->ring, UAC_RING_SLOTS * UAC_PKT_BYTES);
		d->in_use = 0;
		return -1;
	}
	audio_fifo_init(&d->fifo, d->fifo_buf, UAC_FIFO_BYTES);
	d->src_rate = UAC_RATE_HZ;
	d->rs_step = 0x10000U;   /* 1:1 until set_params */
	d->poll_ticks = get_hz() ? (get_hz() / 200) : 1;
	if (d->poll_ticks == 0) {
		d->poll_ticks = 1;
	}

	/* Program the device clock to 48 kHz, then activate the streaming alt. */
	if (p.clock_id != 0) {
		(void)uac_set_clock_rate(dev, p.ac_iface, p.clock_id, UAC_RATE_HZ);
	}
	rc = usb_set_interface(dev, d->as_iface, d->as_alt);
	if (rc != USB_XFER_OK) {
		kprintf("uac: SET_INTERFACE(%u, alt %u) failed (%d)\n",
		        d->as_iface, d->as_alt, rc);
		kfree(d->fifo_buf, UAC_FIFO_BYTES);
		dma_free_coherent(d->ring, UAC_RING_SLOTS * UAC_PKT_BYTES);
		d->in_use = 0;
		return -1;
	}

	/* Start the feeder before publishing the device. */
	d->running = 1;
	if (kthread_create(uac_feeder, d, &d->feeder, "uac-feed") != 0) {
		kprintf("uac: feeder kthread failed\n");
		d->running = 0;
		kfree(d->fifo_buf, UAC_FIFO_BYTES);
		dma_free_coherent(d->ring, UAC_RING_SLOTS * UAC_PKT_BYTES);
		d->in_use = 0;
		return -1;
	}

	snprintf(d->audio.name, sizeof(d->audio.name),
	         "USB Audio %04x:%04x", dev->vendor_id, dev->product_id);
	d->audio.ops         = &uac_ops;
	d->audio.driver_data = d;
	d->audio.current.mode = AUMODE_PLAY;
	d->audio.current.play.sample_rate = UAC_RATE_HZ;
	d->audio.current.play.channels    = 2;
	d->audio.current.play.precision   = 16;
	d->audio.current.play.encoding    = AUDIO_ENCODING_SLINEAR_LE;

	rc = audio_register_device(&d->audio);
	if (rc != 0) {
		kprintf("uac: audio_register_device failed (%d)\n", rc);
		d->running = 0;
		d->in_use = 0;
		return -1;
	}

	dev->driver_data = d;
	kprintf("uac: %04x:%04x playback on iface %u alt %u, iso EP 0x%02x "
	        "(%u B/frame) -> %s, 48 kHz S16 stereo (resampling on)\n",
	        dev->vendor_id, dev->product_id, d->as_iface, d->as_alt,
	        p.ep_addr, p.ep_maxpkt, d->audio.name);
	return 0;
}

static void uac_detach(usb_device_t *dev)
{
	uac_dev_t *d = dev->driver_data;

	if (d == NULL) {
		return;
	}
	d->running = 0;
	d->active = 0;
	sched_wakeup((void *)&d->running);
	(void)usb_set_interface(dev, d->as_iface, 0);
	dev->driver_data = NULL;
	/* Buffers are intentionally left allocated: the feeder may still be
	 * unwinding and we have no join primitive here.  Detach is not hit on
	 * the supported (non-hot-unplug) paths. */
}

static usb_class_driver_t uac_driver = {
	.name       = "usb-audio",
	.if_class   = USB_CLASS_AUDIO,
	.if_subclass = USB_SUBCLASS_AUDIOCONTROL,
	.if_protocol = 0xFF,   /* match any UAC protocol (1.0 == 0, 2.0 == 0x20) */
	.probe      = uac_probe,
	.attach     = uac_attach,
	.detach     = uac_detach,
};

void uac_init(void)
{
	usb_register_class_driver(&uac_driver);
}
