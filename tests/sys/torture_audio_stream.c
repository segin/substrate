/*
 * torture_audio_stream.c -- exercise the playback engine's edges.
 *
 * Plays tone segments through /dev/audio at 48 kHz stereo S16LE, each at
 * its own pitch, so a capture of the sound card (qemu -audiodev wav) shows
 * what came out and when:
 *
 *   500 Hz  1 s   in 4608-byte writes (not a multiple of the DMA chunk)
 *   ---     1.5 s pause with the descriptor open: a sub-chunk residue sits
 *                 in the FIFO while the ring drains to silence
 *   1000 Hz 1 s   in 4608-byte writes -- silence here means the feeder
 *                 wedged during the pause
 *   1500 Hz 50 ms one 10 KiB write, then 1 s idle with the fd open: the
 *                 effect must play promptly, not wait for more data
 *   2000 Hz 2 s   written, then flushed after 0.3 s
 *   2500 Hz 0.5 s then drained: nothing of the flushed 2000 Hz may follow
 *
 * The test itself checks the ioctls, that drain and close return within a
 * few seconds, and that a drain reports success.  Prints a "Result:" line.
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
#include <sys/time.h>
#include <unistd.h>

#define RATE       48000
#define CHANNELS   2
#define FRAME      (CHANNELS * 2)
#define AMPLITUDE  0.5
#define WRITE_SIZE 4608

static int failures;
static double phase;

static void fail(const char *what)
{
	printf("FAIL: %s: %s\n", what, strerror(errno));
	failures++;
}

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

/* Fill `frames` frames of sine at `hz` into buf, continuing the phase. */
static void tone(int16_t *buf, int frames, double hz)
{
	int i, c;

	for (i = 0; i < frames; i++) {
		int16_t s = (int16_t)lround(sin(phase) * AMPLITUDE * 32767.0);

		for (c = 0; c < CHANNELS; c++) {
			buf[i * CHANNELS + c] = s;
		}
		phase += 2.0 * M_PI * hz / RATE;
		if (phase > 2.0 * M_PI) {
			phase -= 2.0 * M_PI;
		}
	}
}

static void play(int fd, double hz, double secs, size_t chunk)
{
	size_t total = (size_t)(secs * RATE) * FRAME;
	static int16_t buf[RATE * CHANNELS];
	size_t done = 0;

	while (done < total) {
		size_t n = total - done < chunk ? total - done : chunk;
		ssize_t w;

		tone(buf, (int)(n / FRAME), hz);
		w = write(fd, buf, n);
		if (w < 0) {
			fail("write");
			return;
		}
		done += (size_t)w;
	}
}

int main(void)
{
	audio_info_t info;
	double t;
	int fd;

	fd = open("/dev/audio", O_WRONLY);
	if (fd < 0) {
		fail("open /dev/audio");
		printf("Result: FAIL\n");
		return 1;
	}
	AUDIO_INITINFO(&info);
	info.play.sample_rate = RATE;
	info.play.channels = CHANNELS;
	info.play.precision = 16;
	info.play.encoding = AUDIO_ENCODING_SLINEAR_LE;
	if (ioctl(fd, AUDIO_SETINFO, &info) < 0) {
		fail("AUDIO_SETINFO");
	}

	play(fd, 500.0, 1.0, WRITE_SIZE);
	usleep(1500000);
	play(fd, 1000.0, 1.0, WRITE_SIZE);

	t = now();
	if (ioctl(fd, AUDIO_DRAIN, 0) < 0) {
		fail("AUDIO_DRAIN after resume");
	}
	if (now() - t > 5.0) {
		printf("FAIL: drain after resume took %.1f s\n", now() - t);
		failures++;
	}

	play(fd, 1500.0, 10240.0 / (RATE * FRAME), 10240);
	usleep(1000000);

	play(fd, 2000.0, 2.0, 65536);
	usleep(300000);
	if (ioctl(fd, AUDIO_FLUSH, 0) < 0) {
		fail("AUDIO_FLUSH");
	}
	play(fd, 2500.0, 0.5, WRITE_SIZE);

	t = now();
	if (ioctl(fd, AUDIO_DRAIN, 0) < 0) {
		fail("AUDIO_DRAIN");
	}
	if (now() - t > 5.0) {
		printf("FAIL: final drain took %.1f s\n", now() - t);
		failures++;
	}
	t = now();
	close(fd);
	if (now() - t > 5.0) {
		printf("FAIL: close took %.1f s\n", now() - t);
		failures++;
	}

	printf("Result: %s (%d failure(s))\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures ? 1 : 0;
}
