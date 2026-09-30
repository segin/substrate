#define HOST_TEST 1

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Pull the public ABI through the same path userspace will see.  The
 * framework headers under sys/drivers/audio/ are kernel-internal, so we
 * locally redeclare just the bits the test needs and either pull
 * audio.c through directly or supply tiny stubs for kernel glue.
 */
#include <sys/audioio.h>
#include <vfs/vfs.h>
#include <sys/proc.h>
#include <sys/lock.h>

static int devfs_register_calls;
void devfs_register_device(fs_node_t *node) {
	(void)node;
	devfs_register_calls++;
}

int copyin(const void *src, void *dst, size_t size) {
	memcpy(dst, src, size);
	return 0;
}
int copyout(const void *src, void *dst, size_t size) {
	memcpy(dst, src, size);
	return 0;
}

void kprint(const char *str) { (void)str; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

/* Real backends pull in PCI / ISA / DMA infrastructure that has no
 * host equivalent; stub them so audio_init() can call through. */
void ac97_init(void) {}
void sb16_init(void) {}
void hda_init(void) {}

/*
 * audio.c now arbitrates /dev/audio ownership per writing process under a
 * spinlock, and registers short-name devfs aliases.  current_thread == NULL
 * means "kernel context" — the ownership check is skipped, which is what we
 * want for the framework-level tests here.
 */
thread_t *current_thread = NULL;
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
int cmdline_has(const char *key) { (void)key; return 0; }
int devfs_register_alias(const char *path, const char *target) {
	(void)path; (void)target; return 0;
}

/*
 * audio.c includes "audio.h" with a relative path; we point the include
 * search at the directory below.
 */
#include "../../sys/drivers/audio/audio.h"

/*
 * audio_register_device() publishes an OSS frontend for every backend.
 * oss.c drags in the personality-aware ioctl translation and its own
 * kernel glue, none of which this test exercises, so stub the entry
 * point.  Without it the link fails outright -- which it had been doing
 * silently since the OSS frontend landed, leaving a stale binary behind.
 */
struct audio_dev;
void oss_register_device(struct audio_dev *dev, int unit);
void oss_register_device(struct audio_dev *dev, int unit) {
	(void)dev;
	(void)unit;
}

#include "../../sys/drivers/audio/audio.c"
#include "../../sys/drivers/audio/null_audio.c"

/* ----------------------------------------------------------------- */
/* Tests                                                             */
/* ----------------------------------------------------------------- */

static void test_initinfo_marks_all_fields_unset(void) {
	audio_info_t info;
	memset(&info, 0, sizeof(info));
	AUDIO_INITINFO(&info);
	assert(info.play.sample_rate == AUDIO_NOTSET_U32);
	assert(info.play.channels    == AUDIO_NOTSET_U32);
	assert(info.play.precision   == AUDIO_NOTSET_U32);
	assert(info.play.encoding    == AUDIO_NOTSET_U32);
	assert(info.play.gain        == AUDIO_NOTSET_U32);
	assert(info.play.balance     == AUDIO_NOTSET_U8);
	assert(info.play.pause       == AUDIO_NOTSET_U8);
	assert(info.record.sample_rate == AUDIO_NOTSET_U32);
	assert(info.monitor_gain == AUDIO_NOTSET_U32);
	assert(info.mode         == AUDIO_NOTSET_U32);
	assert(info.blocksize    == AUDIO_NOTSET_U32);
	assert(info.hiwat        == AUDIO_NOTSET_U32);
	assert(info.lowat        == AUDIO_NOTSET_U32);
}

static void test_default_info_populates_sensible_defaults(void) {
	audio_info_t info;
	audio_default_info(&info);
	assert(info.play.sample_rate == 44100);
	assert(info.play.channels == 2);
	assert(info.play.precision == 16);
	assert(info.play.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(info.play.balance == AUDIO_MID_BALANCE);
	assert(info.mode == AUMODE_PLAY);
	assert(info.blocksize == AUDIO_DEFAULT_BLOCKSIZE);
}

static void test_merge_only_overwrites_non_sentinel(void) {
	audio_info_t base;
	audio_info_t overlay;

	audio_default_info(&base);
	memset(&overlay, 0, sizeof(overlay));
	AUDIO_INITINFO(&overlay);
	overlay.play.sample_rate = 48000;
	overlay.play.gain = 200;
	overlay.mode = AUMODE_PLAY | AUMODE_RECORD;

	audio_merge_info(&base, &overlay);

	/* Touched fields take the new value. */
	assert(base.play.sample_rate == 48000);
	assert(base.play.gain == 200);
	assert(base.mode == (AUMODE_PLAY | AUMODE_RECORD));

	/* Untouched fields keep defaults. */
	assert(base.play.channels == 2);
	assert(base.play.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(base.blocksize == AUDIO_DEFAULT_BLOCKSIZE);
}

static void test_validate_rejects_unknown_encoding(void) {
	audio_info_t info;
	audio_default_info(&info);
	info.play.encoding = 0xBEEF;
	assert(audio_validate_info(&info) == -EINVAL);
}

static void test_validate_rejects_bad_channel_count(void) {
	audio_info_t info;
	audio_default_info(&info);
	info.play.channels = 0;
	assert(audio_validate_info(&info) == -EINVAL);
	info.play.channels = 99;
	assert(audio_validate_info(&info) == -EINVAL);
}

static void test_validate_rejects_out_of_range_sample_rate(void) {
	audio_info_t info;
	audio_default_info(&info);
	info.play.sample_rate = 500;
	assert(audio_validate_info(&info) == -EINVAL);
	info.play.sample_rate = 1000000;
	assert(audio_validate_info(&info) == -EINVAL);
}

static void test_validate_clamps_gain_and_balance(void) {
	audio_info_t info;
	audio_default_info(&info);
	info.play.gain    = AUDIO_MAX_GAIN + 99;
	info.play.balance = AUDIO_RIGHT_BAL + 50;
	info.monitor_gain = AUDIO_MAX_GAIN + 1234;
	assert(audio_validate_info(&info) == 0);
	assert(info.play.gain == AUDIO_MAX_GAIN);
	assert(info.play.balance == AUDIO_RIGHT_BAL);
	assert(info.monitor_gain == AUDIO_MAX_GAIN);
}

static void test_validate_normalizes_lowat_against_hiwat(void) {
	audio_info_t info;
	audio_default_info(&info);
	info.hiwat = 4;
	info.lowat = 9;     /* >= hiwat is invalid; should clamp */
	assert(audio_validate_info(&info) == 0);
	assert(info.lowat == 3);
}

static void test_validate_rejects_zero_mode(void) {
	audio_info_t info;
	audio_default_info(&info);
	info.mode = 0;
	assert(audio_validate_info(&info) == -EINVAL);
}

static void test_register_publishes_two_devfs_nodes(void) {
	devfs_register_calls = 0;
	memset(audio_nodes, 0, sizeof(audio_nodes));
	memset(audioctl_nodes, 0, sizeof(audioctl_nodes));
	audio_devices_head = NULL;
	null_audio_init();
	assert(devfs_register_calls == 2);
	/*
	 * After registration the impl pointer should point at the registered
	 * audio_dev_t and the names should be /dev/audio0 + /dev/audioctl0.
	 */
	assert(audio_nodes[0].impl != 0);
	assert(audioctl_nodes[0].impl != 0);
	assert(strcmp(audio_nodes[0].name, "audio0") == 0);
	assert(strcmp(audioctl_nodes[0].name, "audioctl0") == 0);
}

static audio_dev_t *get_test_dev(void) {
	return (audio_dev_t *)audio_nodes[0].impl;
}

static void test_ioctl_getinfo_returns_current(void) {
	audio_dev_t *dev = get_test_dev();
	audio_info_t info;

	memset(&info, 0xAA, sizeof(info));
	assert(audio_ioctl_dispatch(dev, AUDIO_GETINFO, &info) == 0);
	assert(info.play.sample_rate == dev->current.play.sample_rate);
	assert(info.mode == dev->current.mode);
}

static void test_ioctl_setinfo_round_trips_through_merge(void) {
	audio_dev_t *dev = get_test_dev();
	audio_info_t set;
	audio_info_t got;

	memset(&set, 0, sizeof(set));
	AUDIO_INITINFO(&set);
	set.play.sample_rate = 22050;
	set.play.gain        = 100;
	set.blocksize        = 2048;

	assert(audio_ioctl_dispatch(dev, AUDIO_SETINFO, &set) == 0);

	memset(&got, 0, sizeof(got));
	assert(audio_ioctl_dispatch(dev, AUDIO_GETINFO, &got) == 0);
	assert(got.play.sample_rate == 22050);
	assert(got.play.gain == 100);
	assert(got.blocksize == 2048);
	/* Channel count stayed at default 2. */
	assert(got.play.channels == 2);
}

static void test_ioctl_setinfo_rejects_invalid(void) {
	audio_dev_t *dev = get_test_dev();
	audio_info_t set;

	memset(&set, 0, sizeof(set));
	AUDIO_INITINFO(&set);
	set.play.encoding  = 0xCAFE;
	set.play.precision = 24;

	assert(audio_ioctl_dispatch(dev, AUDIO_SETINFO, &set) == -EINVAL);
}

static void test_ioctl_getdev_returns_null_backend_id(void) {
	audio_dev_t *dev = get_test_dev();
	audio_device_t info;

	memset(&info, 0xAA, sizeof(info));
	assert(audio_ioctl_dispatch(dev, AUDIO_GETDEV, &info) == 0);
	assert(strcmp(info.name, "null") == 0);
	assert(strcmp(info.version, "1.0") == 0);
}

static void test_ioctl_getprops_advertises_caps(void) {
	audio_dev_t *dev = get_test_dev();
	int props = 0;

	assert(audio_ioctl_dispatch(dev, AUDIO_GETPROPS, &props) == 0);
	assert(props & AUDIO_PROP_PLAYBACK);
	assert(props & AUDIO_PROP_CAPTURE);
	assert(props & AUDIO_PROP_FULLDUPLEX);
}

static void test_ioctl_setfd_requires_fullduplex_and_persists(void) {
	audio_dev_t *dev = get_test_dev();
	int v;

	v = 1;
	assert(audio_ioctl_dispatch(dev, AUDIO_SETFD, &v) == 0);
	v = 0;
	assert(audio_ioctl_dispatch(dev, AUDIO_GETFD, &v) == 0);
	assert(v == 1);
}

static void test_ioctl_unknown_returns_enotty(void) {
	audio_dev_t *dev = get_test_dev();
	assert(audio_ioctl_dispatch(dev, 0xDEADBEEFU, NULL) == -ENOTTY);
}

static void test_ioctl_drain_and_flush_succeed(void) {
	audio_dev_t *dev = get_test_dev();
	assert(audio_ioctl_dispatch(dev, AUDIO_DRAIN, NULL) == 0);
	assert(audio_ioctl_dispatch(dev, AUDIO_FLUSH, NULL) == 0);
}

static void test_ioctl_null_args_rejected(void) {
	audio_dev_t *dev = get_test_dev();
	assert(audio_ioctl_dispatch(dev, AUDIO_GETINFO, NULL) == -EINVAL);
	assert(audio_ioctl_dispatch(dev, AUDIO_SETINFO, NULL) == -EINVAL);
	assert(audio_ioctl_dispatch(dev, AUDIO_GETDEV, NULL) == -EINVAL);
}

/* ----------------------------------------------------------------- */
/* Encoding conversion                                               */
/* ----------------------------------------------------------------- */

static int16_t s16le(const uint8_t *p) {
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static audio_prinfo_t fmt(uint32_t enc, uint32_t prec, uint32_t ch,
			  uint32_t rate) {
	audio_prinfo_t p;

	memset(&p, 0, sizeof(p));
	p.encoding = enc;
	p.precision = prec;
	p.channels = ch;
	p.sample_rate = rate;
	return p;
}

/*
 * Run whole frames of `sw` through the converter into `hw`, from a
 * fresh state.  Returns the output bytes; a trailing partial frame is
 * not consumed.
 */
static size_t conv_to(audio_prinfo_t sw, audio_prinfo_t hw,
		      const uint8_t *in, size_t len, uint8_t *out) {
	audio_conv_t st;
	size_t fb = audio_frame_bytes(&sw);
	size_t i, n = 0;

	audio_conv_reset(&st);
	for (i = 0; i + fb <= len; i += fb) {
		size_t got = audio_conv_frame(&st, &sw, &hw, in + i, out + n);

		assert(got <= audio_conv_burst(&sw, &hw));
		n += got;
	}
	return n;
}

/* The same channel count and rate: only the sample format changes. */
static size_t conv(uint32_t enc, uint32_t prec, uint32_t ch,
		   const uint8_t *in, size_t len, uint8_t *out) {
	return conv_to(fmt(enc, prec, ch, 44100),
		       fmt(AUDIO_ENCODING_SLINEAR_LE, 16, ch, 44100),
		       in, len, out);
}

/* Signed 16-bit LE is what backends want, so it must pass through
 * untouched -- this is the path every ordinary player takes, and the
 * write path skips the converter for it altogether. */
static void test_conv_native_is_passthrough(void) {
	const uint8_t in[8] = { 0x01, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0x34, 0x12 };
	audio_prinfo_t hw = fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 2, 44100);
	audio_prinfo_t sw;
	uint8_t out[16];
	size_t n;

	n = conv(AUDIO_ENCODING_SLINEAR_LE, 16, 2, in, sizeof(in), out);
	assert(n == sizeof(in));
	assert(memcmp(out, in, sizeof(in)) == 0);
	/* The native aliases behave identically. */
	sw = fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 2, 44100);
	assert(audio_is_passthrough(&sw, &hw));
	sw.encoding = AUDIO_ENCODING_SLINEAR;
	assert(audio_is_passthrough(&sw, &hw));
	sw.encoding = AUDIO_ENCODING_PCM16;
	assert(audio_is_passthrough(&sw, &hw));
	/* A different width, channel count or rate is not. */
	sw.precision = 32;
	assert(!audio_is_passthrough(&sw, &hw));
	sw = fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 44100);
	assert(!audio_is_passthrough(&sw, &hw));
	sw = fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 2, 48000);
	assert(!audio_is_passthrough(&sw, &hw));
}

