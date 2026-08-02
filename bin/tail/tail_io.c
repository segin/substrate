/*
 * tail_io.c - I/O helpers and algorithms for tail(1)
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tail.h"
#include <sys/types.h>

/* -------------------------------------------------------------------------
 * I/O helpers
 * ------------------------------------------------------------------------- */
int write_all(const unsigned char *buf, size_t n)
{
	size_t off = 0;
	while(off < n) {
		ssize_t w = write(STDOUT_FILENO, buf + off, n - off);
		if(w < 0) { if(errno == EINTR) continue; return(-1); }
		off += (size_t)w;
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * From-start helpers: skip first (count-1) delimiters then copy rest.
 * ------------------------------------------------------------------------- */
static int skip_and_copy_lines(int fd, int64_t start_line, unsigned char delim)
{
	/* start_line is 1-based; we need to skip (start_line - 1) delimiters */
	int64_t skip = start_line - 1;
	unsigned char buf[TAIL_BUFSZ];
	bool skipping = (skip > 0);

	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; return(-1); }
		if(n == 0) break;
		if(!skipping) {
			if(write_all(buf, (size_t)n) < 0) return(-1);
			continue;
		}
		/* still skipping */
		for(ssize_t k = 0; k < n; k++) {
			if(buf[k] == (char)delim) {
				skip--;
				if(skip == 0) {
					/* output rest of buf from k+1 */
					skipping = false;
					if(k + 1 < n)
						if(write_all(buf + k + 1, (size_t)(n - k - 1)) < 0)
							return(-1);
					break;
				}
			}
		}
	}
	return(0);
}

static int skip_and_copy_bytes(int fd, int64_t start_byte)
{
	/* start_byte is 1-based */
	int64_t skip = start_byte - 1;
	unsigned char buf[TAIL_BUFSZ];

	/* Try seek first */
	if(skip > 0) {
		off_t pos = lseek(fd, (off_t)skip, SEEK_CUR);
		if(pos == (off_t)-1 && errno != ESPIPE) return(-1);
		if(pos != (off_t)-1) skip = 0; /* seeked successfully */
	}

	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; return(-1); }
		if(n == 0) break;
		if(skip > 0) {
			ssize_t drop = (skip >= n) ? n : (ssize_t)skip;
			skip -= drop;
			if(drop < n) {
				if(write_all(buf + drop, (size_t)(n - drop)) < 0) return(-1);
			}
		} else {
			if(write_all(buf, (size_t)n) < 0) return(-1);
		}
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * End-relative, seekable file: seek to (size - count) bytes.
 * ------------------------------------------------------------------------- */
static int tail_seekable_bytes(int fd, int64_t count)
{
	off_t size = lseek(fd, 0, SEEK_END);
	if(size < 0) return(-1);
	off_t start = (count >= size) ? 0 : (off_t)(size - count);
	if(lseek(fd, start, SEEK_SET) < 0) return(-1);

	unsigned char buf[TAIL_BUFSZ];
	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; return(-1); }
		if(n == 0) break;
		if(write_all(buf, (size_t)n) < 0) return(-1);
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * End-relative, seekable file: scan backwards for 'count' delimiters.
 * Returns the file offset of the start of the desired tail,
 * then copies from there to end.
 * ------------------------------------------------------------------------- */
static int tail_seekable_lines(int fd, int64_t count, unsigned char delim)
{
	if(count <= 0) return(0);
	off_t size = lseek(fd, 0, SEEK_END);
	if(size < 0) return(-1);
	if(size == 0) return(0);

	/* Read backwards in blocks counting delimiters */
	unsigned char buf[TAIL_BUFSZ];
	int64_t found = 0;
	off_t pos = size;
	off_t start_off = 0;       /* byte offset where our tail begins */
	bool done = false;

	while(pos > 0 && !done) {
		off_t blk_start = pos - (off_t)sizeof(buf);
		if(blk_start < 0) blk_start = 0;
		size_t blk_size = (size_t)(pos - blk_start);

		if(lseek(fd, blk_start, SEEK_SET) < 0) return(-1);
		if(read(fd, buf, blk_size) != (ssize_t)blk_size) return(-1);

		/* scan backwards within block */
		for(ssize_t k = (ssize_t)blk_size - 1; k >= 0; k--) {
			if(buf[k] == (char)delim) {
				/* Don't count a delimiter if it's the very last byte of the file */
				if((off_t)k + blk_start == size - 1)
					continue;

				found++;
				if(found == count) {
					start_off = blk_start + (off_t)k + 1;
					done = true;
					break;
				}
			}
		}
		pos = blk_start;
	}
	/* found < count: output entire file */

	if(lseek(fd, start_off, SEEK_SET) < 0) return(-1);
	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; return(-1); }
		if(n == 0) break;
		if(write_all(buf, (size_t)n) < 0) return(-1);
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * Non-seekable (pipe) byte tail: ring buffer of last 'count' bytes.
 * ------------------------------------------------------------------------- */
