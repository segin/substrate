/*
 * audio_fifo.h - single-producer / single-consumer PCM byte FIFO.
 *
 * Decouples the audio write() producer (a user thread) from the DMA-ring
 * consumer (the controller IRQ handler).  write() appends PCM here and blocks
 * only when this deep software buffer fills; the IRQ-driven feeder drains it
 * into the hardware BDL ring autonomously, so scheduling jitter under CPU load
 * no longer starves the DMA engine into an underrun.
 *
 * Concurrency model: exactly one producer (audio_fifo_write) and one consumer
 * (audio_fifo_read) at any instant.  The producer is the write() syscall; the
 * consumer is the controller IRQ -- except during initial priming, when the
 * driver runs the feeder itself with the device IRQ masked (so the IRQ cannot
 * also consume at the same time).  head/tail are free-running byte counters;
 * the capacity need not be a power of two.  Release/acquire ordering on the
 * counters makes the data copy visible across the producer/IRQ boundary.
 *
 * Header-only static-inline so it links into both the kernel drivers and the
 * host unit test without a separate translation unit.
 */
#ifndef _DRIVERS_AUDIO_FIFO_H
#define _DRIVERS_AUDIO_FIFO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct audio_fifo {
	uint8_t        *buf;
	size_t          cap;
	volatile size_t head;   /* total bytes ever written (producer owns) */
	volatile size_t tail;   /* total bytes ever read    (consumer owns) */
} audio_fifo_t;

static inline void audio_fifo_init(audio_fifo_t *f, uint8_t *buf, size_t cap)
{
	f->buf = buf;
	f->cap = cap;
	f->head = 0;
	f->tail = 0;
}

static inline void audio_fifo_reset(audio_fifo_t *f)
{
	__atomic_store_n(&f->tail, 0, __ATOMIC_RELEASE);
	__atomic_store_n(&f->head, 0, __ATOMIC_RELEASE);
}

static inline size_t audio_fifo_used(const audio_fifo_t *f)
{
	size_t h = __atomic_load_n(&f->head, __ATOMIC_ACQUIRE);
	size_t t = __atomic_load_n(&f->tail, __ATOMIC_ACQUIRE);
	return h - t;
}

static inline size_t audio_fifo_free(const audio_fifo_t *f)
{
	return f->cap - audio_fifo_used(f);
}

/* Producer side. Returns the number of bytes accepted (<= n). */
static inline size_t audio_fifo_write(audio_fifo_t *f, const void *src, size_t n)
{
	size_t space = audio_fifo_free(f);
	size_t head, off, first;

	if (n > space) {
		n = space;
	}
	if (n == 0) {
		return 0;
	}
	head = f->head;                 /* producer owns head */
	off = head % f->cap;
	first = f->cap - off;
	if (first > n) {
		first = n;
	}
	memcpy(f->buf + off, src, first);
	if (n > first) {
		memcpy(f->buf, (const uint8_t *)src + first, n - first);
	}
	__atomic_store_n(&f->head, head + n, __ATOMIC_RELEASE);
	return n;
}

/* Consumer side. Returns the number of bytes produced into dst (<= n). */
static inline size_t audio_fifo_read(audio_fifo_t *f, void *dst, size_t n)
{
	size_t avail = audio_fifo_used(f);
	size_t tail, off, first;

	if (n > avail) {
		n = avail;
	}
	if (n == 0) {
		return 0;
	}
	tail = f->tail;                 /* consumer owns tail */
	off = tail % f->cap;
	first = f->cap - off;
	if (first > n) {
		first = n;
	}
	memcpy(dst, f->buf + off, first);
	if (n > first) {
		memcpy((uint8_t *)dst + first, f->buf, n - first);
	}
	__atomic_store_n(&f->tail, tail + n, __ATOMIC_RELEASE);
	return n;
}

#endif /* _DRIVERS_AUDIO_FIFO_H */
