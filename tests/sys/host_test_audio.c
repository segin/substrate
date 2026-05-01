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

/*
 * audio.c includes "audio.h" with a relative path; we point the include
 * search at the directory below.
 */
#include "../../sys/drivers/audio/audio.h"
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
	puts("host_test_audio: PASS");
	return 0;
}