/* Big-endian signed: byteswap, same size. */
static void test_conv_slinear_be_swaps(void) {
	const uint8_t in[4] = { 0x12, 0x34, 0xFF, 0x80 };
	uint8_t out[8];
	size_t n;

	n = conv(AUDIO_ENCODING_SLINEAR_BE, 16, 1, in, sizeof(in), out);
	assert(n == 4);
	assert(s16le(&out[0]) == (int16_t)0x1234);
	assert(s16le(&out[2]) == (int16_t)0xFF80);
}

/* A trailing partial frame produces nothing on its own; the write path
 * carries it into the next write (test_write_carries_partial_frame). */
static void test_conv_slinear_be_drops_odd_tail(void) {
	const uint8_t in[3] = { 0x12, 0x34, 0x56 };
	uint8_t out[8];

	assert(conv(AUDIO_ENCODING_SLINEAR_BE, 16, 1, in, 3, out) == 2);
}

/* 8-bit unsigned recentres on zero and widens to 16-bit. */
static void test_conv_unsigned8_widens(void) {
	const uint8_t in[3] = { 0x80, 0x00, 0xFF };
	uint8_t out[8];
	size_t n;

	n = conv(AUDIO_ENCODING_PCM8, 8, 1, in, sizeof(in), out);
	assert(n == 6);
	assert(s16le(&out[0]) == 0);        /* 0x80 is silence */
	assert(s16le(&out[2]) == -32768);   /* 0x00 is full negative */
	assert(s16le(&out[4]) == 32512);    /* 0xFF is near full positive */
}

