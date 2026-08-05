/*
 * tonegen - write a fixed 1 kHz square wave to an audio device.
 *
 * Deliberately trivial and dependency-free: it exists so a boot test can
 * prove an audio driver actually produces samples, by pointing QEMU's `wav`
 * audio backend at a file and checking what lands there.  A tone is used
 * rather than a music file because the expected output is exactly
 * predictable -- full-scale alternating levels at a known period -- so the
 * captured WAV can be checked by arithmetic instead of by ear.
 *
 * Usage: tonegen [seconds] > /dev/audio0
 * Format is fixed at 48 kHz, 16-bit signed, stereo, matching the driver
 * default so no ioctl negotiation is involved.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RATE      48000
#define TONE_HZ   1000
#define AMPLITUDE 12000

int main(int argc, char **argv)
{
	int seconds = 2;
	long total, i;
	int half = RATE / (TONE_HZ * 2);   /* samples per half period */
	static short buf[1024];            /* interleaved L,R pairs */
	size_t fill = 0;

	if (argc > 1) {
		seconds = atoi(argv[1]);
		if (seconds < 1) seconds = 1;
		if (seconds > 30) seconds = 30;
	}
	total = (long)RATE * seconds;

	for (i = 0; i < total; i++) {
		short v = ((i / half) & 1) ? (short)AMPLITUDE : (short)-AMPLITUDE;

		buf[fill++] = v;   /* left  */
		buf[fill++] = v;   /* right */
		if (fill == sizeof(buf) / sizeof(buf[0])) {
			if (write(1, buf, sizeof(buf)) < 0) {
				perror("write");
				return 1;
			}
			fill = 0;
		}
	}
	if (fill > 0 && write(1, buf, fill * sizeof(buf[0])) < 0) {
		perror("write");
		return 1;
	}
	return 0;
}
