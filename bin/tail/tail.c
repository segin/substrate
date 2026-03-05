/*
 * tail - output the last part of files
 *
 * POSIX.1-2024 + BSD + GNU coreutils compatibility.
 *
 * Precedence: POSIX required → BSD preferred → GNU long options.
 *
 * Suffix parsing:
 *   BSD: K/M/G/T/P/E (×1024), case-insensitive, trailing B ignored.
 *   GNU: b=512, K=1024, KB=1000, M=1048576, MB=1000000, KiB=K, MiB=M, …
 *   Where ambiguous (MB), BSD interpretation is used (×1024^N).
 *   GNU 'b' alone (as suffix to -c/-n) treated as 512.
 *
 * Synopsis: tail [-f|-F|-r] [-c N|-n N|-b N] [-qvz] [file...]
 *           tail [GNU-long-opts] [file...]
 *
 * GNU: --bytes, --lines, --follow[=descriptor|name], --retry, --pid,
 *      --sleep-interval, --max-unchanged-stats, --debug, --zero-terminated,
 *      --quiet/--silent, --verbose, --help, --version
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define TAIL_VERSION	"tail (Substrate) 1.0"
#define TAIL_BUFSZ	65536u
#define BLOCK_SIZE	512	/* -b unit */

/* -------------------------------------------------------------------------
 * Numeric suffix parser
 * Supports BSD suffix set + GNU 'b'=512 + IEC/decimal SI aliases.
 * All binary; 'b'=512, K=1024, KB=1024 (BSD precedence), M=1048576, etc.
 * Returns 0 on success, -1 on error (errno = ERANGE or EINVAL).
 * sign_char: if '+' the result may be positive (start-from-begin);
 *            stored verbatim in *out as a positive value with *from_start set.
 * ------------------------------------------------------------------------- */