/*
 * G.711 anchor values.  Both laws encode silence and full scale at
 * known points; a table that is subtly wrong still "works" and just
 * sounds bad, so pin the ends and the monotonicity.
 */
static void test_conv_ulaw_anchors(void) {
	uint8_t out[4];

	/* One byte in, one 16-bit sample out. */
	assert(conv(AUDIO_ENCODING_ULAW, 8, 1, (const uint8_t[]){ 0xFF }, 1,
		    out) == 2);
	/* 0xFF is mu-law zero; 0x7F is zero with the sign bit set. */
	conv(AUDIO_ENCODING_ULAW, 8, 1,(const uint8_t[]){ 0xFF }, 1, out);
	assert(s16le(out) == 0);
	conv(AUDIO_ENCODING_ULAW, 8, 1,(const uint8_t[]){ 0x7F }, 1, out);
	assert(s16le(out) == 0);
	/* 0x00 / 0x80 are the extremes.  Note the sign: in mu-law the sign
	 * bit is inverted along with everything else, so 0x00 is full
	 * negative and 0x80 full positive. */
	conv(AUDIO_ENCODING_ULAW, 8, 1,(const uint8_t[]){ 0x00 }, 1, out);
	assert(s16le(out) == -32124);
	conv(AUDIO_ENCODING_ULAW, 8, 1,(const uint8_t[]){ 0x80 }, 1, out);
	assert(s16le(out) == 32124);
}

