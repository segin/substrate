/*
 * compress.c - Compress files using Lempel-Ziv-Welch (LZW) encoding
 *
 * Produces .Z files compatible with historic Unix compress(1) / ncompress.
 * Also acts as zcat when invoked as "zcat" (equivalent to uncompress -c).
 *
 * Copyright (c) 2024-2026 The Substrate Project
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 */
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <libgen.h>

#define MAGIC_1 0x1f
#define MAGIC_2 0x9d
#define BLOCK_MASK 0x80

#define INIT_BITS   9
#define BITS        16
#define HSIZE       69001
#define FIRST       257
#define CLEAR       256
#define MAXCODE(n)  ((1L << (n)) - 1)

/* ── Output buffer (matches ncompress layout) ── */
#define OBUFSIZ 2048
static unsigned char outbuf[OBUFSIZ + 2048];

/* ── ncompress-compatible output macro ──
 * b = buffer, o = bit offset (in bits), c = code, n = n_bits
 * Writes code c at bit offset o in buffer b, LSB-first across 3 bytes.
 */
#define OUTPUT(b,o,c,n) do { \
	unsigned char *p = &(b)[(o)>>3]; \
	unsigned long _i = ((unsigned long)(c)) << ((o) & 0x7); \
	p[0] |= (unsigned char)(_i); \
	p[1] |= (unsigned char)(_i >> 8); \
	p[2] |= (unsigned char)(_i >> 16); \
	(o) += (n); \
} while(0)

/* ── Global options ── */
static int force_flag = 0;
static int cat_flag = 0;
static int verbose_flag = 0;
static int quiet_flag = 0;
static int maxbits = BITS;
static int block_mode = 1;

/* ── Hash table ── */
typedef unsigned long count_int;
static count_int htab[HSIZE];
static unsigned short codetab[HSIZE];

#define HTABOF(i) htab[i]
#define CODETABOF(i) codetab[i]

static void cl_hash(long hsize) {
	memset(htab, 0xff, (size_t)hsize * sizeof(htab[0]));
}

static int compress_stream(FILE *in, FILE *out) {
	unsigned char header[3];
	int n_bits = INIT_BITS;
	long maxmaxcode = 1L << maxbits;
	long extcode;
	long free_ent;
	long bytes_in = 0, bytes_out = 0;
	int stcode = 1;

	/* Bit offset in outbuf */
	int offset = 0;

	/* Write header */
	header[0] = MAGIC_1;
	header[1] = MAGIC_2;
	header[2] = (unsigned char)(maxbits | (block_mode ? BLOCK_MASK : 0));
	if(fwrite(header, 1, 3, out) != 3) return(-1);
	bytes_out = 3;

	/* Init */
	extcode = MAXCODE(n_bits);
	if(n_bits < maxbits) extcode++;

	cl_hash(HSIZE);
	free_ent = block_mode ? FIRST : 256;

	int c = fgetc(in);
	if(c == EOF) return(0);
	bytes_in = 1;
	long ent = c;

	long hshift = 0;
	for(long fcode = (long)HSIZE; fcode < 65536L; fcode *= 2L)
		hshift++;
	hshift = 8 - hshift;

	/* Output first code (it's always a raw byte, < 256) */
	/* Actually ncompress accumulates into the first string too */
	/* Let's match ncompress exactly: loop and output in main loop */

	int ch;
	while((ch = fgetc(in)) != EOF) {
		bytes_in++;

		long fcode = ((long)ch << maxbits) + ent;
		long i = ((long)ch << hshift) ^ ent;

		if(htab[i] == (count_int)fcode) {
			ent = codetab[i];
			continue;
		}

		if(htab[i] != (count_int)-1) {
			long disp = HSIZE - i;
			if(i == 0) disp = 1;
			do {
				i += disp;
				if(i >= HSIZE) i -= HSIZE;
				if(htab[i] == (count_int)fcode) {
					ent = codetab[i];
					goto next;
				}
			} while(htab[i] != (count_int)-1);
		}

		/* Output current ent */
		OUTPUT(outbuf, offset, ent, n_bits);

		/* Flush output buffer if getting full */
		if(offset >= (OBUFSIZ << 3)) {
			int ob = offset >> 3;
			if(fwrite(outbuf, 1, ob, out) != (size_t)ob) return(-1);
			bytes_out += ob;
			/* Shift remaining partial byte */
			int rem = offset & 7;
			if(rem) {
				outbuf[0] = outbuf[ob];
			}
			memset(outbuf + (rem ? 1 : 0), 0, sizeof(outbuf) - (rem ? 1 : 0));
			offset = rem;
		}

		ent = ch;

		if(stcode) {
			stcode = 0;
		}

		if(free_ent < maxmaxcode) {
			codetab[i] = (unsigned short)free_ent++;
			htab[i] = (count_int)fcode;

			if(free_ent > extcode) {
				if(n_bits < maxbits) {
					n_bits++;
					extcode = MAXCODE(n_bits);
					if(n_bits < maxbits)
						extcode++;
				}
			}
		} else if(block_mode) {
			/* Table full: output CLEAR code and reset */
			OUTPUT(outbuf, offset, CLEAR, n_bits);

			/* Flush output buffer */
			if(offset >= (OBUFSIZ << 3)) {
				int ob = offset >> 3;
				if(fwrite(outbuf, 1, ob, out) != (size_t)ob) return(-1);
				bytes_out += ob;
				int rem = offset & 7;
				if(rem) outbuf[0] = outbuf[ob];
				memset(outbuf + (rem ? 1 : 0), 0, sizeof(outbuf) - (rem ? 1 : 0));
				offset = rem;
			}

			cl_hash(HSIZE);
			stcode = 1;
			free_ent = FIRST;
			n_bits = INIT_BITS;
			extcode = MAXCODE(n_bits);
			if(n_bits < maxbits) extcode++;
		}
next:
		;
	}

	/* Output final ent */
	OUTPUT(outbuf, offset, ent, n_bits);

	/* Flush remaining */
	if(offset > 0) {
		int ob = (offset + 7) >> 3;
		if(fwrite(outbuf, 1, ob, out) != (size_t)ob) return(-1);
		bytes_out += ob;
	}

	if(ferror(in)) return(-1);
	if(ferror(out)) return(-1);

	if(verbose_flag && bytes_in > 0) {
		double ratio = 100.0 * (1.0 - (double)bytes_out / (double)bytes_in);
		fprintf(stderr, "Compression: %.1f%%\n", ratio);
	}

	return(0);
}

