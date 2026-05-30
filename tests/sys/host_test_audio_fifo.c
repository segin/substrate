/*
 * host_test_audio_fifo.c - unit tests for the SPSC PCM FIFO
 * (sys/drivers/audio/audio_fifo.h).
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "drivers/audio/audio_fifo.h"

static void test_empty(void)
{
	uint8_t backing[16];
	audio_fifo_t f;
	audio_fifo_init(&f, backing, sizeof(backing));
	assert(audio_fifo_used(&f) == 0);
	assert(audio_fifo_free(&f) == sizeof(backing));

	uint8_t out[4];
	assert(audio_fifo_read(&f, out, sizeof(out)) == 0);
}

static void test_basic_write_read(void)
{
	uint8_t backing[16];
	audio_fifo_t f;
	audio_fifo_init(&f, backing, sizeof(backing));

	assert(audio_fifo_write(&f, "hello", 5) == 5);
	assert(audio_fifo_used(&f) == 5);
	assert(audio_fifo_free(&f) == 11);

	uint8_t out[8] = {0};
	assert(audio_fifo_read(&f, out, 5) == 5);
	assert(memcmp(out, "hello", 5) == 0);
	assert(audio_fifo_used(&f) == 0);
}

static void test_partial_when_full(void)
{
	uint8_t backing[8];
	audio_fifo_t f;
	audio_fifo_init(&f, backing, sizeof(backing));

	/* Only 8 bytes of capacity; writing 12 accepts 8. */
	assert(audio_fifo_write(&f, "0123456789AB", 12) == 8);
	assert(audio_fifo_free(&f) == 0);
	/* Further writes accept nothing until we read. */
	assert(audio_fifo_write(&f, "X", 1) == 0);
}

static void test_wraparound(void)
{
	uint8_t backing[8];
	audio_fifo_t f;
	audio_fifo_init(&f, backing, sizeof(backing));
	uint8_t out[8];

	/* Fill 6, drain 6, then write 6 more -> straddles the buffer end. */
	assert(audio_fifo_write(&f, "ABCDEF", 6) == 6);
	assert(audio_fifo_read(&f, out, 6) == 6);
	assert(memcmp(out, "ABCDEF", 6) == 0);

	assert(audio_fifo_write(&f, "uvwxyz", 6) == 6);   /* wraps */
	memset(out, 0, sizeof(out));
	assert(audio_fifo_read(&f, out, 6) == 6);
	assert(memcmp(out, "uvwxyz", 6) == 0);
	assert(audio_fifo_used(&f) == 0);
}

static void test_streaming_many_wraps(void)
{
	uint8_t backing[7];   /* deliberately not a power of two */
	audio_fifo_t f;
	audio_fifo_init(&f, backing, sizeof(backing));

	/* Stream a long sequence in odd-sized chunks; every byte must come
	 * out exactly once, in order. */
	uint8_t next_w = 0, next_r = 0;
	for (int round = 0; round < 1000; round++) {
		uint8_t chunk[5];
		for (int i = 0; i < 5; i++)
			chunk[i] = (uint8_t)(next_w + i);
		size_t wrote = audio_fifo_write(&f, chunk, 5);
		next_w = (uint8_t)(next_w + wrote);

		uint8_t got[3];
		size_t rd = audio_fifo_read(&f, got, 3);
		for (size_t i = 0; i < rd; i++) {
			assert(got[i] == next_r);
			next_r = (uint8_t)(next_r + 1);
		}
	}
	/* Drain the remainder and confirm continuity to the end. */
	for (;;) {
		uint8_t got[4];
		size_t rd = audio_fifo_read(&f, got, 4);
		if (rd == 0)
			break;
		for (size_t i = 0; i < rd; i++) {
			assert(got[i] == next_r);
			next_r = (uint8_t)(next_r + 1);
		}
	}
	assert(next_r == next_w);
}

static void test_reset(void)
{
	uint8_t backing[8];
	audio_fifo_t f;
	audio_fifo_init(&f, backing, sizeof(backing));
	audio_fifo_write(&f, "abcd", 4);
	audio_fifo_reset(&f);
	assert(audio_fifo_used(&f) == 0);
	assert(audio_fifo_free(&f) == sizeof(backing));
}

int main(void)
{
	test_empty();
	test_basic_write_read();
	test_partial_when_full();
	test_wraparound();
	test_streaming_many_wraps();
	test_reset();
	printf("host_test_audio_fifo: all tests passed\n");
	return 0;
}