static void test_conv_alaw_anchors(void) {
	uint8_t out[4];

	assert(conv(AUDIO_ENCODING_ALAW, 8, 1, (const uint8_t[]){ 0xD5 }, 1,
		    out) == 2);
	conv(AUDIO_ENCODING_ALAW, 8, 1,(const uint8_t[]){ 0xD5 }, 1, out);
	assert(s16le(out) == 8);
	conv(AUDIO_ENCODING_ALAW, 8, 1,(const uint8_t[]){ 0x55 }, 1, out);
	assert(s16le(out) == -8);
	conv(AUDIO_ENCODING_ALAW, 8, 1,(const uint8_t[]){ 0xAA }, 1, out);
	assert(s16le(out) == 32256);
	conv(AUDIO_ENCODING_ALAW, 8, 1,(const uint8_t[]){ 0x2A }, 1, out);
	assert(s16le(out) == -32256);
}

/* Both laws must be monotonic across each half of their range -- the
 * cheapest check that catches a transposed segment or bias. */
static void test_conv_g711_monotonic(void) {
	int i;
	int16_t prev;
	uint8_t out[4];

	/* mu-law 0x00..0x7F runs from full negative up to zero. */
	conv(AUDIO_ENCODING_ULAW, 8, 1,(const uint8_t[]){ 0x00 }, 1, out);
	prev = s16le(out);
	assert(prev == -32124);
	for (i = 1; i < 128; i++) {
		int16_t v;
		uint8_t b = (uint8_t)i;

		conv(AUDIO_ENCODING_ULAW, 8, 1,&b, 1, out);
		v = s16le(out);
		assert(v > prev);
		prev = v;
	}
	assert(prev == 0);   /* 0x7F is mu-law negative zero */

	/* 0x80..0xFF is the mirror image, running down to zero. */
	conv(AUDIO_ENCODING_ULAW, 8, 1,(const uint8_t[]){ 0x80 }, 1, out);
	prev = s16le(out);
	assert(prev == 32124);
	for (i = 129; i < 256; i++) {
		int16_t v;
		uint8_t b = (uint8_t)i;

		conv(AUDIO_ENCODING_ULAW, 8, 1,&b, 1, out);
		v = s16le(out);
		assert(v < prev);
		prev = v;
	}
	assert(prev == 0);   /* 0xFF is mu-law positive zero */
}

/*
 * The whole point of the exercise: a backend must be programmed for the
 * format it will actually be handed, not the one the application asked
 * for.  8-bit sources are widened, so the hardware has to be told 16.
 */
