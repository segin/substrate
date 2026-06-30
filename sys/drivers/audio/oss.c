/*
 * oss.c - OSS (Open Sound System) /dev/dsp frontend.
 *
 * An additive frontend over the same audio_dev backend that drives the
 * Sun/SADA /dev/audioN nodes.  /dev/dspN (and the /dev/dsp alias) share the
 * backend completely: open -> backend open, write -> ops->write (play PCM),
 * close -> backend close.  Only the ioctl ABI differs — this file translates
 * the OSS SNDCTL_DSP_* command set into the framework's audio_info_t model,
 * leaving the existing Sun/SADA path in audio.c untouched.
 *
 * Personality-agnostic: the node carries no perso_mask, so native, Linux, and
 * FreeBSD programs all see it.  Each personality encodes SNDCTL_DSP_* with its
 * own _IOWR macro (so the direction/size high bits differ), therefore the
 * dispatch matches on the OSS group ('P') and command number alone — never the
 * fully-encoded request word — and every personality's OSS ioctls route to the
 * right handler.
 */

#include "audio.h"

#include <sys/audioio.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/major.h>
#include <sys/soundcard.h>
#include <kern/console.h>
#include <vfs/vfs.h>
#include <stdio.h>
#include <string.h>

static fs_node_t oss_nodes[AUDIO_MAX_DEVICES];

/* ----------------------------------------------------------------- */
/* AFMT <-> audio_info_t encoding/precision mapping                  */
/* ----------------------------------------------------------------- */

/*
 * Translate an OSS AFMT_* sample format into the framework's
 * (encoding, precision) pair.  Returns 0 and fills enc/prec on a format we
 * support, or -EINVAL for one we don't (the OSS convention is then to leave
 * the current format in place and report it back to the caller).
 */
static int oss_afmt_to_encoding(int afmt, uint32_t *enc, uint32_t *prec)
{
	switch (afmt) {
	case AFMT_S16_LE: *enc = AUDIO_ENCODING_SLINEAR_LE; *prec = 16; return 0;
	case AFMT_S16_BE: *enc = AUDIO_ENCODING_SLINEAR_BE; *prec = 16; return 0;
	case AFMT_U8:     *enc = AUDIO_ENCODING_ULINEAR;    *prec = 8;  return 0;
	case AFMT_S8:     *enc = AUDIO_ENCODING_SLINEAR;    *prec = 8;  return 0;
	case AFMT_MU_LAW: *enc = AUDIO_ENCODING_ULAW;       *prec = 8;  return 0;
	case AFMT_A_LAW:  *enc = AUDIO_ENCODING_ALAW;       *prec = 8;  return 0;
	default:          return -EINVAL;
	}
}

/* Inverse mapping: what AFMT_* does the current play config correspond to? */
static int oss_encoding_to_afmt(uint32_t enc, uint32_t prec)
{
	switch (enc) {
	case AUDIO_ENCODING_SLINEAR_LE:
	case AUDIO_ENCODING_PCM16:
		return AFMT_S16_LE;
	case AUDIO_ENCODING_SLINEAR_BE:
		return AFMT_S16_BE;
	case AUDIO_ENCODING_SLINEAR:
		return (prec == 8) ? AFMT_S8 : AFMT_S16_LE;
	case AUDIO_ENCODING_ULINEAR:
	case AUDIO_ENCODING_ULINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_BE:
	case AUDIO_ENCODING_PCM8:
		return AFMT_U8;
	case AUDIO_ENCODING_ULAW:
		return AFMT_MU_LAW;
	case AUDIO_ENCODING_ALAW:
		return AFMT_A_LAW;
	default:
		return (prec == 8) ? AFMT_U8 : AFMT_S16_LE;
	}
}

/* The formats the framework can validate and a backend can play. */
#define OSS_SUPPORTED_FMTS \
	(AFMT_S16_LE | AFMT_S16_BE | AFMT_U8 | AFMT_S8 | AFMT_MU_LAW | AFMT_A_LAW)

