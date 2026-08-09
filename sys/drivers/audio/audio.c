/*
 * audio.c - Substrate audio framework.
 *
 * Owns parameter validation, the shared ioctl dispatcher, devfs
 * publication, and per-device book-keeping.  Backends are pure ops
 * vectors plugged in via audio_register_device().
 */

#include <stdio.h>
#include <string.h>

#include <drivers/audio/audio.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <sys/audioio.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/major.h>
#include <sys/proc.h>
#include <vfs/vfs.h>

static audio_dev_t *audio_devices_head;
static spinlock_t audio_dev_lock = SPINLOCK_INIT("audio_dev");

/*
 * One pair of fs_node_t per registered audio_dev_t (audio + audioctl).
 * The static arrays match AUDIO_MAX_DEVICES so we never heap-allocate
 * during early boot.
 */
static fs_node_t audio_nodes[AUDIO_MAX_DEVICES];
static fs_node_t audioctl_nodes[AUDIO_MAX_DEVICES];

static audio_dev_t *audio_dev_for_node(fs_node_t *node)
{
	if (node == NULL)
		return NULL;
	return (audio_dev_t *)node->impl;
}

/* ----------------------------------------------------------------- */
/* Defaults / merge / validation                                     */
/* ----------------------------------------------------------------- */

void audio_default_info(audio_info_t *info)
{
	memset(info, 0, sizeof(*info));

	info->play.sample_rate = 44100;
	info->play.channels    = 2;
	info->play.precision   = 16;
	info->play.encoding    = AUDIO_ENCODING_SLINEAR_LE;
	info->play.gain        = AUDIO_MAX_GAIN / 2;
	info->play.balance     = AUDIO_MID_BALANCE;
	info->play.buffer_size = AUDIO_DEFAULT_BLOCKSIZE * AUDIO_DEFAULT_HIWAT;

	info->record.sample_rate = 44100;
	info->record.channels    = 2;
	info->record.precision   = 16;
	info->record.encoding    = AUDIO_ENCODING_SLINEAR_LE;
	info->record.gain        = AUDIO_MAX_GAIN / 2;
	info->record.balance     = AUDIO_MID_BALANCE;
	info->record.buffer_size = AUDIO_DEFAULT_BLOCKSIZE * AUDIO_DEFAULT_HIWAT;

	info->monitor_gain = 0;
	info->mode         = AUMODE_PLAY;
	info->blocksize    = AUDIO_DEFAULT_BLOCKSIZE;
	info->hiwat        = AUDIO_DEFAULT_HIWAT;
	info->lowat        = AUDIO_DEFAULT_LOWAT;
}

#define MERGE_U32(b, o, f) do { \
	if ((o)->f != AUDIO_NOTSET_U32) (b)->f = (o)->f; \
} while (0)

#define MERGE_U8(b, o, f) do { \
	if ((o)->f != AUDIO_NOTSET_U8) (b)->f = (o)->f; \
} while (0)

static void audio_merge_prinfo(audio_prinfo_t *base, const audio_prinfo_t *o)
{
	MERGE_U32(base, o, sample_rate);
	MERGE_U32(base, o, channels);
	MERGE_U32(base, o, precision);
	MERGE_U32(base, o, encoding);
	MERGE_U32(base, o, gain);
	MERGE_U32(base, o, port);
	MERGE_U32(base, o, seek);
	MERGE_U32(base, o, avail_ports);
	MERGE_U32(base, o, buffer_size);
	MERGE_U32(base, o, samples);
	MERGE_U32(base, o, eof);
	MERGE_U8(base, o, pause);
	MERGE_U8(base, o, error);
	MERGE_U8(base, o, waiting);
	MERGE_U8(base, o, balance);
	MERGE_U8(base, o, open);
	MERGE_U8(base, o, active);
}

void audio_merge_info(audio_info_t *base, const audio_info_t *o)
{
	audio_merge_prinfo(&base->play, &o->play);
	audio_merge_prinfo(&base->record, &o->record);
	MERGE_U32(base, o, monitor_gain);
	MERGE_U32(base, o, mode);
	MERGE_U32(base, o, blocksize);
	MERGE_U32(base, o, hiwat);
	MERGE_U32(base, o, lowat);
}