static void test_hw_prinfo_maps_to_backend_format(void) {
	audio_prinfo_t sw, hw;

	memset(&sw, 0, sizeof(sw));
	sw.sample_rate = 8000;
	sw.channels = 1;

	sw.encoding = AUDIO_ENCODING_ULAW;
	sw.precision = 8;
	audio_hw_prinfo(&sw, &hw);
	assert(hw.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(hw.precision == 16);
	assert(hw.sample_rate == 8000 && hw.channels == 1);

	sw.encoding = AUDIO_ENCODING_PCM8;
	audio_hw_prinfo(&sw, &hw);
	assert(hw.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(hw.precision == 16);

	sw.encoding = AUDIO_ENCODING_SLINEAR_BE;
	sw.precision = 16;
	audio_hw_prinfo(&sw, &hw);
	assert(hw.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(hw.precision == 16);

	/* Already native: untouched. */
	sw.encoding = AUDIO_ENCODING_SLINEAR_LE;
	audio_hw_prinfo(&sw, &hw);
	assert(hw.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(hw.precision == 16);
}

/* ----------------------------------------------------------------- */
/* 24/32-bit, downmixing, resampling                                 */
/* ----------------------------------------------------------------- */

static void test_validate_accepts_linear_widths(void) {
	static const uint32_t encs[] = {
		AUDIO_ENCODING_SLINEAR_LE, AUDIO_ENCODING_SLINEAR_BE,
		AUDIO_ENCODING_ULINEAR_LE, AUDIO_ENCODING_ULINEAR_BE,
		AUDIO_ENCODING_SLINEAR, AUDIO_ENCODING_ULINEAR,
	};
	static const uint32_t precs[] = { 8, 16, 24, 32 };
	audio_info_t info;
	size_t e, p;

	for (e = 0; e < sizeof(encs) / sizeof(encs[0]); e++) {
		for (p = 0; p < sizeof(precs) / sizeof(precs[0]); p++) {
			audio_default_info(&info);
			info.play.encoding = encs[e];
			info.play.precision = precs[p];
			assert(audio_validate_info(&info) == 0);
		}
		audio_default_info(&info);
		info.play.encoding = encs[e];
		info.play.precision = 12;
		assert(audio_validate_info(&info) == -EINVAL);
	}
	/* Fixed-width encodings keep their one width. */
	audio_default_info(&info);
	info.play.encoding = AUDIO_ENCODING_PCM16;
	info.play.precision = 32;
	assert(audio_validate_info(&info) == -EINVAL);
	info.play.encoding = AUDIO_ENCODING_ULAW;
	info.play.precision = 16;
	assert(audio_validate_info(&info) == -EINVAL);
}

static void put32le(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void put16le(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

/* 32-bit rounds to the nearest 16-bit value and saturates at the top. */
static void test_conv_s32_rounds_to_s16(void) {
	uint8_t in[20], out[10];

	put32le(in + 0, 0x7FFFFFFFu);   /* full scale: must not wrap */
	put32le(in + 4, 0x80000000u);   /* full negative */
	put32le(in + 8, 0x00018000u);   /* 1.5 LSB -> 2 */
	put32le(in + 12, 0x00017FFFu);  /* just under -> 1 */
	put32le(in + 16, 0x12345678u);
	assert(conv(AUDIO_ENCODING_SLINEAR_LE, 32, 1, in, 20, out) == 10);
	assert(s16le(out + 0) == 32767);
	assert(s16le(out + 2) == -32768);
	assert(s16le(out + 4) == 2);
	assert(s16le(out + 6) == 1);
	assert(s16le(out + 8) == 0x1234);
}

static void test_conv_other_widths(void) {
	uint8_t out[8];

	/* 32-bit big-endian. */
	assert(conv(AUDIO_ENCODING_SLINEAR_BE, 32, 1,
		    (const uint8_t[]){ 0x12, 0x34, 0x56, 0x78 }, 4, out) == 2);
	assert(s16le(out) == 0x1234);
	/* 24-bit packed, little- and big-endian. */
	assert(conv(AUDIO_ENCODING_SLINEAR_LE, 24, 1,
		    (const uint8_t[]){ 0x56, 0x34, 0x12 }, 3, out) == 2);
	assert(s16le(out) == 0x1234);
	assert(conv(AUDIO_ENCODING_SLINEAR_BE, 24, 1,
		    (const uint8_t[]){ 0xED, 0xCB, 0xA9 }, 3, out) == 2);
	assert(s16le(out) == (int16_t)0xEDCC);   /* rounds up from ...A9 */
	/* Unsigned 16: the midpoint is silence. */
	assert(conv(AUDIO_ENCODING_ULINEAR_LE, 16, 3,
		    (const uint8_t[]){ 0x00, 0x80, 0xFF, 0xFF, 0x00, 0x00 },
		    6, out) == 6);
	assert(s16le(out + 0) == 0);
	assert(s16le(out + 2) == 32767);
	assert(s16le(out + 4) == -32768);
	/* Unsigned 32 big-endian. */
	assert(conv(AUDIO_ENCODING_ULINEAR_BE, 32, 1,
		    (const uint8_t[]){ 0x80, 0x00, 0x00, 0x00 }, 4, out) == 2);
	assert(s16le(out) == 0);
	/* Signed 8. */
	assert(conv(AUDIO_ENCODING_SLINEAR, 8, 2,
		    (const uint8_t[]){ 0x7F, 0x80 }, 2, out) == 4);
	assert(s16le(out + 0) == 0x7F00);
	assert(s16le(out + 2) == -32768);
}

/* Mix one S16 frame of `s` channels to `d` at the same rate. */
static void mix16(const int16_t *in, uint32_t s, int16_t *out, uint32_t d) {
	uint8_t ib[AUDIO_MAX_CHANNELS * 2], ob[AUDIO_MAX_CHANNELS * 2];
	uint32_t c;

	for (c = 0; c < s; c++) {
		put16le(ib + 2 * c, (uint16_t)in[c]);
	}
	assert(conv_to(fmt(AUDIO_ENCODING_SLINEAR_LE, 16, s, 44100),
		       fmt(AUDIO_ENCODING_SLINEAR_LE, 16, d, 44100),
		       ib, 2 * s, ob) == 2 * d);
	for (c = 0; c < d; c++) {
		out[c] = s16le(ob + 2 * c);
	}
}

static void test_mix_mono_and_stereo(void) {
	int16_t o[2];

	mix16((const int16_t[]){ 1234 }, 1, o, 2);
	assert(o[0] == 1234 && o[1] == 1234);
	mix16((const int16_t[]){ 1000, 3000 }, 2, o, 1);
	assert(o[0] == 2000);
}

/*
 * 5.1 in SDL/WAVE order (FL FR FC LFE BL BR) to stereo: fronts stay on
 * their side, the centre goes to both equally, LFE is dropped, and full
 * scale everywhere is full scale out -- the mix is normalised, so a loud
 * multichannel stream cannot clip.
 */
static void test_mix_51_to_stereo(void) {
	int16_t o[2];

	mix16((const int16_t[]){ 10000, 0, 0, 0, 0, 0 }, 6, o, 2);
	assert(o[0] > 0 && o[1] == 0);
	mix16((const int16_t[]){ 0, 10000, 0, 0, 0, 0 }, 6, o, 2);
	assert(o[0] == 0 && o[1] > 0);
	mix16((const int16_t[]){ 0, 0, 10000, 0, 0, 0 }, 6, o, 2);
	assert(o[0] > 0 && o[0] == o[1]);
	mix16((const int16_t[]){ 0, 0, 0, 10000, 0, 0 }, 6, o, 2);
	assert(o[0] == 0 && o[1] == 0);
	mix16((const int16_t[]){ 0, 0, 0, 0, 10000, 0 }, 6, o, 2);
	assert(o[0] > 0 && o[1] == 0);
	mix16((const int16_t[]){ 10000, 10000, 10000, 10000, 10000, 10000 },
	      6, o, 2);
	assert(o[0] == 10000 && o[1] == 10000);
	mix16((const int16_t[]){ -32768, -32768, -32768, -32768, -32768,
				 -32768 }, 6, o, 2);
	assert(o[0] == -32768 && o[1] == -32768);
}

/* 7.1 at 32-bit full scale folds to stereo full scale, not a wrap. */
static void test_mix_71_s32_full_scale(void) {
	uint8_t in[32], out[4];
	int c;

	for (c = 0; c < 8; c++) {
		put32le(in + 4 * c, 0x7FFFFFFFu);
	}
	assert(conv_to(fmt(AUDIO_ENCODING_SLINEAR_LE, 32, 8, 48000),
		       fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 2, 48000),
		       in, sizeof(in), out) == 4);
	assert(s16le(out) == 32767 && s16le(out + 2) == 32767);
}

/* Upsampling 22.05 -> 44.1 kHz doubles the frame count, and a ramp
 * stays a ramp: interpolated points lie between their neighbours. */
static void test_resample_up(void) {
	uint8_t in[200], out[1024];
	size_t n, i;

	for (i = 0; i < 100; i++) {
		put16le(in + 2 * i, (uint16_t)(i * 100));
	}
	n = conv_to(fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 22050),
		    fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 44100),
		    in, sizeof(in), out) / 2;
	assert(n >= 196 && n <= 200);   /* one frame of latency */
	for (i = 1; i < n; i++) {
		int d = s16le(out + 2 * i) - s16le(out + 2 * (i - 1));

		assert(d == 50);
	}
}

/* Downsampling 96 -> 48 kHz halves the frame count and averages: a
 * tone at the new Nyquist rate cancels instead of aliasing into DC. */
static void test_resample_down(void) {
	uint8_t in[400], out[400];
	size_t n, i;

	for (i = 0; i < 200; i++) {
		put16le(in + 2 * i, (uint16_t)((i & 1) ? -8000 : 8000));
	}
	n = conv_to(fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 96000),
		    fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 48000),
		    in, sizeof(in), out) / 2;
	assert(n == 100);
	for (i = 0; i < n; i++) {
		assert(s16le(out + 2 * i) == 0);
	}
}

/* A non-integer ratio keeps the long-run rate right. */
static void test_resample_44k_to_48k(void) {
	static uint8_t in[4410 * 2], out[4800 * 2 + 64];
	size_t n;

	memset(in, 0, sizeof(in));
	n = conv_to(fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 44100),
		    fmt(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 48000),
		    in, sizeof(in), out) / 2;
	assert(n >= 4797 && n <= 4801);
}

