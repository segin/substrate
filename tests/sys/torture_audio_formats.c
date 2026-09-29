/*
 * torture_audio_formats.c -- play a 1 kHz tone through every format
 * class the audio framework converts.
 *
 * The framework hands backends signed 16-bit PCM at a channel count and
 * rate they accept; applications may write 8- to 32-bit linear PCM, up to
 * 8 channels, at 4-192 kHz.  AUDIO_SETINFO with precision 32 used to fail
 * with EINVAL, which left SDL3 (and PsyMP3 on it) silent.
 *
 * Each segment is one second of sine at half scale, drained before the
 * next; the first is 500 Hz and each one after it 500 Hz higher, so a
 * capture identifies every segment by its pitch.  The test checks the
 * ioctls and writes; whether the audio is right -- each segment one
 * second long at its own pitch, which a wrong rate or a mangled sample
 * format would not be -- is for the host to judge from a capture of the
 * sound card (qemu -audiodev wav).
 *
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>

#define TONE_STEP_HZ 500.0
#define AMPLITUDE    0.5

static int failures;
static double tone_hz = TONE_STEP_HZ;

static void fail(const char *what)
{
	printf("FAIL: %s: %s\n", what, strerror(errno));
	failures++;
}

/* Store sample `v` (full scale +-1.0) as `prec`-bit signed, `big` order. */
static uint8_t *put_sample(uint8_t *p, double v, int prec, int big)
{
	int nb = prec / 8;
	int64_t full = ((int64_t)1 << (prec - 1)) - 1;
	uint32_t u = (uint32_t)(int32_t)llround(v * (double)full);
	int i;

	for (i = 0; i < nb; i++) {
		int shift = big ? 8 * (nb - 1 - i) : 8 * i;

		p[i] = (uint8_t)(u >> shift);
	}
	return p + nb;
}

/*
 * One second of tone.  `tone_mask` says which channels carry it; the
 * rest are silent, so a downmix that drops or misplaces a channel shows
 * up as silence in the capture.
 */
static size_t make_tone(uint8_t *buf, int rate, int chans, int prec,
			int big, unsigned tone_mask)
{
	uint8_t *p = buf;
	int i, c;

	for (i = 0; i < rate; i++) {
		double v = AMPLITUDE * sin(2.0 * M_PI * tone_hz * i / rate);

		for (c = 0; c < chans; c++) {
			p = put_sample(p, (tone_mask & (1u << c)) ? v : 0.0,
				       prec, big);
		}
	}
	tone_hz += TONE_STEP_HZ;
	return (size_t)(p - buf);
}