static int tail_pipe_bytes(int fd, int64_t count)
{
	if(count <= 0) return(0);

	/*
	 * If the requested tail is larger than any buffer we could address,
	 * the "last count bytes" is the entire stream.  Copy it straight
	 * through rather than truncating count into a bogus size_t — on a
	 * 32-bit target `tail -c 4G` would otherwise yield malloc(0), a
	 * `% cap` divide-by-zero (SIGFPE), and an out-of-bounds ring write
	 * (TAIL-01).
	 */
	if((uint64_t)count > (uint64_t)SIZE_MAX) {
		unsigned char buf[TAIL_BUFSZ];
		for(;;) {
			ssize_t n = read(fd, buf, sizeof(buf));
			if(n < 0) { if(errno == EINTR) continue; return(-1); }
			if(n == 0) break;
			if(write_all(buf, (size_t)n) < 0) return(-1);
		}
		return(0);
	}

	size_t cap = (size_t)count;
	unsigned char *ring;
	if(cap == 0) return(0);		/* defensive: keeps the `% cap` below safe */
	ring = malloc(cap);
	if(!ring) { errno = ENOMEM; return(-1); }

	size_t head = 0, fill = 0;
	bool wrapped = false;
	unsigned char buf[TAIL_BUFSZ];

	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; free(ring); return(-1); }
		if(n == 0) break;

		for(ssize_t i = 0; i < n; i++) {
			ring[head] = buf[i];
			head = (head + 1) % cap;
			if(fill < cap) fill++;
			else wrapped = true;
		}
	}

	int rc = 0;
	if(wrapped || fill == cap) {
		/* output from 'head' (oldest) around ring */
		size_t first_part = cap - head;
		if(write_all(ring + head, first_part) < 0 ||
		   write_all(ring, head) < 0) rc = -1;
	} else {
		if(write_all(ring, fill) < 0) rc = -1;
	}
	free(ring);
	return(rc);
}

/* -------------------------------------------------------------------------
 * Non-seekable (pipe) line tail: buffer and track last 'count' delimiters.
 * ------------------------------------------------------------------------- */
static int tail_pipe_lines(int fd, int64_t count, unsigned char delim)
{
	if(count <= 0) return(0);

	/* Bounded ring of the last `count` lines: keep only N line copies
	 * rather than buffering the entire (possibly infinite) pipe, which
	 * would OOM on a large or unbounded producer (TAIL-03).  A
	 * pathologically huge count fails the pointer-array allocation
	 * gracefully instead of crashing. */
	if((uint64_t)count > SIZE_MAX / sizeof(char *)) {
		errno = ENOMEM;
		return(-1);
	}
	size_t  slots = (size_t)count;
	char  **lines = calloc(slots, sizeof(*lines));
	size_t *lens  = calloc(slots, sizeof(*lens));
	if(!lines || !lens) { free(lines); free(lens); errno = ENOMEM; return(-1); }

	size_t head = 0;        /* next slot to overwrite */
	size_t nfilled = 0;     /* slots currently holding a line */
	char  *cur = NULL;
	size_t cur_len = 0, cur_cap = 0;
	int    rc = 0;

	unsigned char buf[TAIL_BUFSZ];
	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; rc = -1; goto done; }
		if(n == 0) break;
		for(ssize_t i = 0; i < n; i++) {
			if(cur_len + 1 > cur_cap) {
				size_t nc = cur_cap ? cur_cap * 2 : 128;
				char *t = realloc(cur, nc);
				if(!t) { rc = -1; goto done; }
				cur = t; cur_cap = nc;
			}
			cur[cur_len++] = (char)buf[i];
			if(buf[i] == delim) {
				free(lines[head]);
				lines[head] = cur; lens[head] = cur_len;
				head = (head + 1) % slots;
				if(nfilled < slots) nfilled++;
				cur = NULL; cur_len = cur_cap = 0;
			}
		}
	}
	/* A trailing line with no final delimiter is still a line. */
	if(cur_len > 0) {
		free(lines[head]);
		lines[head] = cur; lens[head] = cur_len;
		head = (head + 1) % slots;
		if(nfilled < slots) nfilled++;
		cur = NULL;
	}

	/* Emit the retained lines oldest-first. */
	size_t start = (nfilled < slots) ? 0 : head;
	for(size_t k = 0; k < nfilled; k++) {
		size_t idx = (start + k) % slots;
		if(write_all((unsigned char *)lines[idx], lens[idx]) < 0) {
			rc = -1;
			break;
		}
	}