static int expand_number(const char *s, int64_t *out)
{
	char *end;
	long long val;
	int64_t mult = 1;

	if(!s || !*s) { errno = EINVAL; return(-1); }
	errno = 0;
	val = strtoll(s, &end, 10);
	if(end == s) { errno = EINVAL; return(-1); }
	if(errno == ERANGE) return(-1);

	if(*end != '\0') {
		char c = *end;
		if(c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
		switch(c) {
		case 'B':
			/* lone 'B': BSD ignores; also handles GNU 'b' (lowercase) as 512 */
			if((unsigned char)*end == 'b' || (unsigned char)*end == 'B') {
				/* if lowercase 'b' treat as GNU block=512 */
				if(*end == 'b') mult = 512;
				/* else uppercase 'B' = ignored (mult stays 1) */
			}
			end++;
			goto done;
		case 'K': mult = (int64_t)1024LL;                    break;
		case 'M': mult = (int64_t)1024LL * 1024;             break;
		case 'G': mult = (int64_t)1024LL * 1024 * 1024;      break;
		case 'T': mult = (int64_t)1024LL * 1024 * 1024 * 1024; break;
		case 'P': mult = (int64_t)1024LL * 1024 * 1024 * 1024 * 1024; break;
		case 'E': mult = (int64_t)1024LL * 1024 * 1024 * 1024 * 1024 * 1024; break;
		default:  errno = EINVAL; return(-1);
		}
		end++;
		/* accept optional 'i' (IEC) or 'B'/'b' (decimal-SI alias → same binary val) */
		if(*end == 'i' || *end == 'I') end++;
		if(*end == 'b' || *end == 'B') end++;
	}
done:
	if(*end != '\0') { errno = EINVAL; return(-1); }
	if(val > 0 && mult > INT64_MAX / val) { errno = ERANGE; return(-1); }
	if(val < 0 && mult > INT64_MAX / (-(val + 1) + 1)) { errno = ERANGE; return(-1); }
	*out = val * mult;
	return(0);
}

/* -------------------------------------------------------------------------
 * Options
 * ------------------------------------------------------------------------- */
typedef enum { MODE_LINES, MODE_BYTES, MODE_BLOCKS } tail_mode_t;
typedef enum { FOLLOW_NONE, FOLLOW_DESCRIPTOR, FOLLOW_NAME } follow_mode_t;

#define MAX_PIDS 64

struct tail_opts {
	const char *progname;
	tail_mode_t  mode;
	int64_t      count;		/* positive = from end, negative = from start (POSIX '+') */
	bool         from_start;	/* true if leading '+' (start-relative) */
	bool         follow;
	follow_mode_t follow_mode;
	bool         retry;
	bool         reverse;
	bool         quiet;
	bool         verbose;
	bool         zero_term;
	bool         debug_mode;
	double       sleep_interval;
	int          max_unchanged;
	pid_t        pids[MAX_PIDS];
	int          npids;
	bool         show_help;
	bool         show_version;
};

/* -------------------------------------------------------------------------
 * Usage / version
 * ------------------------------------------------------------------------- */
static void usage(FILE *f, const char *p)
{
	fprintf(f,
	    "Usage: %s [-f|-F|-r] [-c [-|+]N | -n [-|+]N | -b N] [-qvz] [file...]\n", p);
}

static void print_help(const struct tail_opts *o)
{
	usage(stdout, o->progname);
	fputs(
	    "\nOutput the last part of files.\n"
	    "\nOptions:\n"
	    "  -c [-|+]N, --bytes=[-|+]N   output last N bytes; +N starts at byte N\n"
	    "  -n [-|+]N, --lines=[-|+]N   output last N lines (default: 10); +N starts at line N\n"
	    "  -b N, --blocks=N            like -c N*512\n"
	    "  -f, --follow[=descriptor]   do not stop at EOF; output appended data\n"
	    "  -F                          like --follow=name --retry\n"
	    "  --follow=name               follow by filename (reopen on rotation)\n"
	    "  --retry                     retry open if file missing (with -f/--follow=name)\n"
	    "  -r                          output items in reverse order\n"
	    "  -q, --quiet, --silent       suppress file headers\n"
	    "  -v, --verbose               always print file headers\n"
	    "  -z, --zero-terminated       use NUL as item delimiter\n"
	    "  -s N, --sleep-interval=N    polling interval for follow (default: 1.0 s)\n"
	    "  --max-unchanged-stats=N     reopen check interval for follow-name (default: 5)\n"
	    "  --pid=PID                   exit when PID dies (repeatable; requires -f)\n"
	    "  --debug                     emit follow diagnostics to stderr\n"
	    "  --                          end option processing\n"
	    "  --help                      display this help and exit\n"
	    "  --version                   output version information and exit\n"
	    "\nNUM suffix: b=512, K=1024, KB=1024, M=1048576, MB=1048576, KiB=K, MiB=M\n"
	    "\nWith -r, -n/-c/-b specify how many items to display (default: all).\n"
	    "If no FILE, or FILE is -, read from standard input.\n",
	    stdout);
}

static void print_version(void) { puts(TAIL_VERSION); }

/* -------------------------------------------------------------------------
 * Parse a signed numeric argument, handling leading '+' (from_start).
 * If s starts with '+', *from_start = true and value is the magnitude.
 * If s starts with '-' or no sign, *from_start = false.
 * ------------------------------------------------------------------------- */
static int parse_count(const char *s, int64_t *out, bool *from_start,
		       const char *prog, const char *opt)
{
	if(!s || !*s) goto bad;
	*from_start = false;
	if(*s == '+') {
		*from_start = true;
		s++;
		if(!*s) goto bad;
	}
	if(expand_number(s, out) < 0) {
		if(errno == ERANGE)
			fprintf(stderr, "%s: %s: value out of range: %s\n", prog, opt, s);
		else
			fprintf(stderr, "%s: %s: invalid number: '%s'\n", prog, opt, s);
		return(-1);
	}
	if(*from_start && *out == 0) {
		fprintf(stderr, "%s: %s: +0 is not valid (origin is 1)\n", prog, opt);
		return(-1);
	}
	return(0);
bad:
	fprintf(stderr, "%s: %s: missing numeric argument\n", prog, opt);
	return(-1);
}

/* -------------------------------------------------------------------------
 * Option parsing.  Returns index of first non-option arg, or -1 on error.
 * ------------------------------------------------------------------------- */
static int parse_options(int *argcp, char ***argvp, struct tail_opts *o)
{
	int argc = *argcp;
	char **argv = *argvp;
	int i;
	bool seen_count = false;

	o->progname      = argv[0];
	o->mode          = MODE_LINES;
	o->count         = 10;
	o->from_start    = false;
	o->follow        = false;
	o->follow_mode   = FOLLOW_DESCRIPTOR;
	o->retry         = false;
	o->reverse       = false;
	o->quiet         = false;
	o->verbose       = false;
	o->zero_term     = false;
	o->debug_mode    = false;
	o->sleep_interval= 1.0;
	o->max_unchanged = 5;
	o->npids         = 0;
	o->show_help     = false;
	o->show_version  = false;

	/* Pre-pass: intercept GNU obsolete / BSD historic syntax: -NUM... */
	int new_argc = 1;
	char **new_argv = malloc((size_t)(argc + 1) * sizeof(char *));
	if (!new_argv) { errno = ENOMEM; return -1; }
	new_argv[0] = argv[0];

	bool expects_arg = false;
	for(i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if(strcmp(arg, "--") == 0) {
			while(i < argc) new_argv[new_argc++] = argv[i++];
			break;
		}

		if(expects_arg) {
			expects_arg = false;
			new_argv[new_argc++] = argv[i];
			continue;
		}

		if(strcmp(arg, "-n") == 0 || strcmp(arg, "-c") == 0 || strcmp(arg, "-b") == 0 || strcmp(arg, "-s") == 0 ||
		   strcmp(arg, "--lines") == 0 || strcmp(arg, "--bytes") == 0 || strcmp(arg, "--blocks") == 0 ||
		   strcmp(arg, "--sleep-interval") == 0 || strcmp(arg, "--max-unchanged-stats") == 0 || strcmp(arg, "--pid") == 0) {
			expects_arg = true;
			new_argv[new_argc++] = argv[i];
			continue;
		}

		if(arg[0] == '-' && arg[1] >= '0' && arg[1] <= '9') {
			char *endp; long long n;
			int64_t mult = 1;
			tail_mode_t pack_mode = MODE_LINES;
			bool do_follow = false;

			errno = 0;
			n = strtoll(arg + 1, &endp, 10);
			if(errno == ERANGE || n < 0) {
				fprintf(stderr, "%s: invalid count '%s'\n", o->progname, arg + 1);
				free(new_argv);
				return(-1);
			}
			for(; *endp != '\0'; endp++) {
				switch(*endp) {
				case 'b': mult = BLOCK_SIZE; pack_mode = MODE_BYTES; break;
				case 'c': pack_mode = MODE_BYTES; break;
				case 'l': pack_mode = MODE_LINES; break;
				case 'f': do_follow = true; break;
				default:
					fprintf(stderr, "%s: invalid option '%s'\n", o->progname, arg);
					usage(stderr, o->progname);
					free(new_argv);
					return(-1);
				}
			}
			if(n > 0 && mult > INT64_MAX / n) {
				fprintf(stderr, "%s: value out of range '%s'\n", o->progname, arg + 1);
				free(new_argv);
				return(-1);
			}
			if(seen_count && o->mode != pack_mode) {
				fprintf(stderr, "%s: cannot combine -b, -c, and -n\n", o->progname);
				free(new_argv);
				return(-1);
			}
			o->mode = pack_mode;
			o->count = n * mult;
			o->from_start = false;
			seen_count = true;
			if(do_follow) { o->follow = true; o->follow_mode = FOLLOW_DESCRIPTOR; }
			continue; /* omit this arg from new_argv */
		}
		new_argv[new_argc++] = argv[i];
	}
	new_argv[new_argc] = NULL;

	/* Update argc/argv for getopt_long */
	*argcp = new_argc;
	*argvp = new_argv;

	static const struct option longopts[] = {
		{"lines", required_argument, NULL, 'n'},
		{"bytes", required_argument, NULL, 'c'},
		{"blocks", required_argument, NULL, 'b'},
		{"follow", optional_argument, NULL, 1000},
		{"retry", no_argument, NULL, 1001},
		{"sleep-interval", required_argument, NULL, 's'},
		{"max-unchanged-stats", required_argument, NULL, 1002},
		{"pid", required_argument, NULL, 1003},
		{"debug", no_argument, NULL, 1004},
		{"zero-terminated", no_argument, NULL, 'z'},
		{"quiet", no_argument, NULL, 'q'},
		{"silent", no_argument, NULL, 'q'},
		{"verbose", no_argument, NULL, 'v'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	int opt;
	optind = 1;
	while((opt = getopt_long(new_argc, new_argv, "n:c:b:fFrqzs:v", longopts, NULL)) != -1) {
		switch(opt) {
		case 'n':
		case 'c':
		case 'b': {
			tail_mode_t this_mode = (opt == 'n') ? MODE_LINES : (opt == 'c') ? MODE_BYTES : MODE_BLOCKS;
			if(seen_count && o->mode != this_mode) {
				fprintf(stderr, "%s: cannot combine -b, -c, and -n\n", o->progname);
				free(new_argv); return(-1);
			}
			if(parse_count(optarg, &o->count, &o->from_start, o->progname,
				(const char[]){'-', (char)opt, '\0'}) < 0) {
				free(new_argv); return(-1);
			}
			o->mode = this_mode;
			seen_count = true;
			break;
		}
		case 'f':
			o->follow = true;
			o->follow_mode = FOLLOW_DESCRIPTOR;
			break;
		case 'F':
			o->follow = true;
			o->follow_mode = FOLLOW_NAME;
			o->retry = true;
			break;
		case 'r':
			o->reverse = true;
			break;
		case 'q':
			o->quiet = true;
			break;
		case 'v':
			o->verbose = true;
			break;
		case 'z':
			o->zero_term = true;
			break;
		case 's': {
			char *ep; double d = strtod(optarg, &ep);
			if(ep == optarg || *ep != '\0' || d < 0.0) {
				fprintf(stderr, "%s: invalid sleep interval '%s'\n", o->progname, optarg);
				free(new_argv); return(-1);
			}
			o->sleep_interval = d;
			break;
		}
		case 1000: /* --follow */
			o->follow = true;
			if(optarg) {
				if(strcmp(optarg, "name") == 0)
					o->follow_mode = FOLLOW_NAME;
				else if(strcmp(optarg, "descriptor") == 0)
					o->follow_mode = FOLLOW_DESCRIPTOR;
				else {
					fprintf(stderr, "%s: invalid argument '%s' for '--follow'\n", o->progname, optarg);
					free(new_argv); return(-1);
				}
			} else {
				o->follow_mode = FOLLOW_DESCRIPTOR;
			}
			break;
		case 1001: /* --retry */
			o->retry = true;
			break;
		case 1002: /* --max-unchanged-stats */ {
			char *ep; long v = strtol(optarg, &ep, 10);
			if(ep == optarg || *ep != '\0' || v < 0) {
				fprintf(stderr, "%s: invalid max-unchanged-stats '%s'\n", o->progname, optarg);
				free(new_argv); return(-1);
			}
			o->max_unchanged = (int)v;
			break;
		}
		case 1003: /* --pid */
			if(o->npids >= MAX_PIDS) {
				fprintf(stderr, "%s: too many --pid values\n", o->progname);
				free(new_argv); return(-1);
			}
			{
				char *ep; long pid = strtol(optarg, &ep, 10);
				if(ep == optarg || *ep != '\0' || pid <= 0) {
					fprintf(stderr, "%s: invalid PID '%s'\n", o->progname, optarg);
					free(new_argv); return(-1);
				}
				o->pids[o->npids++] = (pid_t)pid;
			}
			break;
		case 1004: /* --debug */
			o->debug_mode = true;
			break;
		case 'h':
			o->show_help = true;
			break;
		case 'V':
			o->show_version = true;
			break;
		case '?':
		default:
			usage(stderr, o->progname);
			free(new_argv);
			return(-1);
		}
	}

	/* Validation */
	if(o->reverse && o->follow) {
		fprintf(stderr, "%s: cannot combine -r with -f or -F\n", o->progname);
		free(new_argv);
		return(-1);
	}

	/* o->quiet overrides o->verbose */
	if(o->quiet) o->verbose = false;

	/* Normalise blocks → bytes */
	if(o->mode == MODE_BLOCKS) {
		if(!o->from_start && o->count > INT64_MAX / BLOCK_SIZE) {
			fprintf(stderr, "%s: block count overflow\n", o->progname);
			return(-1);
		}
		o->count *= BLOCK_SIZE;
		o->mode   = MODE_BYTES;
	}

	/* Default: -n 10 from end */
	if(!seen_count) {
		o->mode       = MODE_LINES;
		o->count      = 10;
		o->from_start = false;
	}

	return(optind);
}

/* -------------------------------------------------------------------------
 * I/O helpers
 * ------------------------------------------------------------------------- */
static int write_all(const unsigned char *buf, size_t n)
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

	size_t cap = (size_t)count;
	unsigned char *ring = malloc(cap);
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

	/* Buffer entire input then find the right offset */
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

	/* Find position of (count)-th delimiter from the end */
	int64_t delims = 0;
	size_t  start = 0;
	for(size_t k = used; k > 0; k--) {
		if(data[k - 1] == (char)delim) {
			/* Don't count a delimiter if it's the very last byte of the input */
			if(k == used)
				continue;

			delims++;
			if(delims == count) { start = k; break; }
		}
	}
	int rc = write_all(data + start, used - start);
	free(data);
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
static int process_fd(int fd, const struct tail_opts *o)
{
	unsigned char delim = o->zero_term ? '\0' : '\n';

	if(o->reverse) {
		int64_t count = (o->count == 10 && !o->from_start) ? INT64_MAX : o->count;
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

/* -------------------------------------------------------------------------
 * Header emission (same POSIX rule as head: no leading NL before first).
 * ------------------------------------------------------------------------- */
static int emit_header(const char *name, bool first)
{
	char hdr[1024];
	int rc = 0;
	if(!first) rc = (int)write_all((const unsigned char *)"\n", 1);
	if(rc < 0) return(-1);
	int len = snprintf(hdr, sizeof(hdr), "==> %s <==\n", name);
	if(len < 0 || (size_t)len >= sizeof(hdr)) {
		fprintf(stdout, "==> %s <==\n", name);
		fflush(stdout);
		return(0);
	}
	return(write_all((unsigned char *)hdr, (size_t)len));
}

/* -------------------------------------------------------------------------
 * Sleep helper: sleep for 'seconds' (supports sub-second via nanosleep).
 * ------------------------------------------------------------------------- */
static void do_sleep(double seconds)
{
	struct timespec ts;
	ts.tv_sec  = (time_t)seconds;
	ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
	while(nanosleep(&ts, &ts) < 0 && errno == EINTR) {}
}

/* -------------------------------------------------------------------------
 * Check whether all watched PIDs have terminated.
 * ------------------------------------------------------------------------- */
static bool all_pids_gone(const struct tail_opts *o)
{
	for(int i = 0; i < o->npids; i++) {
		if(kill(o->pids[i], 0) == 0 || errno == EPERM)
			return(false);  /* still running */
	}
	return(true);
}

/* -------------------------------------------------------------------------
 * Follow state per tracked file.
 * ------------------------------------------------------------------------- */
struct follow_file {
	const char *path;		/* NULL for stdin */
	int         fd;
	dev_t       dev;
	ino_t       ino;
	off_t       pos;		/* last known read position */
	off_t       last_size;
	int         unchanged_count;
	bool        missing;
};

/* -------------------------------------------------------------------------
 * Follow loop: called after initial tail output for each file.
 * For simplicity we use polling (nanosleep); event backends can be added later.
 * TAIL-FR-040 notes event-driven as SHOULD, so polling is conformant.
 * ------------------------------------------------------------------------- */
static int follow_files(struct follow_file *files, int nfiles,
			const struct tail_opts *o,
			bool show_headers,
			int *last_active_idx)
{
	unsigned char buf[TAIL_BUFSZ];

	for(;;) {
		/* Check --pid */
		if(o->npids > 0 && all_pids_gone(o)) {
			if(o->debug_mode)
				fprintf(stderr, "tail: all watched PIDs gone, exiting\n");
			break;
		}

		bool any_data = false;

		for(int fi = 0; fi < nfiles; fi++) {
			struct follow_file *ff = &files[fi];

			/* Reopen attempts for follow-by-name */
			if(ff->fd < 0 && ff->path) {
				if(o->retry || o->follow_mode == FOLLOW_NAME) {
					int newfd = open(ff->path, O_RDONLY);
					if(newfd >= 0) {
						struct stat st;
						if(fstat(newfd, &st) == 0) {
							ff->fd   = newfd;
							ff->dev  = st.st_dev;
							ff->ino  = st.st_ino;
							ff->pos  = 0;
							ff->last_size = st.st_size;
							ff->missing = false;
							if(o->debug_mode)
								fprintf(stderr,
								    "tail: '%s' appeared\n", ff->path);
						} else {
							close(newfd);
						}
					}
				}
				if(ff->fd < 0) continue;
			}

			if(ff->fd < 0) continue;

			/* For follow-by-name: check rotation / truncation */
			if(o->follow_mode == FOLLOW_NAME && ff->path) {
				ff->unchanged_count++;
				if(ff->unchanged_count >= o->max_unchanged || true) {
					ff->unchanged_count = 0;
					struct stat st;
					if(stat(ff->path, &st) < 0) {
						if(!ff->missing) {
							if(o->debug_mode)
								fprintf(stderr,
								    "tail: '%s' became inaccessible\n",
								    ff->path);
							ff->missing = true;
						}
					} else {
						ff->missing = false;
						/* Rotation: different inode/device */
						if(st.st_ino != ff->ino || st.st_dev != ff->dev) {
							if(o->debug_mode)
								fprintf(stderr,
								    "tail: '%s' rotated (inode changed)\n",
								    ff->path);
							close(ff->fd);
							ff->fd = open(ff->path, O_RDONLY);
							if(ff->fd >= 0) {
								fstat(ff->fd, &st);
								ff->ino  = st.st_ino;
								ff->dev  = st.st_dev;
								ff->pos  = 0;
								ff->last_size  = st.st_size;
							}
						} else if(st.st_size < ff->last_size) {
							/* Truncation */
							if(o->debug_mode)
								fprintf(stderr,
								    "tail: '%s' truncated\n", ff->path);
							lseek(ff->fd, 0, SEEK_SET);
							ff->pos = 0;
							ff->last_size = st.st_size;
						} else {
							ff->last_size = st.st_size;
						}
					}
				}
			}

			/* Read new data */
			for(;;) {
				ssize_t n = read(ff->fd, buf, sizeof(buf));
				if(n < 0) {
					if(errno == EINTR) continue;
					break;  /* transient error */
				}
				if(n == 0) break;
				any_data = true;

				if(show_headers && nfiles > 1 && fi != *last_active_idx) {
					if(emit_header(ff->path ? ff->path : "(standard input)",
					    *last_active_idx < 0) < 0)
						return(-1);
					*last_active_idx = fi;
				}
				if(write_all(buf, (size_t)n) < 0) return(-1);
				ff->pos += n;
			}
		}

		if(!any_data)
			do_sleep(o->sleep_interval);
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
	struct tail_opts o;
	char **parsed_argv = argv;
	int parsed_argc = argc;

	int first_file = parse_options(&parsed_argc, &parsed_argv, &o);
	if(first_file < 0) return(1);

	if(o.show_help)    { print_help(&o);   return(0); }
	if(o.show_version) { print_version();  return(0); }

	int nfiles      = parsed_argc - first_file;
	int exit_status = 0;

	bool show_headers;
	if(o.quiet)        show_headers = false;
	else if(o.verbose) show_headers = true;
	else               show_headers = (nfiles > 1);

	unsigned char delim = o.zero_term ? '\0' : '\n';
	(void)delim;

	if(nfiles == 0) {
		/* stdin only */
		bool is_pipe = false;
		{
			struct stat st;
			if(fstat(STDIN_FILENO, &st) == 0)
				is_pipe = S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode);
		}
		if(show_headers)
			if(emit_header("(standard input)", true) < 0) return(1);

		if(process_fd(STDIN_FILENO, &o) < 0) {
			fprintf(stderr, "%s: (standard input): %s\n",
			    o.progname, strerror(errno));
			exit_status = 1;
		}

		if(o.follow && !is_pipe && exit_status == 0) {
			struct follow_file ff;
			memset(&ff, 0, sizeof(ff));
			ff.fd   = STDIN_FILENO;
			ff.path = NULL;
			{
				struct stat st;
				if(fstat(STDIN_FILENO, &st) == 0) {
					ff.dev = st.st_dev; ff.ino = st.st_ino;
					ff.last_size = st.st_size;
				}
			}
			int last = 0;
			if(follow_files(&ff, 1, &o, false, &last) < 0) exit_status = 1;
		}
		return(exit_status);
	}

	/* Multiple operands */
	struct follow_file *ffs = NULL;
	if(o.follow) {
		ffs = calloc((size_t)nfiles, sizeof(struct follow_file));
		if(!ffs) { perror(o.progname); return(1); }
	}

	bool first = true;
	for(int i = first_file; i < parsed_argc; i++) {
		const char *name  = parsed_argv[i];
		bool  is_stdin    = (strcmp(name, "-") == 0);
		int   fd;

		if(show_headers) {
			const char *display = is_stdin ? "(standard input)" : name;
			if(emit_header(display, first) < 0) { exit_status = 1; goto next; }
		}

		if(is_stdin) {
			fd = STDIN_FILENO;
		} else {
			fd = open(name, O_RDONLY);
			if(fd < 0) {
				fprintf(stderr, "%s: %s: %s\n", o.progname, name, strerror(errno));
				exit_status = 1;
				if(o.follow && ffs) {
					int fi = i - first_file;
					ffs[fi].path = name;
					ffs[fi].fd   = -1;
					ffs[fi].missing = true;
				}
				first = false;
				continue;
			}
		}

		if(process_fd(fd, &o) < 0) {
			fprintf(stderr, "%s: %s: %s\n", o.progname, name, strerror(errno));
			exit_status = 1;
		}

		if(o.follow && ffs) {
			int fi = i - first_file;
			struct stat st;
			ffs[fi].path = is_stdin ? NULL : name;
			ffs[fi].fd   = fd;  /* keep open */
			if(fstat(fd, &st) == 0) {
				ffs[fi].dev  = st.st_dev;
				ffs[fi].ino  = st.st_ino;
				ffs[fi].last_size = st.st_size;
				ffs[fi].pos  = lseek(fd, 0, SEEK_CUR);
			}
		} else {
			if(!is_stdin) close(fd);
		}
next:
		first = false;
	}

	if(o.follow && ffs && exit_status == 0) {
		int last_active = -1;
		if(follow_files(ffs, nfiles, &o, show_headers, &last_active) < 0)
			exit_status = 1;
		for(int i = 0; i < nfiles; i++)
			if(ffs[i].fd >= 0 && ffs[i].path)
				close(ffs[i].fd);
	}

	free(ffs);
	return(exit_status);
}