static int write_all(int fd, const uint8_t *buf, size_t len)
{
	size_t off = 0;

	/* Odd-sized writes on purpose: they split frames. */
	while (off < len) {
		size_t n = len - off > 4099 ? 4099 : len - off;
		ssize_t w = write(fd, buf + off, n);

		if (w < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		off += (size_t)w;
	}
	return 0;
}

static void sun_segment(const char *name, int enc, int prec, int chans,
			int rate, unsigned tone_mask)
{
	audio_info_t info, got;
	uint8_t *buf;
	size_t len;
	int fd;

	fd = open("/dev/audio", O_WRONLY);
	if (fd < 0) {
		fail("open /dev/audio");
		return;
	}
	AUDIO_INITINFO(&info);
	info.mode = AUMODE_PLAY;
	info.play.encoding = (uint32_t)enc;
	info.play.precision = (uint32_t)prec;
	info.play.channels = (uint32_t)chans;
	info.play.sample_rate = (uint32_t)rate;
	if (ioctl(fd, AUDIO_SETINFO, &info) < 0) {
		fail(name);
		close(fd);
		return;
	}
	if (ioctl(fd, AUDIO_GETINFO, &got) < 0) {
		fail("AUDIO_GETINFO");
	} else if ((int)got.play.precision != prec ||
		   (int)got.play.channels != chans ||
		   (int)got.play.sample_rate != rate ||
		   (int)got.play.encoding != enc) {
		printf("FAIL: %s: GETINFO reports %u-bit %u ch %u Hz enc %u\n",
		       name, got.play.precision, got.play.channels,
		       got.play.sample_rate, got.play.encoding);
		failures++;
	}

	buf = malloc((size_t)rate * (size_t)chans * 4);
	if (buf == NULL) {
		fail("malloc");
		close(fd);
		return;
	}
	len = make_tone(buf, rate, chans, prec,
			enc == AUDIO_ENCODING_SLINEAR_BE, tone_mask);
	if (write_all(fd, buf, len) < 0) {
		fail(name);
	} else {
		printf("SEGMENT: %s\n", name);
	}
	(void)ioctl(fd, AUDIO_DRAIN, 0);
	free(buf);
	close(fd);
}

static void oss_segment(const char *name, int afmt, int prec, int chans,
			int rate)
{
	uint8_t *buf;
	size_t len;
	int fd, v;

	fd = open("/dev/dsp", O_WRONLY);
	if (fd < 0) {
		fail("open /dev/dsp");
		return;
	}
	v = afmt;
	if (ioctl(fd, SNDCTL_DSP_SETFMT, &v) < 0 || v != afmt) {
		printf("FAIL: %s: SETFMT %#x -> %#x\n", name, afmt, v);
		failures++;
	}
	v = chans;
	if (ioctl(fd, SNDCTL_DSP_CHANNELS, &v) < 0 || v != chans) {
		printf("FAIL: %s: CHANNELS %d -> %d\n", name, chans, v);
		failures++;
	}
	v = rate;
	if (ioctl(fd, SNDCTL_DSP_SPEED, &v) < 0 || v != rate) {
		printf("FAIL: %s: SPEED %d -> %d\n", name, rate, v);
		failures++;
	}
	v = 0;
	if (ioctl(fd, SNDCTL_DSP_GETFMTS, &v) < 0 || (v & afmt) == 0) {
		printf("FAIL: %s: GETFMTS %#x lacks %#x\n", name, v, afmt);
		failures++;
	}

	buf = malloc((size_t)rate * (size_t)chans * 4);
	if (buf == NULL) {
		fail("malloc");
		close(fd);
		return;
	}
	len = make_tone(buf, rate, chans, prec, 0, (1u << chans) - 1);
	if (write_all(fd, buf, len) < 0) {
		fail(name);
	} else {
		printf("SEGMENT: %s\n", name);
	}
	(void)ioctl(fd, SNDCTL_DSP_SYNC, 0);
	free(buf);
	close(fd);
}

int main(void)
{
	/* Reference: already the backend's format. */
	sun_segment("s16le stereo 44100", AUDIO_ENCODING_SLINEAR_LE, 16, 2,
		    44100, 0x3);
	/* What SDL3's NetBSD-style backend asks for. */
	sun_segment("s32le stereo 44100", AUDIO_ENCODING_SLINEAR_LE, 32, 2,
		    44100, 0x3);
	sun_segment("s32be stereo 48000", AUDIO_ENCODING_SLINEAR_BE, 32, 2,
		    48000, 0x3);
	/* Packed 24-bit mono, upmixed and upsampled. */
	sun_segment("s24le mono 22050", AUDIO_ENCODING_SLINEAR_LE, 24, 1,
		    22050, 0x1);
	/* 5.1 with the tone on the centre only: must reach both sides. */
	sun_segment("s32le 5.1 centre 96000", AUDIO_ENCODING_SLINEAR_LE, 32, 6,
		    96000, 1u << 2);
	/* 7.1, tone everywhere, downsampled from 192 kHz. */
	sun_segment("s32le 7.1 192000", AUDIO_ENCODING_SLINEAR_LE, 32, 8,
		    192000, 0xFF);
	oss_segment("oss s32le stereo 48000", AFMT_S32_LE, 32, 2, 48000);
	oss_segment("oss s24packed mono 32000", AFMT_S24_PACKED, 24, 1, 32000);

	printf("Result: %s (%d failures)\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures != 0;
}