/* ----------------------------------------------------------------- */
/* Apply helper — mirrors AUDIO_SETINFO: validate, set_params, store */
/* ----------------------------------------------------------------- */

static int oss_apply(audio_dev_t *dev, audio_info_t *merged)
{
	int rc;

	rc = audio_validate_info(merged);
	if (rc != 0) {
		return rc;
	}
	if (dev->ops != NULL && dev->ops->set_params != NULL) {
		rc = dev->ops->set_params(dev, merged);
		if (rc != 0) {
			return rc;
		}
	}
	dev->current = *merged;
	return 0;
}

/* ----------------------------------------------------------------- */
/* GETOSPACE helper                                                  */
/* ----------------------------------------------------------------- */

static void oss_get_ospace(audio_dev_t *dev, audio_buf_info *bi)
{
	int fragsize = 0, fragstotal = 0, fragments = 0, bytes = 0;

	if (dev->ops != NULL && dev->ops->get_ospace != NULL &&
	    dev->ops->get_ospace(dev, &fragsize, &fragstotal,
				 &fragments, &bytes) == 0) {
		bi->fragsize   = fragsize;
		bi->fragstotal = fragstotal;
		bi->fragments  = fragments;
		bi->bytes      = bytes;
		return;
	}

	/*
	 * Backend without a FIFO-occupancy reporter: synthesize from the
	 * framework-level blocksize/hiwat and report the whole buffer free.
	 */
	bi->fragsize   = (int)(dev->current.blocksize ?
			       dev->current.blocksize : AUDIO_DEFAULT_BLOCKSIZE);
	bi->fragstotal = (int)(dev->current.hiwat ?
			       dev->current.hiwat : AUDIO_DEFAULT_HIWAT);
	bi->fragments  = bi->fragstotal;
	bi->bytes      = bi->fragsize * bi->fragstotal;
}

/* ----------------------------------------------------------------- */
/* OSS ioctl dispatch                                                */
/* ----------------------------------------------------------------- */