/* ----------------------------------------------------------------- */
/* Negotiation and the write path, against a scripted backend        */
/* ----------------------------------------------------------------- */

static struct {
	uint32_t max_chan;       /* reject more than this (0: any) */
	uint32_t only_rate;      /* reject any other rate (0: any) */
	uint32_t report_rate;    /* accept, but report this rate */
	int calls;
	uint8_t written[4096];
	size_t written_len;
} fake;

static int fake_set_params(audio_dev_t *dev, audio_info_t *info) {
	(void)dev;
	fake.calls++;
	assert(info->play.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(info->play.precision == 16);
	if (fake.max_chan && info->play.channels > fake.max_chan) {
		return -EINVAL;
	}
	if (fake.only_rate && info->play.sample_rate != fake.only_rate) {
		return -EINVAL;
	}
	if (fake.report_rate) {
		info->play.sample_rate = fake.report_rate;
	}
	return 0;
}

static int fake_write(audio_dev_t *dev, const void *buf, size_t len) {
	(void)dev;
	assert(fake.written_len + len <= sizeof(fake.written));
	memcpy(fake.written + fake.written_len, buf, len);
	fake.written_len += len;
	return (int)len;
}

static int fake_free_bytes = -1;   /* -1: no get_ospace answer */

static int fake_get_ospace(audio_dev_t *dev, int *fragsize, int *fragstotal,
			   int *fragments, int *bytes) {
	(void)dev;
	if (fake_free_bytes < 0) {
		return -EINVAL;
	}
	*fragsize = 4096;
	*fragstotal = 16;
	*fragments = fake_free_bytes / 4096;
	*bytes = fake_free_bytes;
	return 0;
}

static audio_dev_ops_t fake_ops = {
	.set_params = fake_set_params,
	.write = fake_write,
	.get_ospace = fake_get_ospace,
};

static audio_dev_t fake_dev;
static fs_node_t fake_node;

static void fake_reset(void) {
	memset(&fake, 0, sizeof(fake));
	memset(&fake_dev, 0, sizeof(fake_dev));
	fake_dev.ops = &fake_ops;
	audio_default_info(&fake_dev.current);
	fake_dev.hw_play = fake_dev.current.play;
	memset(&fake_node, 0, sizeof(fake_node));
	fake_node.impl = (uintptr_t)&fake_dev;
}

static int fake_apply(uint32_t enc, uint32_t prec, uint32_t ch,
		      uint32_t rate) {
	audio_info_t info = fake_dev.current;

	info.play.encoding = enc;
	info.play.precision = prec;
	info.play.channels = ch;
	info.play.sample_rate = rate;
	return audio_apply_info(&fake_dev, &info);
}

/* psymp3's request: S32 at 44.1 kHz.  The application keeps its format;
 * the backend is programmed for 16-bit. */
static void test_negotiate_keeps_app_format(void) {
	fake_reset();
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 32, 2, 44100) == 0);
	assert(fake_dev.current.play.precision == 32);
	assert(fake_dev.hw_play.precision == 16);
	assert(fake_dev.hw_play.encoding == AUDIO_ENCODING_SLINEAR_LE);
	assert(fake_dev.hw_play.channels == 2);
	assert(fake_dev.hw_play.sample_rate == 44100);
}

