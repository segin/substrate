/*
 * torture_audio_share.c -- a second opener must not disturb the stream.
 *
 *   1000 Hz 2 s  played by the parent through /dev/audio (48 kHz stereo).
 *                Half a second in, a child opens /dev/dsp (whose open used
 *                to apply 8 kHz U8 mono defaults to the running stream) and
 *                tries to set 8 kHz itself, which must fail with EBUSY.
 *   1500 Hz 0.5 s played by a second child that opens the device the
 *                moment the parent starts closing -- while its close is
 *                still draining.  The drain's teardown used to reset the
 *                ring under the newcomer.
 *
 * The capture (qemu -audiodev wav) must show 2 s of 1000 Hz at one pitch
 * followed by 0.5 s of 1500 Hz.  Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <sys/wait.h>
#include <unistd.h>

#define RATE 48000

static int failures;

static void fail(const char *what)
{
	printf("FAIL: %s: %s\n", what, strerror(errno));
	failures++;
}

static int open_audio(void)
{
	audio_info_t info;
	int fd = open("/dev/audio", O_WRONLY);

	if (fd < 0) {
		fail("open /dev/audio");
		return -1;
	}
	AUDIO_INITINFO(&info);
	info.play.sample_rate = RATE;
	info.play.channels = 2;
	info.play.precision = 16;
	info.play.encoding = AUDIO_ENCODING_SLINEAR_LE;
	if (ioctl(fd, AUDIO_SETINFO, &info) < 0) {
		fail("AUDIO_SETINFO");
	}
	return fd;
}

static void play(int fd, double hz, double secs)
{
	static int16_t buf[4096 * 2];
	size_t frames = (size_t)(secs * RATE), done = 0;
	double ph = 0;

	while (done < frames) {
		size_t n = frames - done < 4096 ? frames - done : 4096;

		for (size_t i = 0; i < n; i++) {
			int16_t s = (int16_t)lround(sin(ph) * 16000.0);

			buf[2 * i] = buf[2 * i + 1] = s;
			ph += 2.0 * M_PI * hz / RATE;
		}
		if (write(fd, buf, n * 4) < 0) {
			fail("write");
			return;
		}
		done += n;
	}
}

int main(void)
{
	int fd, st;
	pid_t intruder, follower;

	/*
	 * Children first, so neither inherits the parent's descriptor.  All
	 * 2 s of the parent's audio fit in the buffers, so its writes return
	 * at once and its close() then drains for about 2 s: the follower's
	 * open at 0.8 s lands inside that drain.
	 */
	intruder = fork();
	if (intruder == 0) {
		int dsp, v = 8000;

		usleep(400000);
		dsp = open("/dev/dsp", O_WRONLY);
		if (dsp < 0) {
			_exit(2);
		}
		if (ioctl(dsp, SNDCTL_DSP_SPEED, &v) == 0 || errno != EBUSY) {
			_exit(3);
		}
		close(dsp);
		_exit(0);
	}
	follower = fork();
	if (follower == 0) {
		int f2;

		usleep(800000);
		f2 = open_audio();
		if (f2 < 0) {
			_exit(2);
		}
		play(f2, 1500.0, 0.5);
		(void)ioctl(f2, AUDIO_DRAIN, 0);
		close(f2);
		_exit(failures ? 1 : 0);
	}

	fd = open_audio();
	if (fd < 0) {
		printf("Result: FAIL\n");
		return 1;
	}
	play(fd, 1000.0, 2.0);
	close(fd);

	if (waitpid(intruder, &st, 0) < 0 || !WIFEXITED(st) ||
	    WEXITSTATUS(st) != 0) {
		printf("FAIL: second opener (status %d): format change not "
		       "refused\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
		failures++;
	}
	if (waitpid(follower, &st, 0) < 0 || !WIFEXITED(st) ||
	    WEXITSTATUS(st) != 0) {
		printf("FAIL: follower exited with %d\n",
		       WIFEXITED(st) ? WEXITSTATUS(st) : -1);
		failures++;
	}
	printf("Result: %s (%d failure(s))\n", failures ? "FAIL" : "PASS",
	       failures);
	return failures ? 1 : 0;
}
