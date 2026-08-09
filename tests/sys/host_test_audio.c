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

/* Signed 16-bit LE is what backends want, so it must pass through
 * untouched and unexpanded -- this is the path every ordinary player
 * takes and it must stay a straight memcpy. */
static void test_conv_native_is_passthrough(void) {
	const uint8_t in[8] = { 0x01, 0x80, 0xFF, 0x7F, 0x00, 0x00, 0x34, 0x12 };
	uint8_t out[16];
	size_t n;

	assert(audio_conv_ratio(AUDIO_ENCODING_SLINEAR_LE, 16) == 1);
	n = audio_convert(AUDIO_ENCODING_SLINEAR_LE, 16, in, sizeof(in), out);
	assert(n == sizeof(in));
	assert(memcmp(out, in, sizeof(in)) == 0);
	/* The native aliases behave identically. */
	assert(audio_conv_ratio(AUDIO_ENCODING_SLINEAR, 16) == 1);
	assert(audio_conv_ratio(AUDIO_ENCODING_PCM16, 16) == 1);
}

/* Big-endian signed: byteswap, same size. */
static void test_conv_slinear_be_swaps(void) {
	const uint8_t in[4] = { 0x12, 0x34, 0xFF, 0x80 };
	uint8_t out[8];
	size_t n;

	assert(audio_conv_ratio(AUDIO_ENCODING_SLINEAR_BE, 16) == 1);
	n = audio_convert(AUDIO_ENCODING_SLINEAR_BE, 16, in, sizeof(in), out);
	assert(n == 4);
	assert(s16le(&out[0]) == (int16_t)0x1234);
	assert(s16le(&out[2]) == (int16_t)0xFF80);
}

/* An odd trailing byte cannot be swapped alone; it must be dropped
 * rather than paired with whatever arrives next. */
static void test_conv_slinear_be_drops_odd_tail(void) {
	const uint8_t in[3] = { 0x12, 0x34, 0x56 };
	uint8_t out[8];

	assert(audio_convert(AUDIO_ENCODING_SLINEAR_BE, 16, in, 3, out) == 2);
}

/* 8-bit unsigned recentres on zero and widens to 16-bit. */
static void test_conv_unsigned8_widens(void) {
	const uint8_t in[3] = { 0x80, 0x00, 0xFF };
	uint8_t out[8];
	size_t n;

	assert(audio_conv_ratio(AUDIO_ENCODING_PCM8, 8) == 2);
	n = audio_convert(AUDIO_ENCODING_PCM8, 8, in, sizeof(in), out);
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

	assert(audio_conv_ratio(AUDIO_ENCODING_ULAW, 8) == 2);
	/* 0xFF is mu-law zero; 0x7F is zero with the sign bit set. */
	audio_convert(AUDIO_ENCODING_ULAW, 8, (const uint8_t[]){ 0xFF }, 1, out);
	assert(s16le(out) == 0);
	audio_convert(AUDIO_ENCODING_ULAW, 8, (const uint8_t[]){ 0x7F }, 1, out);
	assert(s16le(out) == 0);
	/* 0x00 / 0x80 are the extremes.  Note the sign: in mu-law the sign
	 * bit is inverted along with everything else, so 0x00 is full
	 * negative and 0x80 full positive. */
	audio_convert(AUDIO_ENCODING_ULAW, 8, (const uint8_t[]){ 0x00 }, 1, out);
	assert(s16le(out) == -32124);
	audio_convert(AUDIO_ENCODING_ULAW, 8, (const uint8_t[]){ 0x80 }, 1, out);
	assert(s16le(out) == 32124);
}

static void test_conv_alaw_anchors(void) {
	uint8_t out[4];

	assert(audio_conv_ratio(AUDIO_ENCODING_ALAW, 8) == 2);
	audio_convert(AUDIO_ENCODING_ALAW, 8, (const uint8_t[]){ 0xD5 }, 1, out);
	assert(s16le(out) == 8);
	audio_convert(AUDIO_ENCODING_ALAW, 8, (const uint8_t[]){ 0x55 }, 1, out);
	assert(s16le(out) == -8);
	audio_convert(AUDIO_ENCODING_ALAW, 8, (const uint8_t[]){ 0xAA }, 1, out);
	assert(s16le(out) == 32256);
	audio_convert(AUDIO_ENCODING_ALAW, 8, (const uint8_t[]){ 0x2A }, 1, out);
	assert(s16le(out) == -32256);
}

/* Both laws must be monotonic across each half of their range -- the
 * cheapest check that catches a transposed segment or bias. */
static void test_conv_g711_monotonic(void) {
	int i;
	int16_t prev;
	uint8_t out[4];

	/* mu-law 0x00..0x7F runs from full negative up to zero. */
	audio_convert(AUDIO_ENCODING_ULAW, 8, (const uint8_t[]){ 0x00 }, 1, out);
	prev = s16le(out);
	assert(prev == -32124);
	for (i = 1; i < 128; i++) {
		int16_t v;
		uint8_t b = (uint8_t)i;

		audio_convert(AUDIO_ENCODING_ULAW, 8, &b, 1, out);
		v = s16le(out);
		assert(v > prev);
		prev = v;
	}
	assert(prev == 0);   /* 0x7F is mu-law negative zero */

	/* 0x80..0xFF is the mirror image, running down to zero. */
	audio_convert(AUDIO_ENCODING_ULAW, 8, (const uint8_t[]){ 0x80 }, 1, out);
	prev = s16le(out);
	assert(prev == 32124);
	for (i = 129; i < 256; i++) {
		int16_t v;
		uint8_t b = (uint8_t)i;

		audio_convert(AUDIO_ENCODING_ULAW, 8, &b, 1, out);
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
	puts("host_test_audio: PASS");
	return 0;
}