/* A backend refusing 5.1 and anything but 48 kHz still gets a stream. */
static void test_negotiate_falls_back(void) {
	fake_reset();
	fake.max_chan = 2;
	fake.only_rate = 48000;
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 32, 6, 96000) == 0);
	assert(fake_dev.current.play.channels == 6);
	assert(fake_dev.current.play.sample_rate == 96000);
	assert(fake_dev.hw_play.channels == 2);
	assert(fake_dev.hw_play.sample_rate == 48000);
}

/* A backend that sets a rate of its own (AC'97 without VRA) is
 * resampled to rather than played at the wrong speed. */
static void test_negotiate_takes_reported_rate(void) {
	fake_reset();
	fake.report_rate = 48000;
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 16, 2, 22050) == 0);
	assert(fake_dev.current.play.sample_rate == 22050);
	assert(fake_dev.hw_play.sample_rate == 48000);
}

/* Fixed stereo hardware: mono is duplicated, 7.1 folded down, without
 * asking the backend for a channel count it cannot play. */
static void test_negotiate_fixed_channels(void) {
	fake_reset();
	fake_dev.hw_chan_min = 2;
	fake_dev.hw_chan_max = 2;
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 44100) == 0);
	assert(fake_dev.hw_play.channels == 2);
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 16, 8, 44100) == 0);
	assert(fake_dev.hw_play.channels == 2);
}

/* Nothing the backend accepts: the device keeps its old format. */
static void test_negotiate_failure_leaves_device(void) {
	fake_reset();
	fake.only_rate = 12345;
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 32, 2, 44100) == -EINVAL);
	assert(fake_dev.current.play.precision == 16);
}

/* S32 stereo written in pieces that split frames: every byte is taken,
 * and each frame reaches the backend once, whole, as S16. */
static void test_write_carries_partial_frame(void) {
	uint8_t in[24];
	int i;

	fake_reset();
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 32, 2, 44100) == 0);
	for (i = 0; i < 6; i++) {
		put32le(in + 4 * i, (uint32_t)(i + 1) << 16);
	}
	assert(audio_node_write(&fake_node, 0, 10, in) == 10);
	assert(fake.written_len == 4);            /* frame 0 */
	assert(audio_node_write(&fake_node, 0, 3, in + 10) == 3);
	assert(fake.written_len == 4);            /* still mid-frame 1 */
	assert(audio_node_write(&fake_node, 0, 11, in + 13) == 11);
	assert(fake.written_len == 12);           /* frames 1 and 2 */
	for (i = 0; i < 6; i++) {
		assert(s16le(fake.written + 2 * i) == i + 1);
	}
}