done:
	free(cur);
	for(size_t k = 0; k < slots; k++)
		free(lines[k]);
	free(lines);
	free(lens);
	return(rc);
}

/* -------------------------------------------------------------------------
 * Reverse mode: buffer input (optionally limited by count), emit reversed.
 * In -r mode -n/-c gives how many items to display (default: all = INT64_MAX).
 * ------------------------------------------------------------------------- */
static int tail_reverse(int fd, int64_t count, bool bytes_mode, unsigned char delim)
{
	/* Buffer entire relevant input */
	size_t alloc = 65536, used = 0;
	unsigned char *data = malloc(alloc);
	if(!data) return(-1);

	for(;;) {
		unsigned char buf[TAIL_BUFSZ];
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) { if(errno == EINTR) continue; free(data); return(-1); }
		if(n == 0) break;
		if(used + (size_t)n > alloc) {
			size_t na = alloc * 2;
			if(na < used + (size_t)n) na = used + (size_t)n;
			unsigned char *tmp = realloc(data, na);
			if(!tmp) { free(data); return(-1); }
			data = tmp; alloc = na;
		}
		memcpy(data + used, buf, (size_t)n);
		used += (size_t)n;
	}

	int rc = 0;
	if(bytes_mode) {
		/* Reverse bytes */
		int64_t to_show = (count == INT64_MAX || count >= (int64_t)used)
		    ? (int64_t)used : count;
		size_t start = used - (size_t)to_show;
		/* Emit bytes in reverse order */
		for(size_t k = used; k > start; k--) {
			if(write_all(data + k - 1, 1) < 0) { rc = -1; break; }
		}
	} else {
		/* Split into delimited lines, collect, reverse, emit */
		/* Build line array */
		size_t max_lines = 1024, nlines = 0;
		struct { size_t start, len; } *lines = malloc(max_lines * sizeof(*lines));
		if(!lines) { free(data); return(-1); }

		size_t line_start = 0;
		for(size_t k = 0; k <= used; k++) {
			if(k == used || data[k] == (char)delim) {
				size_t line_len = (k < used) ? k - line_start + 1 : k - line_start;
				if(line_len > 0 || k < used) {
					if(nlines >= max_lines) {
						max_lines *= 2;
						void *tmp = realloc(lines, max_lines * sizeof(*lines));
						if(!tmp) { free(lines); free(data); return(-1); }
						lines = tmp;
					}
					lines[nlines].start = line_start;
					lines[nlines].len   = (k < used) ? k - line_start + 1 : k - line_start;
					nlines++;
				}
				line_start = k + 1;
			}
		}

		int64_t to_show = (count == INT64_MAX || count >= (int64_t)nlines)
		    ? (int64_t)nlines : count;
		for(int64_t idx = (int64_t)nlines - 1; idx >= (int64_t)nlines - to_show; idx--) {
			if(idx < 0) break;
			size_t s = lines[idx].start, l = lines[idx].len;
			if(l > 0) if(write_all(data + s, l) < 0) { rc = -1; break; }
			/* Add delimiter after each reversed line if it had one */
			if(l > 0 && data[s + l - 1] == (char)delim) {
				/* already included delimiter in len */
			} else if(l == 0 && idx < (int64_t)nlines - 1) {
				/* trailing partial line gets no extra delimiter */
			}
		}
		free(lines);
	}
	free(data);
	return(rc);
}

/* -------------------------------------------------------------------------
 * Dispatch to the right algorithm for one input fd.
 * 'regular' = lseek returns non-(-1) on the fd.
 * ------------------------------------------------------------------------- */
int process_fd(int fd, const struct tail_opts *o)
{
	unsigned char delim = o->zero_term ? '\0' : '\n';

	if(o->reverse) {
		int64_t count = o->count_explicit ? o->count : INT64_MAX;
		return(tail_reverse(fd, count, o->mode == MODE_BYTES, delim));
	}

	if(o->from_start) {
		if(o->mode == MODE_BYTES)
			return(skip_and_copy_bytes(fd, o->count));
		else
			return(skip_and_copy_lines(fd, o->count, delim));
	}

	/* End-relative */
	bool seekable = (lseek(fd, 0, SEEK_CUR) != (off_t)-1);
	if(o->mode == MODE_BYTES) {
		if(seekable) return(tail_seekable_bytes(fd, o->count));
		return(tail_pipe_bytes(fd, o->count));
	} else {
		if(seekable) return(tail_seekable_lines(fd, o->count, delim));
		return(tail_pipe_lines(fd, o->count, delim));
	}
}
