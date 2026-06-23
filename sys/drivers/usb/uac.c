/*
 * uac.c - USB Audio Class (UAC) playback driver.
 *
 * Binds a USB audio function and exposes its playback path as a substrate
 * audio_dev_t (/dev/audioN), so the normal audio stack — mpg123, /dev/audio
 * writers, the audioio ioctls — drives a USB headset/DAC.
 *
 * Scope: playback (host -> device, isochronous OUT) only; capture is ignored.
 * Tested target is the Apple EarPods (05ac:110b), a UAC 2.0 full-speed
 * function:
 *
 *   - Interface 0  AudioControl (UAC 2.0): clock source ID 9 feeds the
 *                  USB-streaming input terminal (the playback path).
 *   - Interface 1  AudioStreaming, alt 0 = zero bandwidth,
 *                  alt 1 = 24-bit/48k, alt 2 = 16-bit/48k, both on
 *                  isochronous OUT endpoint 0x02 (bInterval 1).
 *
 * We prefer the 16-bit alternate setting (matches substrate's S16 audio
 * framework), set the clock to 48 kHz via the UAC 2.0 clock-source control,
 * SET_INTERFACE to the streaming alt, and stream write() data straight to the
 * iso endpoint.  The descriptor walk is generic, so other single-clock UAC
 * playback functions with a 16-bit alt should work too.
 */

#include "usb.h"
#include <drivers/audio/audio.h>

#include <sys/audioio.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

/* Class-specific descriptor + request constants (UAC 2.0). */
#define UAC_CS_INTERFACE          0x24
#define UAC_AC_INPUT_TERMINAL     0x02
#define UAC_AC_CLOCK_SOURCE       0x0A
#define UAC_AS_FORMAT_TYPE        0x02
#define UAC_TT_USB_STREAMING      0x0101  /* wTerminalType: USB streaming   */
#define UAC_REQ_CUR               0x01    /* SET/GET CUR                    */
#define UAC_CS_SAM_FREQ_CONTROL   0x01    /* clock-source freq control      */
#define UAC_EP_XFER_ISO           0x01    /* bmAttributes transfer type     */

#define UAC_RATE_HZ               48000U  /* the EarPods' only clock rate   */
#define UAC_MAX_DEVICES           4

typedef struct uac_dev {
	int            in_use;
	usb_device_t  *udev;
	usb_endpoint_t iso_ep;     /* iso OUT endpoint (built from descriptors) */
	uint8_t        as_iface;   /* streaming interface number               */
	uint8_t        as_alt;     /* chosen alternate setting                 */
	audio_dev_t    audio;      /* the registered framework device          */
} uac_dev_t;

static uac_dev_t uac_devices[UAC_MAX_DEVICES];

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
	/* The device clock is fixed at 48 kHz / 16-bit / stereo; accept the
	 * request but report back what the hardware will actually play. */
	(void)info;
	adev->current.play.sample_rate = UAC_RATE_HZ;
	adev->current.play.channels    = 2;
	adev->current.play.precision   = 16;
	adev->current.play.encoding    = AUDIO_ENCODING_SLINEAR_LE;
	return 0;
}

static int uac_write(audio_dev_t *adev, const void *buf, size_t len)
{
	uac_dev_t *d = adev->driver_data;
	uint32_t actual = 0;
	int rc;

	if (d == NULL || d->udev == NULL || len == 0) {
		return (int)len;
	}
	if (len > 0x7FFFFFFFu) {
		len = 0x7FFFFFFFu;
	}

	/* Synchronous isochronous OUT: one packet per 1ms frame.  Blocks for
	 * roughly len / wMaxPacketSize milliseconds while the controller streams
	 * the buffer to the endpoint. */
	rc = usb_iso_transfer(d->udev, &d->iso_ep, (void *)buf,
	                      (uint32_t)len, &actual);
	if (rc != USB_XFER_OK && actual == 0) {
		return -5; /* -EIO */
	}
	return (int)(actual ? actual : len);
}