/* A large write is converted through the staging buffer in pieces. */
static void test_write_large_upsample(void) {
	static uint8_t in[4000];

	fake_reset();
	fake.report_rate = 48000;
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 8000) == 0);
	memset(in, 0, sizeof(in));
	/* 300 mono frames at 8 kHz -> ~1800 at 48 kHz, 3600 bytes: close
	 * to a full staging buffer, so it takes more than one pass. */
	assert(audio_node_write(&fake_node, 0, 600, in) == 600);
	assert(fake.written_len >= (300 - 1) * 6 * 2 &&
	       fake.written_len <= 300 * 6 * 2);
}

/* S16 at the backend's own format goes straight through, odd sizes
 * included -- the pre-existing behaviour. */
static void test_write_passthrough(void) {
	const uint8_t in[5] = { 1, 2, 3, 4, 5 };

	fake_reset();
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 16, 2, 44100) == 0);
	assert(audio_node_write(&fake_node, 0, 5, in) == 5);
	assert(fake.written_len == 5);
	assert(memcmp(fake.written, in, 5) == 0);
}

/* OSS space reports are converted to the application's byte units. */
static void test_hw_to_app_bytes(void) {
	fake_reset();
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 32, 2, 44100) == 0);
	assert(audio_hw_to_app_bytes(&fake_dev, 1000) == 2000);
	fake.report_rate = 48000;
	fake_dev.hw_chan_min = 2;
	fake_dev.hw_chan_max = 2;
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 16, 1, 24000) == 0);
	/* hw: 2 ch x 2 B x 48000; app: 1 x 2 x 24000 -> a quarter. */
	assert(audio_hw_to_app_bytes(&fake_dev, 1000) == 250);
}

/*
 * play.seek reports what is still queued, in samples of the application's
 * format.  SDL3's Sun backend waits on it before each write; always 0, it
 * wrote into a full FIFO holding its device lock and starved
 * SDL_ResumeAudioStreamDevice for seconds.
 */
static void test_getinfo_reports_queued_seek(void) {
	audio_info_t got;

	fake_reset();
	assert(fake_apply(AUDIO_ENCODING_SLINEAR_LE, 32, 2, 44100) == 0);
	/* 64 KiB FIFO, 16 KiB free: 48 KiB of S16 queued = 24576 hw samples,
	 * which is 24576 S32 samples too (same frame count, same channels). */
	fake_free_bytes = 16384;
	assert(audio_ioctl_dispatch(&fake_dev, AUDIO_GETINFO, &got) == 0);
	assert(got.play.seek == 24576);
	/* Empty FIFO: nothing queued. */
	fake_free_bytes = 65536;
	assert(audio_ioctl_dispatch(&fake_dev, AUDIO_GETINFO, &got) == 0);
	assert(got.play.seek == 0);
	/* A backend that cannot say reports 0, as before. */
	fake_free_bytes = -1;
	assert(audio_ioctl_dispatch(&fake_dev, AUDIO_GETINFO, &got) == 0);
	assert(got.play.seek == 0);
}

int main(void) {
	test_initinfo_marks_all_fields_unset();
	test_default_info_populates_sensible_defaults();
	test_merge_only_overwrites_non_sentinel();
	test_validate_rejects_unknown_encoding();
	test_validate_rejects_bad_channel_count();
	test_validate_rejects_out_of_range_sample_rate();
	test_validate_clamps_gain_and_balance();
	test_validate_normalizes_lowat_against_hiwat();
	test_validate_rejects_zero_mode();
	test_register_publishes_two_devfs_nodes();
	test_ioctl_getinfo_returns_current();
	test_ioctl_setinfo_round_trips_through_merge();
	test_ioctl_setinfo_rejects_invalid();
	test_ioctl_getdev_returns_null_backend_id();
	test_ioctl_getprops_advertises_caps();
	test_ioctl_setfd_requires_fullduplex_and_persists();
	test_ioctl_unknown_returns_enotty();
	test_ioctl_drain_and_flush_succeed();
	test_ioctl_null_args_rejected();
	test_conv_native_is_passthrough();
	test_conv_slinear_be_swaps();
	test_conv_slinear_be_drops_odd_tail();
	test_conv_unsigned8_widens();
	test_conv_ulaw_anchors();
	test_conv_alaw_anchors();
	test_conv_g711_monotonic();
	test_hw_prinfo_maps_to_backend_format();
	test_validate_accepts_linear_widths();
	test_conv_s32_rounds_to_s16();
	test_conv_other_widths();
	test_mix_mono_and_stereo();
	test_mix_51_to_stereo();
	test_mix_71_s32_full_scale();
	test_resample_up();
	test_resample_down();
	test_resample_44k_to_48k();
	test_negotiate_keeps_app_format();
	test_negotiate_falls_back();
	test_negotiate_takes_reported_rate();
	test_negotiate_fixed_channels();
	test_negotiate_failure_leaves_device();
	test_write_carries_partial_frame();
	test_write_large_upsample();
	test_write_passthrough();
	test_hw_to_app_bytes();
	test_getinfo_reports_queued_seek();
	puts("host_test_audio: PASS");
	return 0;
}