static void usage(const char *prog) {
	fprintf(stderr, "usage: %s [-cfqvb bits] [file ...]\n", prog);
	exit(1);
}

static int process_file(const char *name) {
	FILE *in = NULL, *out = NULL;
	char out_name[1024];
	char in_name[1024];
	struct stat in_sb;
	int res;
	int from_stdin = (name == NULL || strcmp(name, "-") == 0);

	if(from_stdin) {
		in = stdin;
		out = stdout;
	} else {
		strncpy(in_name, name, sizeof(in_name) - 1);
		in_name[sizeof(in_name) - 1] = '\0';

		if(stat(in_name, &in_sb) < 0) {
			perror(in_name);
			return(1);
		}

		size_t len = strlen(in_name);
		if(len > 2 && strcmp(in_name + len - 2, ".Z") == 0) {
			fprintf(stderr, "compress: %s: already has .Z suffix\n", in_name);
			return(1);
		}

		in = fopen(in_name, "rb");
		if(!in) {
			perror(in_name);
			return(1);
		}

		if(cat_flag) {
			out = stdout;
		} else {
			if(len + 3 > sizeof(out_name)) {
				fprintf(stderr, "compress: %s: name too long\n", in_name);
				fclose(in);
				return(1);
			}
			memcpy(out_name, in_name, len);
			out_name[len] = '.';
			out_name[len + 1] = 'Z';
			out_name[len + 2] = '\0';

			int fd;
			int flags = O_WRONLY | O_CREAT;
			if(!force_flag)
				flags |= O_EXCL;
			else
				flags |= O_TRUNC;

			fd = open(out_name, flags, 0666);
			if(fd < 0) {
				if(errno == EEXIST)
					fprintf(stderr, "compress: %s already exists\n", out_name);
				else
					perror(out_name);
				fclose(in);
				return(1);
			}
			out = fdopen(fd, "wb");
			if(!out) {
				perror(out_name);
				close(fd);
				fclose(in);
				return(1);
			}
		}
	}

	/* Zero the output buffer */
	memset(outbuf, 0, sizeof(outbuf));

	res = compress_stream(in, out);

	if(!from_stdin) fclose(in);

	if(out != stdout) {
		if(fclose(out) != 0) {
			perror(out_name);
			res = -1;
		}

		if(res == 0) {
			struct stat out_sb;
			if(!force_flag && stat(out_name, &out_sb) == 0 &&
			   out_sb.st_size >= in_sb.st_size) {
				if(!quiet_flag)
					fprintf(stderr, "compress: %s: file would grow; not compressed\n", in_name);
				unlink(out_name);
				return(2);
			}

			chmod(out_name, in_sb.st_mode & 07777);

			if(verbose_flag)
				fprintf(stderr, "%s: compressed\n", in_name);

			if(unlink(in_name) < 0) {
				perror(in_name);
				res = -1;
			}
		} else {
			unlink(out_name);
		}
	}

	return(res != 0 ? 1 : 0);
}

int main(int argc, char *argv[]) {
	int c;
	int exit_code = 0;

	const char *progname = basename(argv[0]);
	if(strcmp(progname, "zcat") == 0) {
		char **new_argv = malloc(sizeof(char*) * (argc + 2));
		new_argv[0] = "uncompress";
		new_argv[1] = "-c";
		for(int i = 1; i < argc; i++)
			new_argv[i + 1] = argv[i];
		new_argv[argc + 1] = NULL;
		execvp("uncompress", new_argv);
		perror("zcat: uncompress");
		return(1);
	}

	while((c = getopt(argc, argv, "b:cfqv")) != -1) {
		switch(c) {
		case 'b':
			maxbits = atoi(optarg);
			if(maxbits < 9 || maxbits > 16) {
				fprintf(stderr, "compress: -b value must be between 9 and 16\n");
				return(1);
			}
			break;
		case 'c':
			cat_flag = 1;
			break;
		case 'f':
			force_flag = 1;
			break;
		case 'q':
			quiet_flag = 1;
			break;
		case 'v':
			verbose_flag = 1;
			break;
		default:
			usage(progname);
		}
	}

	if(optind == argc) {
		if(process_file(NULL) != 0)
			exit_code = 1;
	} else {
		for(int i = optind; i < argc; i++) {
			if(process_file(argv[i]) != 0)
				exit_code = 1;
		}
	}

	return(exit_code);
}