#undef MERGE_U32
#undef MERGE_U8

static int audio_encoding_known(uint32_t enc)
{
	switch (enc) {
	case AUDIO_ENCODING_NONE:
	case AUDIO_ENCODING_ULAW:
	case AUDIO_ENCODING_ALAW:
	case AUDIO_ENCODING_PCM16:
	case AUDIO_ENCODING_PCM8:
	case AUDIO_ENCODING_ADPCM:
	case AUDIO_ENCODING_SLINEAR_LE:
	case AUDIO_ENCODING_SLINEAR_BE:
	case AUDIO_ENCODING_ULINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_BE:
	case AUDIO_ENCODING_SLINEAR:
	case AUDIO_ENCODING_ULINEAR:
		return 1;
	default:
		return 0;
	}
}

static int audio_validate_prinfo(audio_prinfo_t *p)
{
	if (!audio_encoding_known(p->encoding)) {
		return -EINVAL;
	}
	/* Channels: 1, 2, or up to 8 (multichannel). */
	if (p->channels == 0 || p->channels > 8) {
		return -EINVAL;
	}
	/* Sample rate: clamp to a sane range. */
	if (p->sample_rate < 4000 || p->sample_rate > 192000) {
		return -EINVAL;
	}
	/* Precision must match encoding's natural width to keep the
	 * framework simple — backends that genuinely support oddball
	 * combinations can override in set_params. */
	switch (p->encoding) {
	case AUDIO_ENCODING_PCM8:
	case AUDIO_ENCODING_ULINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_BE:
	case AUDIO_ENCODING_ULINEAR:
	case AUDIO_ENCODING_ULAW:
	case AUDIO_ENCODING_ALAW:
		if (p->precision != 8) {
			return -EINVAL;
		}
		break;
	case AUDIO_ENCODING_PCM16:
	case AUDIO_ENCODING_SLINEAR_LE:
	case AUDIO_ENCODING_SLINEAR_BE:
	case AUDIO_ENCODING_SLINEAR:
		if (p->precision != 16) {
			return -EINVAL;
		}
		break;
	default:
		break;
	}
	if (p->gain > AUDIO_MAX_GAIN) {
		p->gain = AUDIO_MAX_GAIN;
	}
	if (p->balance > AUDIO_RIGHT_BAL) {
		p->balance = AUDIO_RIGHT_BAL;
	}
	return 0;
}

int audio_validate_info(audio_info_t *info)
{
	int rc;

	rc = audio_validate_prinfo(&info->play);
	if (rc != 0) {
		return rc;
	}
	rc = audio_validate_prinfo(&info->record);
	if (rc != 0) {
		return rc;
	}
	if (info->monitor_gain > AUDIO_MAX_GAIN) {
		info->monitor_gain = AUDIO_MAX_GAIN;
	}
	if (info->blocksize == 0) {
		info->blocksize = AUDIO_DEFAULT_BLOCKSIZE;
	}
	if (info->hiwat == 0) {
		info->hiwat = AUDIO_DEFAULT_HIWAT;
	}
	if (info->lowat == 0 || info->lowat >= info->hiwat) {
		info->lowat = info->hiwat - 1;
	}
	if ((info->mode & ~(AUMODE_PLAY | AUMODE_RECORD | AUMODE_PLAY_ALL)) != 0) {
		return -EINVAL;
	}
	if (info->mode == 0) {
		return -EINVAL;
	}
	return 0;
}

/* ----------------------------------------------------------------- */
/* ioctl dispatch                                                    */
/* ----------------------------------------------------------------- */