static int uac_drain(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static int uac_flush(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static void uac_get_devinfo(audio_dev_t *adev, audio_device_t *out)
{
	(void)adev;
	memset(out, 0, sizeof(*out));
	snprintf(out->name, sizeof(out->name), "USB Audio");
	snprintf(out->version, sizeof(out->version), "2.0");
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
 *
 * Walk the configuration descriptor and locate the playback streaming
 * interface + alt setting, its isochronous OUT endpoint, and the clock entity
 * that feeds the USB-streaming input terminal.  We prefer a 16-bit alternate
 * setting; if only a non-16-bit one exists we take the first iso OUT as a
 * fallback.
 */
typedef struct uac_parse {
	int      ac_iface;     /* AudioControl interface number (clock wIndex) */
	uint8_t  clock_id;     /* clock entity feeding the playback terminal   */
	int      play_iface;   /* chosen AudioStreaming interface              */
	int      play_alt;     /* chosen alternate setting                     */
	uint8_t  ep_addr;      /* iso OUT endpoint address                     */
	uint16_t ep_maxpkt;    /* iso OUT wMaxPacketSize                        */
	uint8_t  ep_interval;
	int      have_16bit;   /* a 16-bit alt was selected                    */
} uac_parse_t;

static void uac_parse_config(usb_device_t *dev, uac_parse_t *p)
{
	const uint8_t *ptr = dev->config_data;
	const uint8_t *end = ptr + dev->config_len;

	int      cur_iface   = -1;
	int      cur_alt     = -1;
	uint8_t  cur_subclass = 0;
	uint8_t  cur_protocol = 0;   /* bInterfaceProtocol: 0x20 == UAC 2.0     */
	uint8_t  cur_bits     = 0;   /* bit resolution of the current AS alt   */

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
			cur_subclass = ptr[6];   /* bInterfaceSubClass */
			cur_protocol = ptr[7];   /* bInterfaceProtocol */
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
						p->clock_id = ptr[7]; /* bCSourceID */
					}
				} else if (subtype == UAC_AC_CLOCK_SOURCE && blen >= 4) {
					if (p->clock_id == 0) {
						p->clock_id = ptr[3]; /* bClockID fallback */
					}
				}
			} else if (cur_subclass == USB_SUBCLASS_AUDIOSTREAMING) {
				if (subtype == UAC_AS_FORMAT_TYPE) {
					/* FORMAT_TYPE_I bBitResolution sits at a
					 * different offset between UAC versions:
					 *  UAC2: [4]=bSubslotSize [5]=bBitResolution
					 *  UAC1: [4]=bNrChannels [5]=bSubframeSize
					 *        [6]=bBitResolution */
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
				/* Take this alt if it is the first usable one, or if
				 * it is 16-bit and we previously only had a non-16-bit
				 * candidate. */
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

/*
 * ------------------------------------------------------------------
 * Clock + alt-setting configuration
 * ------------------------------------------------------------------
 */
static int uac_set_clock_rate(usb_device_t *dev, int ac_iface,
                              uint8_t clock_id, uint32_t hz)
{
	uint8_t buf[4];

	buf[0] = (uint8_t)(hz & 0xFF);
	buf[1] = (uint8_t)((hz >> 8) & 0xFF);
	buf[2] = (uint8_t)((hz >> 16) & 0xFF);
	buf[3] = (uint8_t)((hz >> 24) & 0xFF);

	/* UAC 2.0 SET_CUR to the clock-source entity:
	 *   bmRequestType = OUT | Class | Interface
	 *   wValue        = CS_SAM_FREQ_CONTROL << 8
	 *   wIndex        = (clockID << 8) | AudioControl interface
	 */
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
	/* Bind once, on the AudioControl interface of an audio function. */
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

	if (p.play_iface < 0 || p.ep_maxpkt == 0) {
		kprintf("uac: %04x:%04x has no isochronous OUT playback endpoint\n",
		        dev->vendor_id, dev->product_id);
		return -1;
	}
	if (!p.have_16bit) {
		/* Only non-16-bit formats; substrate's framework is S16, so we
		 * cannot feed it correctly yet. */
		kprintf("uac: %04x:%04x has no 16-bit playback alt (got %u-byte "
		        "packets); not attaching\n",
		        dev->vendor_id, dev->product_id, p.ep_maxpkt);
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

	/* Build the iso OUT endpoint from the descriptor data (the core's
	 * flattened endpoint cache may hold a different alt's endpoint). */
	d->iso_ep.address    = p.ep_addr;
	d->iso_ep.type       = USB_EP_TYPE_ISO;
	d->iso_ep.max_packet = p.ep_maxpkt;
	d->iso_ep.interval   = p.ep_interval;
	d->iso_ep.toggle     = 0;

	/* Program the sample clock, then activate the streaming alt setting so
	 * the isochronous endpoint becomes live. */
	if (p.clock_id != 0) {
		rc = uac_set_clock_rate(dev, p.ac_iface, p.clock_id, UAC_RATE_HZ);
		if (rc != USB_XFER_OK) {
			kprintf("uac: set clock %u to %u Hz failed (%d)\n",
			        p.clock_id, UAC_RATE_HZ, rc);
			/* non-fatal: many devices default to 48k */
		}
	}

	rc = usb_set_interface(dev, d->as_iface, d->as_alt);
	if (rc != USB_XFER_OK) {
		kprintf("uac: SET_INTERFACE(%u, alt %u) failed (%d)\n",
		        d->as_iface, d->as_alt, rc);
		d->in_use = 0;
		return -1;
	}

	/* Register with the audio framework. */
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
		d->in_use = 0;
		return -1;
	}

	dev->driver_data = d;
	kprintf("uac: %04x:%04x playback on iface %u alt %u, iso EP 0x%02x "
	        "(%u B/frame), %u Hz S16 stereo -> %s\n",
	        dev->vendor_id, dev->product_id, d->as_iface, d->as_alt,
	        p.ep_addr, p.ep_maxpkt, UAC_RATE_HZ, d->audio.name);
	return 0;
}

static void uac_detach(usb_device_t *dev)
{
	uac_dev_t *d = dev->driver_data;

	if (d == NULL) {
		return;
	}
	/* Return the streaming interface to its zero-bandwidth alt 0. */
	(void)usb_set_interface(dev, d->as_iface, 0);
	d->in_use = 0;
	dev->driver_data = NULL;
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
