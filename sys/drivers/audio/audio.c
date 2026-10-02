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
#include <sys/poll.h>
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
	if (p->channels == 0 || p->channels > AUDIO_MAX_CHANNELS) {
		return -EINVAL;
	}
	/* Sample rate: clamp to a sane range. */
	if (p->sample_rate < 4000 || p->sample_rate > 192000) {
		return -EINVAL;
	}
	/*
	 * Linear PCM may be 8, 16, 24 (packed, three bytes) or 32 bits wide;
	 * the framework converts every width to what the backend plays.  The
	 * companded encodings and the fixed-width PCM8/PCM16 aliases have
	 * exactly one width.
	 */
	switch (p->encoding) {
	case AUDIO_ENCODING_PCM8:
	case AUDIO_ENCODING_ULAW:
	case AUDIO_ENCODING_ALAW:
		if (p->precision != 8) {
			return -EINVAL;
		}
		break;
	case AUDIO_ENCODING_PCM16:
		if (p->precision != 16) {
			return -EINVAL;
		}
		break;
	default:
		if (p->precision != 8 && p->precision != 16 &&
		    p->precision != 24 && p->precision != 32) {
			return -EINVAL;
		}
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
		audio_info_t info;

		if (arg == NULL) {
			return -EINVAL;
		}
		info = dev->current;
		info.play.seek = audio_play_queued_samples(dev);
		if (copyout(&info, arg, sizeof(audio_info_t)) != 0) {
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
		rc = audio_apply_info(dev, &merged);
		if (rc != 0) {
			return rc;
		}
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
		/* The discarded audio takes its resampler history with it. */
		audio_conv_reset(&dev->conv);
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
 * Backends play one sample format -- signed 16-bit little-endian -- at a
 * channel count and rate they accepted in set_params().  Applications may
 * write any linear PCM from 8 to 32 bits (signed or unsigned, either byte
 * order), G.711, 1 to 8 channels, 4 kHz to 192 kHz.  Each application
 * frame is decoded to 32-bit full scale, mixed to the backend's channel
 * count, resampled to its rate, and rounded to 16 bits last, so the
 * extra precision of a 24- or 32-bit source survives the mixing and
 * resampling arithmetic.
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

/* True when a write can go to the backend byte for byte. */
static int audio_is_passthrough(const audio_prinfo_t *sw,
				const audio_prinfo_t *hw)
{
	return audio_enc_is_native(sw->encoding, sw->precision) &&
	       sw->channels == hw->channels &&
	       sw->sample_rate == hw->sample_rate;
}

void audio_hw_prinfo(const audio_prinfo_t *sw, audio_prinfo_t *hw)
{
	/*
	 * Whatever the width, the backend gets 16 bits: 8-bit sources are
	 * widened (signed 8-bit is a format plenty of codecs decline --
	 * QEMU's HDA codec advertises 16-bit only) and 24/32-bit ones are
	 * rounded down, which every codec here can play.
	 */
	*hw = *sw;
	hw->encoding  = AUDIO_ENCODING_SLINEAR_LE;
	hw->precision = 16;
}

uint32_t audio_frame_bytes(const audio_prinfo_t *p)
{
	return p->channels * (p->precision / 8);
}

/* One sample of any validated encoding, scaled to 32-bit full scale. */
static int32_t audio_decode_sample(uint32_t enc, uint32_t prec,
				   const uint8_t *p)
{
	uint32_t nb = prec / 8;
	uint32_t u = 0;
	uint32_t i;
	int big;

	switch (enc) {
	case AUDIO_ENCODING_ULAW:
		return (int32_t)((uint32_t)(uint16_t)audio_ulaw_to_pcm(p[0])
				 << 16);
	case AUDIO_ENCODING_ALAW:
		return (int32_t)((uint32_t)(uint16_t)audio_alaw_to_pcm(p[0])
				 << 16);
	default:
		break;
	}

	big = (enc == AUDIO_ENCODING_SLINEAR_BE ||
	       enc == AUDIO_ENCODING_ULINEAR_BE);
	for (i = 0; i < nb; i++) {
		u = (u << 8) | p[big ? i : nb - 1 - i];
	}
	u <<= 32 - prec;
	/* Unsigned: the midpoint is silence, so flip the top bit. */
	if (enc == AUDIO_ENCODING_ULINEAR_LE ||
	    enc == AUDIO_ENCODING_ULINEAR_BE ||
	    enc == AUDIO_ENCODING_ULINEAR ||
	    enc == AUDIO_ENCODING_PCM8) {
		u ^= 0x80000000u;
	}
	return (int32_t)u;
}

/*
 * Downmix weights, Q15.  Multichannel frames are taken in the order SDL,
 * WAVE_FORMAT_EXTENSIBLE and ALSA share for 5.1 and 7.1; the centre and
 * surrounds go to both sides at -3 dB (ITU-R BS.775) and LFE is dropped.
 */
enum { CH_FL, CH_FR, CH_FC, CH_LFE, CH_BL, CH_BR, CH_BC, CH_SL, CH_SR };

static const uint8_t audio_layout[AUDIO_MAX_CHANNELS + 1][AUDIO_MAX_CHANNELS] = {
	[3] = { CH_FL, CH_FR, CH_LFE },
	[4] = { CH_FL, CH_FR, CH_BL, CH_BR },
	[5] = { CH_FL, CH_FR, CH_LFE, CH_BL, CH_BR },
	[6] = { CH_FL, CH_FR, CH_FC, CH_LFE, CH_BL, CH_BR },
	[7] = { CH_FL, CH_FR, CH_FC, CH_LFE, CH_BC, CH_SL, CH_SR },
	[8] = { CH_FL, CH_FR, CH_FC, CH_LFE, CH_BL, CH_BR, CH_SL, CH_SR },
};

#define Q15_ONE   32768
#define Q15_M3DB  23170 /* 1/sqrt(2) */

static const int32_t audio_left_w[] = {
	[CH_FL] = Q15_ONE, [CH_FR] = 0, [CH_FC] = Q15_M3DB, [CH_LFE] = 0,
	[CH_BL] = Q15_M3DB, [CH_BR] = 0, [CH_BC] = Q15_M3DB,
	[CH_SL] = Q15_M3DB, [CH_SR] = 0,
};

/* A role's right-hand weight is its mirror image's left-hand one. */
static int32_t audio_right_w(uint8_t role)
{
	switch (role) {
	case CH_FL: return audio_left_w[CH_FR];
	case CH_FR: return audio_left_w[CH_FL];
	case CH_BL: return audio_left_w[CH_BR];
	case CH_BR: return audio_left_w[CH_BL];
	case CH_SL: return audio_left_w[CH_SR];
	case CH_SR: return audio_left_w[CH_SL];
	default:    return audio_left_w[role];
	}
}

/* Mix one frame from `s` channels to `d`. */
static void audio_mix(const int32_t *in, uint32_t s, int32_t *out, uint32_t d)
{
	uint32_t c;

	if (s == d) {
		memcpy(out, in, d * sizeof(*out));
	} else if (s == 1) {
		/* Mono: the same signal on every speaker. */
		for (c = 0; c < d; c++) {
			out[c] = in[0];
		}
	} else if (d == 1) {
		int64_t sum = 0;

		for (c = 0; c < s; c++) {
			sum += in[c];
		}
		out[0] = (int32_t)(sum / (int64_t)s);
	} else if (d == 2) {
		/*
		 * Weighted sum per side, divided by the sum of the weights so
		 * that full scale on every input channel is full scale out:
		 * the mix can never clip.
		 */
		int64_t l = 0, r = 0, wl = 0, wr = 0;

		for (c = 0; c < s; c++) {
			uint8_t role = audio_layout[s][c];
			int32_t a = audio_left_w[role];
			int32_t b = audio_right_w(role);

			l += (int64_t)in[c] * a;
			r += (int64_t)in[c] * b;
			wl += a;
			wr += b;
		}
		out[0] = (int32_t)(l / wl);
		out[1] = (int32_t)(r / wr);
	} else if (s < d) {
		/* More speakers than channels: fill the first, mute the rest. */
		memcpy(out, in, s * sizeof(*out));
		for (c = s; c < d; c++) {
			out[c] = 0;
		}
	} else {
		/* Fewer, but not stereo: keep the leading channels. */
		memcpy(out, in, d * sizeof(*out));
	}
}

/* Round a 32-bit full-scale sample to 16 bits, little-endian. */
static uint8_t *audio_put_s16(uint8_t *out, int32_t v)
{
	int64_t s = ((int64_t)v + 0x8000) >> 16;

	if (s > 32767) {
		s = 32767;
	}
	out[0] = (uint8_t)(s & 0xFF);
	out[1] = (uint8_t)(((uint64_t)s >> 8) & 0xFF);
	return out + 2;
}

void audio_conv_reset(audio_conv_t *st)
{
	memset(st, 0, sizeof(*st));
}

size_t audio_conv_burst(const audio_prinfo_t *sw, const audio_prinfo_t *hw)
{
	size_t frames = 1;

	/* Upsampling emits up to ceil(dst / src) frames per input frame. */
	if (sw->sample_rate < hw->sample_rate) {
		frames = hw->sample_rate / sw->sample_rate + 2;
	}
	return frames * hw->channels * 2;
}

/*
 * Rate conversion keeps a Q16 phase.  Upsampling interpolates linearly
 * between the previous frame and this one, emitting an output frame every
 * `step` = src/dst of an input frame, so it runs one frame behind.
 * Downsampling averages every input frame that falls in an output frame's
 * span -- a box filter, which keeps the worst of the aliasing out that
 * plain decimation would fold back into the audible band.
 */
size_t audio_conv_frame(audio_conv_t *st, const audio_prinfo_t *sw,
			const audio_prinfo_t *hw, const uint8_t *frame,
			uint8_t *out)
{
	int32_t x[AUDIO_MAX_CHANNELS];
	int32_t m[AUDIO_MAX_CHANNELS];
	uint32_t bps = sw->precision / 8;
	uint32_t d = hw->channels;
	uint32_t c;
	uint8_t *o = out;

	for (c = 0; c < sw->channels; c++) {
		x[c] = audio_decode_sample(sw->encoding, sw->precision,
					   frame + c * bps);
	}
	audio_mix(x, sw->channels, m, d);

	if (sw->sample_rate == hw->sample_rate) {
		for (c = 0; c < d; c++) {
			o = audio_put_s16(o, m[c]);
		}
	} else if (sw->sample_rate < hw->sample_rate) {
		uint32_t step = (uint32_t)(((uint64_t)sw->sample_rate << 16) /
					   hw->sample_rate);

		if (!st->primed) {
			memcpy(st->prev, m, d * sizeof(*m));
			st->primed = 1;
			st->phase = 0;
			return 0;
		}
		while (st->phase < 0x10000u) {
			for (c = 0; c < d; c++) {
				int64_t diff = (int64_t)m[c] - st->prev[c];

				o = audio_put_s16(o, (int32_t)(st->prev[c] +
				    ((diff * st->phase) >> 16)));
			}
			st->phase += step;
		}
		st->phase -= 0x10000u;
		memcpy(st->prev, m, d * sizeof(*m));
	} else {
		uint32_t step = (uint32_t)(((uint64_t)sw->sample_rate << 16) /
					   hw->sample_rate);

		for (c = 0; c < d; c++) {
			st->acc[c] += m[c];
		}
		st->acc_n++;
		st->phase += 0x10000u;
		if (st->phase >= step) {
			for (c = 0; c < d; c++) {
				o = audio_put_s16(o, (int32_t)(st->acc[c] /
				    (int64_t)st->acc_n));
				st->acc[c] = 0;
			}
			st->acc_n = 0;
			st->phase -= step;
		}
	}
	return (size_t)(o - out);
}

/*
 * play.seek: samples written but not yet handed to the DMA engine, in the
 * application's format.  A poll-driven writer sizes its writes from this
 * -- SDL3's Sun/NetBSD backend waits until less than one of its buffers is
 * queued and only then writes, because it writes holding its device lock.
 * Left at 0 it looked permanently empty, every write blocked in the kernel
 * with that lock held, and SDL_ResumeAudioStreamDevice() on another thread
 * could starve behind it for many seconds: a silent start.  A backend that
 * cannot report its queue says 0, as before.
 */
uint32_t audio_play_queued_samples(const audio_dev_t *dev)
{
	int fragsize = 0, fragstotal = 0, fragments = 0, freeb = 0;
	int queued;
	uint32_t bps = dev->current.play.precision / 8;

	if (dev->ops == NULL || dev->ops->get_ospace == NULL || bps == 0 ||
	    dev->ops->get_ospace((audio_dev_t *)dev, &fragsize, &fragstotal,
				 &fragments, &freeb) != 0) {
		return 0;
	}
	queued = fragsize * fragstotal - freeb;
	if (queued <= 0) {
		return 0;
	}
	return (uint32_t)audio_hw_to_app_bytes(dev, queued) / bps;
}

int audio_hw_to_app_bytes(const audio_dev_t *dev, int hw_bytes)
{
	uint64_t app = (uint64_t)audio_frame_bytes(&dev->current.play) *
		       dev->current.play.sample_rate;
	uint64_t hw = (uint64_t)audio_frame_bytes(&dev->hw_play) *
		      dev->hw_play.sample_rate;

	if (hw_bytes <= 0 || app == 0 || hw == 0) {
		return hw_bytes;
	}
	return (int)((uint64_t)hw_bytes * app / hw);
}

/*
 * Find a playback format the backend takes.  The request is tried first,
 * then -- when set_params() refuses it -- stereo in place of more
 * channels, and the two rates every codec here supports; whatever channel
 * count and rate the backend ends up programmed for, the write path
 * converts to.  A backend that cannot hit the requested rate exactly may
 * also report the rate it did set (AC'97 without VRA, SB16 above 44.1 kHz)
 * rather than fail, and that is what gets resampled to.
 */
static int audio_negotiate(audio_dev_t *dev, const audio_info_t *info,
			   audio_info_t *hw)
{
	static const uint32_t fallback_rates[] = { 48000, 44100 };
	uint32_t chans[2], rates[3];
	uint32_t nchans = 0, nrates = 0;
	uint32_t ch, ci, ri;
	int first_rc = 0;

	*hw = *info;
	audio_hw_prinfo(&info->play, &hw->play);
	audio_hw_prinfo(&info->record, &hw->record);

	ch = hw->play.channels;
	if (dev->hw_chan_max != 0 && ch > dev->hw_chan_max) {
		ch = dev->hw_chan_max;
	}
	if (dev->hw_chan_min != 0 && ch < dev->hw_chan_min) {
		ch = dev->hw_chan_min;
	}
	hw->play.channels = ch;
	if (dev->ops == NULL || dev->ops->set_params == NULL) {
		return 0;
	}

	chans[nchans++] = ch;
	if (ch > 2 && (dev->hw_chan_min == 0 || dev->hw_chan_min <= 2)) {
		chans[nchans++] = 2;
	}
	rates[nrates++] = hw->play.sample_rate;
	for (ri = 0; ri < 2; ri++) {
		if (fallback_rates[ri] != hw->play.sample_rate) {
			rates[nrates++] = fallback_rates[ri];
		}
	}

	for (ci = 0; ci < nchans; ci++) {
		for (ri = 0; ri < nrates; ri++) {
			audio_info_t try = *hw;
			int rc;

			try.play.channels = chans[ci];
			try.play.sample_rate = rates[ri];
			rc = dev->ops->set_params(dev, &try);
			if (rc != 0) {
				if (first_rc == 0) {
					first_rc = rc;
				}
				continue;
			}
			/* Trust what the backend reports only within reason. */
			if (try.play.sample_rate < 4000 ||
			    try.play.sample_rate > 192000) {
				try.play.sample_rate = rates[ri];
			}
			try.play.channels  = chans[ci];
			try.play.encoding  = AUDIO_ENCODING_SLINEAR_LE;
			try.play.precision = 16;
			*hw = try;
			return 0;
		}
	}
	return first_rc;
}

int audio_apply_info(audio_dev_t *dev, audio_info_t *info)
{
	audio_info_t hw;
	const audio_prinfo_t *old = &dev->current.play;
	int rc;

	rc = audio_validate_info(info);
	if (rc != 0) {
		return rc;
	}
	rc = audio_negotiate(dev, info, &hw);
	if (rc != 0) {
		return rc;
	}
	/* Conversion state belongs to a format; a gain or blocksize change
	 * must not throw away a partial frame. */
	if (old->encoding != info->play.encoding ||
	    old->precision != info->play.precision ||
	    old->channels != info->play.channels ||
	    old->sample_rate != info->play.sample_rate ||
	    dev->hw_play.channels != hw.play.channels ||
	    dev->hw_play.sample_rate != hw.play.sample_rate) {
		audio_conv_reset(&dev->conv);
	}
	dev->current = *info;
	dev->hw_play = hw.play;
	return 0;
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
	 * Converted output a previous write could not hand over goes first;
	 * until it has, none of this write's input can be taken without
	 * reordering the stream.
	 */
	while (dev->conv.out_len > 0) {
		rc = dev->ops->write(dev, dev->conv_buf + dev->conv.out_off,
				     dev->conv.out_len);
		if (rc < 0) {
			return (size_t)rc;
		}
		if (rc == 0) {
			return (size_t)-EAGAIN;
		}
		dev->conv.out_off += (uint32_t)rc;
		dev->conv.out_len -= (uint32_t)rc;
	}

	/*
	 * Fast path: the application is already writing what the backend
	 * plays, so hand the buffer straight down.  This is every ordinary
	 * 16-bit player, and it is byte-for-byte what happened before
	 * conversion existed.
	 */
	if (audio_is_passthrough(&dev->current.play, &dev->hw_play) &&
	    dev->conv.carry_len == 0) {
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
	 * Otherwise convert frame by frame into the staging buffer and hand
	 * it to the backend whenever another frame's output might not fit.
	 * A trailing partial frame is kept for the next write, so every byte
	 * offered is consumed.  The count returned is in the caller's units:
	 * input bytes whose output reached the backend, plus any carried.
	 */
	{
		const audio_prinfo_t *sw = &dev->current.play;
		const audio_prinfo_t *hw = &dev->hw_play;
		audio_conv_t *st = &dev->conv;
		size_t fb = audio_frame_bytes(sw);
		size_t burst = audio_conv_burst(sw, hw);
		size_t done = 0;     /* input whose output was delivered */
		size_t pending = 0;  /* input converted, not yet delivered */
		size_t outn = 0;
		size_t i = 0;

		while (i < size || outn > 0) {
			if (i < size) {
				const uint8_t *frame;

				if (st->carry_len > 0 || size - i < fb) {
					size_t take = fb - st->carry_len;

					if (take > size - i) {
						take = size - i;
					}
					memcpy(st->carry + st->carry_len,
					       buffer + i, take);
					st->carry_len += (uint32_t)take;
					i += take;
					pending += take;
					if (st->carry_len < fb) {
						continue;  /* partial: keep it */
					}
					frame = st->carry;
					st->carry_len = 0;
				} else {
					frame = buffer + i;
					i += fb;
					pending += fb;
				}
				outn += audio_conv_frame(st, sw, hw, frame,
							 dev->conv_buf + outn);
				if (i < size && outn + burst <= AUDIO_CONV_OUT) {
					continue;
				}
			}
			if (outn > 0) {
				rc = dev->ops->write(dev, dev->conv_buf, outn);
				if (rc < 0 && rc != -EAGAIN && rc != -EINTR) {
					/* The backend cannot play: what was
					 * converted is lost with the write. */
					audio_conv_reset(st);
					dev->current.play.samples +=
						(uint32_t)done;
					return done ? done : (size_t)rc;
				}
				if (rc < 0) {
					rc = 0;
				}
				if ((size_t)rc < outn) {
					/*
					 * Short: a signal, or a non-blocking
					 * descriptor with the buffer full.
					 * Keep the rest for the next write --
					 * its input counts as written -- and
					 * stop here.
					 */
					st->out_off = (uint32_t)rc;
					st->out_len = (uint32_t)(outn - (size_t)rc);
					done += pending;
					if (done == 0) {
						return (size_t)-EAGAIN;
					}
					dev->current.play.samples +=
						(uint32_t)done;
					return done;
				}
				outn = 0;
			}
			done += pending;
			pending = 0;
		}
		/* Carried bytes count: they are kept for the next write. */
		done += pending;
		dev->current.play.samples += (uint32_t)done;
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

/*
 * Writable when the backend has room for at least one fragment.  A backend
 * that reports no occupancy, or wakes no channel poll can sleep on, is
 * always writable.  There is no capture path, so never readable.
 */
int audio_node_poll(fs_node_t *node, void *waiter)
{
	audio_dev_t *dev = audio_dev_for_node(node);
	int fragsize = 0, fragstotal = 0, fragments = 0, freeb = 0;

	if (dev == NULL) {
		return POLLNVAL;
	}
	if (dev->wait_chan == NULL || dev->ops == NULL ||
	    dev->ops->get_ospace == NULL ||
	    dev->ops->get_ospace(dev, &fragsize, &fragstotal, &fragments,
				 &freeb) != 0 ||
	    freeb >= fragsize) {
		return POLLOUT | POLLWRNORM;
	}
	if (waiter != NULL) {
		*(void **)waiter = dev->wait_chan;
	}
	return 0;
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
	if (last_close) {
		/* A partial frame left by the last user is not the next
		 * user's audio. */
		audio_conv_reset(&dev->conv);
	}
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
	audio_conv_reset(&dev->conv);
	{
		audio_info_t hw;

		if (audio_negotiate(dev, &dev->current, &hw) != 0) {
			audio_hw_prinfo(&dev->current.play, &hw.play);
		}
		dev->hw_play = hw.play;
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
	audio_n->poll  = audio_node_poll;
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