int audio_ioctl_dispatch(audio_dev_t *dev, uint32_t request, void *arg)
{
	if (dev == NULL) {
		return -EINVAL;
	}

	switch (request) {
	case AUDIO_GETINFO: {
		if (arg == NULL) {
			return -EINVAL;
		}
		if (copyout(&dev->current, arg, sizeof(audio_info_t)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case AUDIO_SETINFO: {
		audio_info_t overlay;
		audio_info_t merged;
		int rc;

		if (arg == NULL) {
			return -EINVAL;
		}
		if (copyin(arg, &overlay, sizeof(overlay)) != 0) {
			return -EFAULT;
		}
		merged = dev->current;
		audio_merge_info(&merged, &overlay);
		rc = audio_validate_info(&merged);
		if (rc != 0) {
			return rc;
		}
		if (dev->ops != NULL && dev->ops->set_params != NULL) {
			/*
			 * Program the hardware for the format it will
			 * actually receive.  The framework converts ULAW,
			 * ALAW, unsigned and big-endian streams to signed
			 * 16-bit LE on the way through, so handing the
			 * backend the application's encoding would set the
			 * codec up to decode something it is never sent --
			 * and, for the 8-bit encodings, at half the sample
			 * width the converted data actually carries.
			 */
			audio_info_t hw = merged;

			audio_hw_prinfo(&merged.play, &hw.play);
			audio_hw_prinfo(&merged.record, &hw.record);
			rc = dev->ops->set_params(dev, &hw);
			if (rc != 0) {
				return rc;
			}
		}
		dev->current = merged;
		if (copyout(&dev->current, arg, sizeof(audio_info_t)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case AUDIO_DRAIN:
		if (dev->ops != NULL && dev->ops->drain != NULL) {
			return dev->ops->drain(dev);
		}
		return 0;

	case AUDIO_FLUSH:
		if (dev->ops != NULL && dev->ops->flush != NULL) {
			return dev->ops->flush(dev);
		}
		return 0;

	case AUDIO_GETDEV: {
		audio_device_t info;

		if (arg == NULL) {
			return -EINVAL;
		}
		memset(&info, 0, sizeof(info));
		if (dev->ops != NULL && dev->ops->get_devinfo != NULL) {
			dev->ops->get_devinfo(dev, &info);
		} else {
			snprintf(info.name, sizeof(info.name), "audio%d", dev->unit);
		}
		if (copyout(&info, arg, sizeof(info)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case AUDIO_GETPROPS: {
		int props = AUDIO_PROP_PLAYBACK;

		if (arg == NULL) {
			return -EINVAL;
		}
		if (dev->ops != NULL && dev->ops->get_props != NULL) {
			props = dev->ops->get_props(dev);
		}
		if (copyout(&props, arg, sizeof(props)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case AUDIO_GETFD: {
		int v = dev->full_duplex;
		if (arg == NULL) {
			return -EINVAL;
		}
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	case AUDIO_SETFD: {
		int v;
		int props;

		if (arg == NULL) {
			return -EINVAL;
		}
		if (copyin(arg, &v, sizeof(v)) != 0) {
			return -EFAULT;
		}
		props = (dev->ops != NULL && dev->ops->get_props != NULL) ?
			dev->ops->get_props(dev) : 0;
		if (v && !(props & AUDIO_PROP_FULLDUPLEX)) {
			return -EINVAL;
		}
		dev->full_duplex = v ? 1 : 0;
		return 0;
	}

	case AUDIO_RERROR: {
		int v = dev->current.record.error;
		if (arg == NULL) {
			return -EINVAL;
		}
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		dev->current.record.error = 0;
		return 0;
	}

	case AUDIO_WSEEK: {
		uint32_t v = dev->current.play.samples;
		if (arg == NULL) {
			return -EINVAL;
		}
		if (copyout(&v, arg, sizeof(v)) != 0) {
			return -EFAULT;
		}
		return 0;
	}

	default:
		return -ENOTTY;
	}
}

/* ----------------------------------------------------------------- */
/* fs_node_t glue                                                    */
/* ----------------------------------------------------------------- */

/* ----------------------------------------------------------------- */
/* Encoding conversion                                               */
/* ----------------------------------------------------------------- */
/*
 * The framework accepts every encoding audio_validate_prinfo() knows
 * about, but no backend ever looked at the field: ULAW, ALAW, unsigned
 * and big-endian streams were handed to the hardware as though they were
 * signed little-endian PCM, and came out as noise.  Convert here instead,
 * once, so every backend sees one format.
 *
 * The validator constrains the problem usefully: companded and unsigned
 * encodings are 8-bit, signed linear ones are 16-bit.  So the whole
 * matrix is "expand 8-bit to signed 16-bit LE", "byteswap", or "nothing".
 */

/* G.711 mu-law expansion (CCITT / Sun reference implementation). */
static int16_t audio_ulaw_to_pcm(uint8_t u)
{
	int t;

	u = (uint8_t)~u;
	t = ((u & 0x0F) << 3) + 0x84;
	t <<= ((unsigned)u & 0x70) >> 4;
	return (int16_t)((u & 0x80) ? (0x84 - t) : (t - 0x84));
}

/* G.711 A-law expansion. */
static int16_t audio_alaw_to_pcm(uint8_t a)
{
	int t, seg;

	a ^= 0x55;
	t = (a & 0x0F) << 4;
	seg = ((unsigned)a & 0x70) >> 4;
	switch (seg) {
	case 0:  t += 8;                       break;
	case 1:  t += 0x108;                   break;
	default: t += 0x108; t <<= seg - 1;    break;
	}
	return (int16_t)((a & 0x80) ? t : -t);
}

/* True when the encoding is already what backends expect. */
static int audio_enc_is_native(uint32_t enc, uint32_t prec)
{
	/* i386 is little-endian, so the "native" spellings are LE. */
	return prec == 16 && (enc == AUDIO_ENCODING_SLINEAR_LE ||
	                      enc == AUDIO_ENCODING_SLINEAR ||
	                      enc == AUDIO_ENCODING_PCM16);
}

unsigned audio_conv_ratio(uint32_t enc, uint32_t prec)
{
	if (audio_enc_is_native(enc, prec)) {
		return 1;
	}
	/* Every non-native encoding this framework validates is 8-bit
	 * except the big-endian signed ones, which only need a swap. */
	return prec == 8 ? 2 : 1;
}

void audio_hw_prinfo(const audio_prinfo_t *sw, audio_prinfo_t *hw)
{
	*hw = *sw;
	if (audio_enc_is_native(sw->encoding, sw->precision)) {
		return;
	}
	hw->encoding = AUDIO_ENCODING_SLINEAR_LE;
	/*
	 * 8-bit sources are widened rather than converted in place.  Signed
	 * 8-bit is a format plenty of codecs decline -- QEMU's HDA codec
	 * advertises 16-bit only -- and widening costs one shift, so it is
	 * both simpler and more portable than asking for 8-bit output.
	 */
	if (sw->precision == 8) {
		hw->precision = 16;
	}
}

size_t audio_convert(uint32_t enc, uint32_t prec, const uint8_t *in,
		     size_t in_len, uint8_t *out)
{
	size_t i;
	size_t n = 0;

	if (audio_enc_is_native(enc, prec)) {
		memcpy(out, in, in_len);
		return in_len;
	}

	if (prec == 8) {
		for (i = 0; i < in_len; i++) {
			int16_t s;

			switch (enc) {
			case AUDIO_ENCODING_ULAW:
				s = audio_ulaw_to_pcm(in[i]);
				break;
			case AUDIO_ENCODING_ALAW:
				s = audio_alaw_to_pcm(in[i]);
				break;
			default:
				/* PCM8 / ULINEAR{,_LE,_BE}: unsigned, so
				 * recentre on zero and widen.  Byte order is
				 * meaningless at one byte per sample. */
				s = (int16_t)(((int)in[i] - 128) << 8);
				break;
			}
			out[n++] = (uint8_t)(s & 0xFF);
			out[n++] = (uint8_t)((s >> 8) & 0xFF);
		}
		return n;
	}

	/* 16-bit big-endian: swap into little-endian.  A trailing odd byte
	 * cannot be swapped on its own; drop it rather than emit a sample
	 * built from the next write's first byte. */
	for (i = 0; i + 1 < in_len; i += 2) {
		out[n++] = in[i + 1];
		out[n++] = in[i];
	}
	return n;
}

size_t audio_node_write(fs_node_t *node, off_t offset, size_t size,
			       const uint8_t *buffer)
{
	audio_dev_t *dev = audio_dev_for_node(node);
	void *me;
	int rc;

	(void)offset;
	if (dev == NULL || dev->ops == NULL || dev->ops->write == NULL) {
		return 0;
	}

	/*
	 * Exclusive playback.  The backend's software FIFO / DMA path is a
	 * single-producer design; two writers running concurrently corrupt
	 * the FIFO and both wedge forever in an uninterruptible D-state ("more
	 * than one program using the audio device -> both hang, unkillable").
	 * Claim the device for the first writer's thread; any other thread —
	 * including a second thread of the SAME process — gets -EBUSY (write()
	 * fails, the player exits cleanly) instead of corrupting it.  Keying
	 * per-process would let sibling threads race the FIFO.  Kernel-context
	 * writes (no thread) are not arbitrated.
	 */
	me = current_thread ? (void *)current_thread : NULL;
	if (me != NULL) {
		spinlock_acquire(&audio_dev_lock);
		if (dev->play_owner == NULL) {
			dev->play_owner = me;
		}
		if (dev->play_owner != me) {
			spinlock_release(&audio_dev_lock);
			return (size_t)-EBUSY;
		}
		spinlock_release(&audio_dev_lock);
	}

	/*
	 * Fast path: the application is already writing what the backend
	 * wants, so hand the buffer straight down.  This is every ordinary
	 * player, and it is byte-for-byte what happened before conversion
	 * existed.
	 */
	if (audio_conv_ratio(dev->current.play.encoding,
	                     dev->current.play.precision) == 1 &&
	    dev->current.play.encoding != AUDIO_ENCODING_SLINEAR_BE) {
		rc = dev->ops->write(dev, buffer, size);
		if (rc < 0) {
			/* Propagate the backend errno (e.g. -EINTR from a
			 * killed wait, -EBUSY) so write() fails instead of
			 * silently returning 0 and spinning the caller. */
			return (size_t)rc;
		}
		dev->current.play.samples += (uint32_t)rc;
		return (size_t)rc;
	}

	/*
	 * Otherwise translate in bounded chunks through the device's staging
	 * buffer.  The byte count returned is always in the *caller's*
	 * units, so a partial backend write has to be scaled back down.
	 */
	{
		uint32_t enc = dev->current.play.encoding;
		uint32_t prec = dev->current.play.precision;
		unsigned ratio = audio_conv_ratio(enc, prec);
		size_t done = 0;

		while (done < size) {
			size_t chunk = size - done;
			size_t outn;

			if (chunk > AUDIO_CONV_IN) {
				chunk = AUDIO_CONV_IN;
			}
			/* Keep 16-bit samples whole across chunks. */
			if (prec == 16 && (chunk & 1) != 0) {
				chunk--;
			}
			if (chunk == 0) {
				break;
			}
			outn = audio_convert(enc, prec, buffer + done, chunk,
			                     dev->conv_buf);
			if (outn == 0) {
				break;
			}
			rc = dev->ops->write(dev, dev->conv_buf, outn);
			if (rc < 0) {
				/* Report the error only if nothing landed;
				 * otherwise report the short write. */
				return done ? done : (size_t)rc;
			}
			dev->current.play.samples += (uint32_t)rc;
			done += (size_t)rc / ratio;
			if ((size_t)rc < outn) {
				break;   /* backend took less than offered */
			}
		}
		return done;
	}
}

size_t audio_node_read(fs_node_t *node, off_t offset, size_t size,
			      uint8_t *buffer)
{
	audio_dev_t *dev = audio_dev_for_node(node);
	int rc;

	(void)offset;
	if (dev == NULL || dev->ops == NULL || dev->ops->read == NULL) {
		return 0;
	}
	rc = dev->ops->read(dev, buffer, size);
	if (rc < 0) {
		return 0;
	}
	dev->current.record.samples += (uint32_t)rc;
	return (size_t)rc;
}

static int audio_node_ioctl(fs_node_t *node, uint32_t request, void *arg)
{
	audio_dev_t *dev = audio_dev_for_node(node);

	return audio_ioctl_dispatch(dev, request, arg);
}

void *audio_node_mmap(fs_node_t *node, void *addr, size_t length,
			     int prot, int flags, off_t offset)
{
	audio_dev_t *dev = audio_dev_for_node(node);

	if (dev == NULL || dev->ops == NULL || dev->ops->mmap == NULL) {
		return (void *)-1;
	}
	return dev->ops->mmap(dev, addr, length, prot, flags, offset);
}

void audio_node_open(fs_node_t *node)
{
	audio_dev_t *dev = audio_dev_for_node(node);
	int first_open = 0;

	if (dev == NULL) {
		return;
	}
	spinlock_acquire(&audio_dev_lock);
	dev->open_refs++;
	first_open = (dev->open_refs == 1);
	spinlock_release(&audio_dev_lock);
	if (first_open && dev->ops != NULL && dev->ops->open != NULL) {
		(void)dev->ops->open(dev, dev->current.mode);
	}
}

void audio_node_close(fs_node_t *node)
{
	audio_dev_t *dev = audio_dev_for_node(node);
	int last_close = 0;

	if (dev == NULL) {
		return;
	}
	spinlock_acquire(&audio_dev_lock);
	if (dev->open_refs == 0) {
		spinlock_release(&audio_dev_lock);
		return;
	}
	dev->open_refs--;
	last_close = (dev->open_refs == 0);
	/* Release the exclusive playback claim when its owning thread closes
	 * (covers exit() too: proc_exit closes fds in the owner's context), or
	 * whenever the device falls fully idle. */
	if (last_close ||
	    (current_thread && dev->play_owner == (void *)current_thread)) {
		dev->play_owner = NULL;
	}
	spinlock_release(&audio_dev_lock);
	if (last_close && dev->ops != NULL && dev->ops->close != NULL) {
		(void)dev->ops->close(dev);
	}
}

/* ----------------------------------------------------------------- */
/* Registration                                                      */
/* ----------------------------------------------------------------- */

int audio_register_device(audio_dev_t *dev)
{
	int unit;
	fs_node_t *audio_n;
	fs_node_t *audioctl_n;

	if (dev == NULL || dev->ops == NULL) {
		return -EINVAL;
	}
	for (unit = 0; unit < AUDIO_MAX_DEVICES; unit++) {
		if (audio_nodes[unit].impl == 0) {
			break;
		}
	}
	if (unit >= AUDIO_MAX_DEVICES) {
		return -EBUSY;
	}

	dev->unit = unit;
	dev->open_refs = 0;
	dev->full_duplex = 0;
	dev->next = audio_devices_head;
	audio_devices_head = dev;

	audio_default_info(&dev->current);

	/*
	 * Push the advertised defaults to the hardware now.  Without this,
	 * the codec keeps its own boot-time rate (48 kHz on AC'97/HDA,
	 * something else on SB16), and userspace `cat data.pcm > /dev/audio0`
	 * with the kernel's "current" 44.1 kHz expectation plays too fast.
	 * Failures here are non-fatal — driver may not need set_params or
	 * may apply on first start; current/audio_info still reflects the
	 * kernel-side defaults.
	 */
	if (dev->ops != NULL && dev->ops->set_params != NULL) {
		audio_info_t hw = dev->current;

		audio_hw_prinfo(&dev->current.play, &hw.play);
		audio_hw_prinfo(&dev->current.record, &hw.record);
		(void)dev->ops->set_params(dev, &hw);
	}

	audio_n = &audio_nodes[unit];
	memset(audio_n, 0, sizeof(*audio_n));
	snprintf(audio_n->name, sizeof(audio_n->name), "audio%d", unit);
	audio_n->flags = FS_CHARDEVICE;
	/*
	 * /dev/audio*: 0660 root:audio.  Members of the audio group
	 * get read/write; everyone else is denied.  Without this the
	 * nodes inherited mode 0 (memset → bzero) and only root could
	 * open them via the kernel's uid-0 bypass.
	 */
	audio_n->mask  = 0660;
	audio_n->uid   = GID_ROOT;
	audio_n->gid   = GID_AUDIO;
	audio_n->read  = audio_node_read;
	audio_n->write = audio_node_write;
	audio_n->ioctl = audio_node_ioctl;
	audio_n->mmap  = audio_node_mmap;
	audio_n->open  = audio_node_open;
	audio_n->close = audio_node_close;
	audio_n->impl = (uintptr_t)dev;
	audio_n->rdev  = makedev(SOUND_MAJOR, unit * 2);
	devfs_register_device(audio_n);

	audioctl_n = &audioctl_nodes[unit];
	memset(audioctl_n, 0, sizeof(*audioctl_n));
	snprintf(audioctl_n->name, sizeof(audioctl_n->name), "audioctl%d", unit);
	audioctl_n->flags = FS_CHARDEVICE;
	/* audioctl is the mixer / settings sibling — same policy as the
	 * data node so a user that can play audio can adjust its
	 * parameters. */
	audioctl_n->mask  = 0660;
	audioctl_n->uid   = GID_ROOT;
	audioctl_n->gid   = GID_AUDIO;
	audioctl_n->ioctl = audio_node_ioctl;
	audioctl_n->open  = audio_node_open;
	audioctl_n->close = audio_node_close;
	audioctl_n->impl = (uintptr_t)dev;
	audioctl_n->rdev  = makedev(SOUND_MAJOR, unit * 2 + 1);
	devfs_register_device(audioctl_n);

	kprintf("audio: registered %s as /dev/audio%d\n", dev->name, unit);

	/* /dev/audio -> /dev/audio0 alias.  NetBSD-side userland (and most
	 * Sun-compat audio code, including mpg123's output_sun module)
	 * opens /dev/audio without a unit suffix; absent this symlink the
	 * open fails with ENOENT.  Register the alias once, on the first
	 * device to come up regardless of which driver got there first. */
	if (unit == 0) {
		(void)devfs_register_alias("audio", "audio0");
		(void)devfs_register_alias("audioctl", "audioctl0");
	}

	/*
	 * Publish the OSS frontend (/dev/dspN, plus the /dev/dsp alias for
	 * unit 0) over the same backend.  This is personality-agnostic: the
	 * node is visible to native, Linux, and FreeBSD programs alike, and
	 * its ioctl dispatch matches OSS commands by group+number so every
	 * personality's _IOWR encoding routes correctly.
	 */
	oss_register_device(dev, unit);

	return 0;
}

int audio_have_device(void)
{
	return audio_devices_head != NULL;
}

void audio_unregister_device(audio_dev_t *dev)
{
	audio_dev_t **cursor;

	if (dev == NULL) {
		return;
	}
	for (cursor = &audio_devices_head; *cursor != NULL;
	     cursor = &(*cursor)->next) {
		if (*cursor == dev) {
			*cursor = dev->next;
			break;
		}
	}
	if (dev->unit >= 0 && dev->unit < AUDIO_MAX_DEVICES) {
		audio_nodes[dev->unit].impl = 0;
		audioctl_nodes[dev->unit].impl = 0;
	}
}



void audio_init(void)
{
	memset(audio_nodes, 0, sizeof(audio_nodes));
	memset(audioctl_nodes, 0, sizeof(audioctl_nodes));
	audio_devices_head = NULL;

	hda_init();
	ac97_init();
	sb16_init();
	/*
	 * The null backend registers as /dev/audio0 and silently swallows
	 * playback; when it claims the first unit it shadows a real device that
	 * registers later (e.g. USB audio, enumerated after audio_init()), so
	 * /dev/audio plays to the bit bucket.  Keep it off by default and gate it
	 * behind the "audio_null" boot argument for hardware-less testing.
	 */
	if (cmdline_has("audio_null")) {
		null_audio_init();
	}
}
