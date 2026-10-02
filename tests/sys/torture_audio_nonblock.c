/*
 * torture_audio_nonblock.c -- non-blocking playback and poll(2).
 *
 * An event-loop player opens the device O_NONBLOCK (or asks for it with
 * SNDCTL_DSP_NONBLOCK), writes until the buffer is full, and polls for
 * POLLOUT before writing again.  Every write used to block until it was
 * fully accepted, whatever the descriptor's flags, and the audio nodes had
 * no poll op at all.
 *
 * Checks, for both ways of going non-blocking:
 *   - filling the buffer ends in EAGAIN promptly instead of blocking;
 *   - poll(POLLOUT) reports space again once playback frees some;
 *   - the next write succeeds.
 * Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/time.h>
#include <unistd.h>

static int failures;
static int16_t buf[16384];

static double now(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static void check(int ok, const char *what)
{
	if (!ok) {
		printf("FAIL: %s (errno %s)\n", what, strerror(errno));
		failures++;
	}
}

static void exercise(int fd, const char *how)
{
	struct pollfd pfd;
	double t;
	ssize_t w;
	size_t total = 0;
	int v, rc;

	v = AFMT_S16_LE;
	check(ioctl(fd, SNDCTL_DSP_SETFMT, &v) == 0, "SETFMT");
	v = 2;
	check(ioctl(fd, SNDCTL_DSP_CHANNELS, &v) == 0, "CHANNELS");
	v = 48000;
	check(ioctl(fd, SNDCTL_DSP_SPEED, &v) == 0, "SPEED");

	/* Quiet tone: the content does not matter here. */
	for (size_t i = 0; i < sizeof(buf) / sizeof(buf[0]); i++) {
		buf[i] = (int16_t)((i & 64) ? 2000 : -2000);
	}

	t = now();
	for (;;) {
		w = write(fd, buf, sizeof(buf));
		if (w < 0) {
			break;
		}
		total += (size_t)w;
		if (now() - t > 3.0) {
			break;
		}
	}
	printf("%s: accepted %zu bytes before %s in %.2f s\n", how, total,
	       w < 0 ? strerror(errno) : "the time limit", now() - t);
	check(w < 0 && errno == EAGAIN, "full buffer ends in EAGAIN");
	check(now() - t < 2.0, "filling the buffer did not block");

	pfd.fd = fd;
	pfd.events = POLLOUT;
	t = now();
	rc = poll(&pfd, 1, 3000);
	printf("%s: poll returned %d revents 0x%x after %.2f s\n", how, rc,
	       pfd.revents, now() - t);
	check(rc == 1 && (pfd.revents & POLLOUT), "poll reports POLLOUT");

	w = write(fd, buf, 4096);
	check(w > 0, "write after POLLOUT");
	(void)ioctl(fd, SNDCTL_DSP_RESET, 0);
}

int main(void)
{
	int fd;

	fd = open("/dev/dsp", O_WRONLY | O_NONBLOCK);
	check(fd >= 0, "open O_NONBLOCK");
	if (fd >= 0) {
		exercise(fd, "O_NONBLOCK");
		close(fd);
	}

	fd = open("/dev/dsp", O_WRONLY);
	check(fd >= 0, "open blocking");
	if (fd >= 0) {
		check(ioctl(fd, SNDCTL_DSP_NONBLOCK, 0) == 0,
		      "SNDCTL_DSP_NONBLOCK");
		exercise(fd, "SNDCTL_DSP_NONBLOCK");
		close(fd);
	}

	printf("Result: %s (%d failure(s))\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures ? 1 : 0;
}
