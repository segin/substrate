/*
 * null_audio.c - "/dev/null" of audio backends.
 *
 * Always succeeds, drops all played samples on the floor, and returns
 * silence on capture.  Lets userspace audio code link, open
 * /dev/audio0, and exercise its full ioctl path before any real audio
 * hardware exists.
 */

#include <drivers/audio/audio.h>

#include <sys/audioio.h>
#include <stdio.h>
#include <string.h>

static audio_dev_t null_audio_dev;

static int null_audio_open(audio_dev_t *dev, int mode)
{
	(void)dev;
	(void)mode;
	return 0;
}

static int null_audio_close(audio_dev_t *dev)
{
	(void)dev;
	return 0;
}

static int null_audio_write(audio_dev_t *dev, const void *buf, size_t len)
{
	(void)dev;
	(void)buf;
	if (len > 0x7FFFFFFFu) {
		len = 0x7FFFFFFFu;
	}
	return (int)len;
}

static int null_audio_read(audio_dev_t *dev, void *buf, size_t len)
{
	(void)dev;
	if (buf != NULL && len > 0) {
		memset(buf, 0, len);
	}
	if (len > 0x7FFFFFFFu) {
		len = 0x7FFFFFFFu;
	}
	return (int)len;
}

static int null_audio_set_params(audio_dev_t *dev, audio_info_t *info)
{
	(void)dev;
	(void)info;
	return 0;
}

static int null_audio_drain(audio_dev_t *dev)
{
	(void)dev;
	return 0;
}

static int null_audio_flush(audio_dev_t *dev)
{
	(void)dev;
	return 0;
}

static void null_audio_get_devinfo(audio_dev_t *dev, audio_device_t *out)
{
	(void)dev;
	memset(out, 0, sizeof(*out));
	snprintf(out->name, sizeof(out->name), "null");
	snprintf(out->version, sizeof(out->version), "1.0");
	snprintf(out->config, sizeof(out->config), "null");
}

static int null_audio_get_props(audio_dev_t *dev)
{
	(void)dev;
	return AUDIO_PROP_PLAYBACK | AUDIO_PROP_CAPTURE |
	       AUDIO_PROP_FULLDUPLEX | AUDIO_PROP_INDEPENDENT;
}

static audio_dev_ops_t null_audio_ops = {
	.open        = null_audio_open,
	.close       = null_audio_close,
	.write       = null_audio_write,
	.read        = null_audio_read,
	.set_params  = null_audio_set_params,
	.drain       = null_audio_drain,
	.flush       = null_audio_flush,
	.get_devinfo = null_audio_get_devinfo,
	.get_props   = null_audio_get_props,
};

void null_audio_init(void)
{
	if (audio_have_device()) {
		return;
	}
	memset(&null_audio_dev, 0, sizeof(null_audio_dev));
	snprintf(null_audio_dev.name, sizeof(null_audio_dev.name),
	         "null audio");
	null_audio_dev.ops = &null_audio_ops;
	(void)audio_register_device(&null_audio_dev);
}