int oss_ioctl_dispatch(audio_dev_t *dev, uint32_t request, void *arg)
{
	uint32_t group = (request >> 8) & 0xFFu;
	uint32_t num   = request & 0xFFu;
	int v;
	int rc;

	if (dev == NULL) {
		return -EINVAL;
	}

	/*
	 * Encoding-tolerant routing: a SNDCTL_DSP_* command is identified by
	 * its OSS group ('P') and command number only.  The direction and size
	 * bits in the high half of the request word vary per personality
	 * (substrate-native, Linux, FreeBSD) and are deliberately ignored.
	 */
	if (group != (uint32_t)OSS_GROUP_DSP) {
		return -ENOTTY;
	}

	switch (num) {
	case 2: /* SNDCTL_DSP_SPEED — arg: int sample rate, write back actual */
	{
		audio_info_t info;

		if (arg == NULL || copyin(arg, &v, sizeof(v)) != 0) {
			return -EFAULT;
		}
		info = dev->current;
		info.play.sample_rate   = (uint32_t)v;
		info.record.sample_rate = (uint32_t)v;
		rc = oss_apply(dev, &info);
		if (rc != 0) {
			return rc;
		}
		v = (int)dev->current.play.sample_rate;
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 3: /* SNDCTL_DSP_STEREO — arg: 0 = mono, 1 = stereo */
	{
		audio_info_t info;

		if (arg == NULL || copyin(arg, &v, sizeof(v)) != 0) {
			return -EFAULT;
		}
		info = dev->current;
		info.play.channels = v ? 2 : 1;
		rc = oss_apply(dev, &info);
		if (rc != 0) {
			return rc;
		}
		v = (dev->current.play.channels >= 2) ? 1 : 0;
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 6: /* SNDCTL_DSP_CHANNELS — arg: channel count, write back actual */
	{
		audio_info_t info;

		if (arg == NULL || copyin(arg, &v, sizeof(v)) != 0) {
			return -EFAULT;
		}
		info = dev->current;
		if (v >= 1) {
			info.play.channels = (uint32_t)v;
		}
		rc = oss_apply(dev, &info);
		if (rc != 0) {
			return rc;
		}
		v = (int)dev->current.play.channels;
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 5: /* SNDCTL_DSP_SETFMT — arg: AFMT_*, write back actual format */
	{
		audio_info_t info;
		uint32_t enc, prec;

		if (arg == NULL || copyin(arg, &v, sizeof(v)) != 0) {
			return -EFAULT;
		}
		/* AFMT_QUERY (0): report current format without changing it. */
		if (v != AFMT_QUERY && oss_afmt_to_encoding(v, &enc, &prec) == 0) {
			info = dev->current;
			info.play.encoding  = enc;
			info.play.precision = prec;
			rc = oss_apply(dev, &info);
			if (rc != 0) {
				return rc;
			}
		}
		v = oss_encoding_to_afmt(dev->current.play.encoding,
					 dev->current.play.precision);
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 11: /* SNDCTL_DSP_GETFMTS — bitmask of supported AFMT_* */
		v = OSS_SUPPORTED_FMTS;
		if (arg == NULL || copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;

	case 12: /* SNDCTL_DSP_GETOSPACE — output FIFO free space */
	{
		audio_buf_info bi;

		memset(&bi, 0, sizeof(bi));
		oss_get_ospace(dev, &bi);
		if (arg == NULL || copyout(&bi, arg, sizeof(bi)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 13: /* SNDCTL_DSP_GETISPACE — no capture buffering: report empty */
	{
		audio_buf_info bi;

		memset(&bi, 0, sizeof(bi));
		bi.fragsize   = (int)(dev->current.blocksize ?
				      dev->current.blocksize :
				      AUDIO_DEFAULT_BLOCKSIZE);
		bi.fragstotal = (int)(dev->current.hiwat ?
				      dev->current.hiwat : AUDIO_DEFAULT_HIWAT);
		if (arg == NULL || copyout(&bi, arg, sizeof(bi)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 4:  /* SNDCTL_DSP_GETBLKSIZE — fragment size in bytes */
		v = (int)(dev->current.blocksize ?
			  dev->current.blocksize : AUDIO_DEFAULT_BLOCKSIZE);
		if (arg == NULL || copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;

	case 10: /* SNDCTL_DSP_SETFRAGMENT — arg: (max_frags<<16)|frag_log2 */
		/*
		 * Accept the request and echo it back.  The backend FIFO is a
		 * fixed deep ring, so we don't repartition it; reporting the
		 * caller's own value back keeps OSS clients (e.g. SDL) happy.
		 */
		if (arg == NULL || copyin(arg, &v, sizeof(v)) != 0) {
			return -EFAULT;
		}
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;

	case 15: /* SNDCTL_DSP_GETCAPS */
	{
		int props = (dev->ops != NULL && dev->ops->get_props != NULL) ?
			    dev->ops->get_props(dev) : 0;

		v = DSP_CAP_REALTIME | DSP_CAP_BATCH | DSP_CAP_TRIGGER;
		if (dev->ops != NULL && dev->ops->mmap != NULL) {
			v |= DSP_CAP_MMAP;
		}
		if (props & AUDIO_PROP_FULLDUPLEX) {
			v |= DSP_CAP_DUPLEX;
		}
		if (arg == NULL || copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case 1: /* SNDCTL_DSP_SYNC — drain the output */
		if (dev->ops != NULL && dev->ops->drain != NULL) {
			return dev->ops->drain(dev);
		}
		return 0;

	case 0: /* SNDCTL_DSP_RESET — discard queued output */
		if (dev->ops != NULL && dev->ops->flush != NULL) {
			return dev->ops->flush(dev);
		}
		return 0;

	case 8:  /* SNDCTL_DSP_POST — hint that a write boundary was reached */
	case 14: /* SNDCTL_DSP_NONBLOCK — fd nonblock toggle (handled at VFS) */
	case 22: /* SNDCTL_DSP_SETDUPLEX */
		return 0;

	case 16: /* SNDCTL_DSP_GETTRIGGER / SETTRIGGER */
		/* Output is always enabled; report/accept that. */
		v = PCM_ENABLE_OUTPUT;
		if (arg != NULL) {
			(void)copyout(&v, arg, sizeof(v));
		}
		return 0;

	case 23: /* SNDCTL_DSP_GETODELAY — bytes still queued for playback */
	{
		int fragsize = 0, fragstotal = 0, fragments = 0, bytes = 0;

		v = 0;
		if (dev->ops != NULL && dev->ops->get_ospace != NULL &&
		    dev->ops->get_ospace(dev, &fragsize, &fragstotal,
					 &fragments, &bytes) == 0) {
			/* queued = total buffer - currently free */
			v = (fragsize * fragstotal) - bytes;
			if (v < 0) {
				v = 0;
			}
		}
		if (arg == NULL || copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	default:
		return -EINVAL;
	}
}

/* ----------------------------------------------------------------- */
/* fs_node_t glue                                                    */
/* ----------------------------------------------------------------- */

static int oss_node_ioctl(fs_node_t *node, uint32_t request, void *arg)
{
	audio_dev_t *dev = (audio_dev_t *)node->impl;

	return oss_ioctl_dispatch(dev, request, arg);
}

/*
 * /dev/dsp open: refcount + backend open via the shared helper, then apply the
 * OSS-conventional default configuration (8000 Hz, AFMT_U8, mono) so a program
 * that writes raw bytes without configuring the device gets OSS semantics
 * rather than the Sun/SADA 44.1 kHz/stereo default.  Programs that set their
 * own format/rate/channels override this immediately.
 */
static void oss_node_open(fs_node_t *node)
{
	audio_dev_t *dev = (audio_dev_t *)node->impl;
	audio_info_t info;

	audio_node_open(node);

	if (dev == NULL) {
		return;
	}
	info = dev->current;
	info.play.sample_rate = 8000;
	info.play.channels    = 1;
	info.play.precision   = 8;
	info.play.encoding    = AUDIO_ENCODING_ULINEAR;
	info.mode             = AUMODE_PLAY;
	(void)oss_apply(dev, &info);
}

/* ----------------------------------------------------------------- */
/* Registration                                                      */
/* ----------------------------------------------------------------- */

void oss_register_device(audio_dev_t *dev, int unit)
{
	fs_node_t *dsp;

	if (dev == NULL || unit < 0 || unit >= AUDIO_MAX_DEVICES) {
		return;
	}

	dsp = &oss_nodes[unit];
	memset(dsp, 0, sizeof(*dsp));
	snprintf(dsp->name, sizeof(dsp->name), "dsp%d", unit);
	dsp->flags = FS_CHARDEVICE;
	/* Same policy as /dev/audioN: 0660 root:audio. */
	dsp->mask  = 0660;
	dsp->uid   = GID_ROOT;
	dsp->gid   = GID_AUDIO;
	dsp->read  = audio_node_read;
	dsp->write = audio_node_write;
	dsp->ioctl = oss_node_ioctl;
	dsp->mmap  = audio_node_mmap;
	dsp->open  = oss_node_open;
	dsp->close = audio_node_close;
	dsp->impl  = (uintptr_t)dev;
	/* OSS DSP nodes share the sound major; offset the minor past the
	 * Sun/SADA audio + audioctl pair so it stays unique per unit. */
	dsp->rdev  = makedev(SOUND_MAJOR, unit * 2 + 0x40);
	devfs_register_device(dsp);

	kprintf("audio: registered %s OSS frontend as /dev/dsp%d\n",
		dev->name, unit);

	/* /dev/dsp -> /dev/dsp0 alias for the common unsuffixed open. */
	if (unit == 0) {
		(void)devfs_register_alias("dsp", "dsp0");
	}
}
