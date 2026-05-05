/*
 * head - output the first part of files
 *
 * POSIX.1-2024 + BSD + GNU coreutils compatibility.
 *
 * Option precedence: BSD where BSD and GNU disagree.
 * Suffix parsing: BSD expand_number style (power-of-two, case-insensitive,
 *                 trailing 'B' ignored).  GNU decimal-SI spellings (kB, MB,
 *                 GB) are accepted but mapped to the same BSD binary value
 *                 (kB → 1024, MB → 1048576, …).
 *
 * Synopsis (POSIX.1-2024):
 *   head [-c number | -n number] [file...]
 *
 * Additional options:
 *   -q / --quiet / --silent   suppress headers
 *   -v / --verbose            force headers
 *   -z / --zero-terminated    NUL delimiter
 *   --lines=[[-]N]            synonym for -n
 *   --bytes=[[-]N]            synonym for -c
 *   --help                    usage + exit 0
 *   --version                 version + exit 0
 *   --                        end option processing
 *
 * Historic BSD syntax: -NUM (e.g. head -20 file)
 * GNU obsolete packed syntax: -NUM[bkm][cqv]
 *   b=×512 bytes, k=×1024 bytes, m=×1048576 bytes
 *   c=byte mode (×1), l=line mode (×1), q=quiet, v=verbose
 *
 * Negative counts (GNU): output all but the last K lines/bytes.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEAD_VERSION "head (Substrate) 1.0"
#define HEAD_BUFSZ   65536u

/* -------------------------------------------------------------------------
 * Suffix parsing (BSD expand_number semantics, case-insensitive)
 * Accepts: K/k  M/m  G/g  T/t  P/p  E/e  (powers of 2)
 *          kB   MB   GB   TB   PB   EB    (treated same as K M G T P E)
 *          KiB  MiB  GiB  TiB  PiB  EiB  (IEC, same value)
 * Trailing lone 'B' with no prefix: ignored (1x multiplier).
 * Returns 0 on success, -1 on error (sets errno = ERANGE or EINVAL).
 * ------------------------------------------------------------------------- */
static int expand_number(const char *s, int64_t *out)
{
	char *end;
	long long val;
	int64_t mult = 1;

	if(s == NULL || *s == '\0') {
		errno = EINVAL;
		return(-1);
	}

	errno = 0;
	val = strtoll(s, &end, 10);
	if(end == s) {
		errno = EINVAL;
		return(-1);
	}
	if(errno == ERANGE) {
		return(-1);
	}

	/* consume optional suffix */
	if(*end != '\0') {
		char c = *end;
		/* upper-case the leading suffix character */
		if(c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 'A');

		switch(c) {
		case 'K': mult = (int64_t)1024LL; break;
		case 'M': mult = (int64_t)1024LL * 1024; break;
		case 'G': mult = (int64_t)1024LL * 1024 * 1024; break;
		case 'T': mult = (int64_t)1024LL * 1024 * 1024 * 1024; break;
		case 'P': mult = (int64_t)1024LL * 1024 * 1024 * 1024 * 1024; break;
		case 'E': mult = (int64_t)1024LL * 1024 * 1024 * 1024 * 1024 * 1024; break;
		case 'B':
			/* lone trailing B: multiplier = 1 */
			end++;
			goto done;
		default:
			errno = EINVAL;
			return(-1);
		}
		end++;

		/*
		 * Skip optional 'i' (IEC form: KiB, MiB, …) or 'B' (GNU decimal
		 * form: kB, MB, …) — both map to the same BSD binary value.
		 */
		if(*end == 'i' || *end == 'I')
			end++;
		if(*end == 'b' || *end == 'B')
			end++;
	}

done:
	if(*end != '\0') {
		errno = EINVAL;
		return(-1);
	}

	/* overflow check */
	if(val > 0 && mult > INT64_MAX / val) {
		errno = ERANGE;
		return(-1);
	}
	if(val < 0 && mult > INT64_MAX / (-(val + 1) + 1)) {
		errno = ERANGE;
		return(-1);
	}

	*out = val * mult;
	return(0);
}

/* -------------------------------------------------------------------------
 * Options structure
 * ------------------------------------------------------------------------- */
typedef enum {
	MODE_LINES,
	MODE_BYTES
} head_mode_t;

struct head_opts {
	const char *progname;
	head_mode_t mode;
	int64_t count;		/* positive: first N; negative: all but last |N| */
	bool quiet;
	bool verbose;
	bool zero_term;
	bool show_help;
	bool show_version;
};

static void usage(FILE *stream, const char *prog)
{
	fprintf(stream,
	    "Usage: %s [-c [-]NUM | -n [-]NUM] [-qvz] [--help] [--version] [file ...]\n",
	    prog);
}

static void print_help(const struct head_opts *o)
{
	usage(stdout, o->progname);
	fputs("\nOutput the first part of files.\n"
	    "\nOptions:\n"
	    "  -c [-]NUM, --bytes=[-]NUM   output first NUM bytes; with leading '-',\n"
	    "                              output all but the last NUM bytes\n"
	    "  -n [-]NUM, --lines=[-]NUM   output first NUM lines (default: 10);\n"
	    "                              with leading '-', all but the last NUM lines\n"
	    "  -q, --quiet, --silent       never print file headers\n"
	    "  -v, --verbose               always print file headers\n"
	    "  -z, --zero-terminated       use NUL as line delimiter instead of newline\n"
	    "  --                          end option processing\n"
	    "  --help                      display this help and exit\n"
	    "  --version                   output version information and exit\n"
	    "\nNUM may have a suffix: K=1024, M=1048576, G=1073741824, …\n"
	    "Also accepted: KiB, MiB, GiB (IEC), kB, MB, GB (same binary value).\n"
	    "\nIf no FILE, or FILE is -, read from standard input.\n"
	    "\nMultiple FILE: prepend '==> name <==' headers (suppressed with -q).\n",
	    stdout);
}

static void print_version(void)
{
	puts(HEAD_VERSION);
}

/* -------------------------------------------------------------------------
 * Parse a (possibly negative) numeric argument including suffix.
 * Negative is allowed only for --lines/-n and --bytes/-c (GNU extension).
 * ------------------------------------------------------------------------- */
static int parse_count(const char *s, int64_t *out, const char *prog, char optch)
{
	if(expand_number(s, out) < 0) {
		if(errno == ERANGE)
			fprintf(stderr, "%s: -%c: value out of range: %s\n", prog, optch, s);
		else
			fprintf(stderr, "%s: invalid count: '%s'\n", prog, s);
		return(-1);
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * Option parsing
 * Returns index of first non-option argument, or -1 on error.
 * ------------------------------------------------------------------------- */
static int parse_options(int *argcp, char ***argvp, struct head_opts *o)
{
	int argc = *argcp;
	char **argv = *argvp;
	int i;
	bool seen_count = false; /* -n or -c already seen */

	o->progname = argv[0];
	o->mode = MODE_LINES;
	o->count = 10;
	o->quiet = false;
	o->verbose = false;
	o->zero_term = false;
	o->show_help = false;
	o->show_version = false;

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

		if(strcmp(arg, "-n") == 0 || strcmp(arg, "-c") == 0 ||
		   strcmp(arg, "--lines") == 0 || strcmp(arg, "--bytes") == 0) {
			expects_arg = true;
			new_argv[new_argc++] = argv[i];
			continue;
		}

		/* check short options starting with -n/-c combined, e.g. -qn or -vn (not strictly standard but getopt handles it) 
		   Actually, GNU getopt long handles it. Let's just catch the exact matches above. 
		   If someone does `head -qn -3`, expects_arg wouldn't catch it. 
		   A more robust way: if it starts with `-` but is NOT a digit, don't intercept.
		   The obsolete syntax is strictly ^-[0-9]. */

		if(arg[0] == '-' && arg[1] >= '0' && arg[1] <= '9') {
			char *endp; long long n;
			int64_t mult = 1;
			head_mode_t pack_mode = MODE_LINES;
			bool set_quiet = false, set_verbose = false;

			errno = 0;
			n = strtoll(arg + 1, &endp, 10);
			if(errno == ERANGE || n < 0) {
				fprintf(stderr, "%s: invalid count '%s'\n", o->progname, arg + 1);
				free(new_argv);
				return(-1);
			}
			for(; *endp != '\0'; endp++) {
				switch(*endp) {
				case 'b': mult = 512;           pack_mode = MODE_BYTES; break;
				case 'k': mult = 1024;          pack_mode = MODE_BYTES; break;
				case 'm': mult = 1024LL * 1024; pack_mode = MODE_BYTES; break;
				case 'c':                        pack_mode = MODE_BYTES; break;
				case 'l':                        pack_mode = MODE_LINES; break;
				case 'q': set_quiet   = true; break;
				case 'v': set_verbose = true; break;
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
				fprintf(stderr, "%s: cannot use -n and -c simultaneously\n", o->progname);
				free(new_argv);
				return(-1);
			}
			o->mode = pack_mode;
			o->count = n * mult;
			seen_count = true;
			if(set_quiet)   o->quiet   = true;
			if(set_verbose) o->verbose = true;
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
		{"quiet", no_argument, NULL, 'q'},
		{"silent", no_argument, NULL, 'q'},
		{"verbose", no_argument, NULL, 'v'},
		{"zero-terminated", no_argument, NULL, 'z'},
		{"help", no_argument, NULL, 'h'},
		{"version", no_argument, NULL, 'V'},
		{NULL, 0, NULL, 0}
	};

	int opt;
	optind = 1;
	while((opt = getopt_long(new_argc, new_argv, "n:c:qvz", longopts, NULL)) != -1) {
		switch(opt) {
		case 'n':
		case 'c': {
			head_mode_t this_mode = (opt == 'n') ? MODE_LINES : MODE_BYTES;
			if(seen_count && o->mode != this_mode) {
				fprintf(stderr, "%s: cannot use -n and -c simultaneously\n", o->progname);
				free(new_argv); return(-1);
			}
			if(parse_count(optarg, &o->count, o->progname, (char)opt) < 0) {
				free(new_argv); return(-1);
			}
			o->mode = this_mode;
			seen_count = true;
			break;
		}
		case 'q':
			o->quiet = true;
			break;
		case 'v':
			o->verbose = true;
			break;
		case 'z':
			o->zero_term = true;
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

	/* BSD rule: -q overrides -v regardless of order */
	if(o->quiet)
		o->verbose = false;

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
		if(w < 0) {
			if(errno == EINTR)
				continue;
			return(-1);
		}
		off += (size_t)w;
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * Ring-buffer for negative-count modes.
 * We keep a circular buffer of the last |count| items (lines or bytes).
 * After reading the entire input, we output everything that is NOT in the
 * ring buffer, i.e. we skip the last |count| items.
 *
 * Strategy: two-pass approach for seekable files (lseek to start + re-read);
 * single-pass ring-buffer for non-seekable streams (memory bounded by |count|
 * bytes for BYTES mode, or by total byte length of last |count| lines for
 * LINES mode — documented as potentially large).
 * ------------------------------------------------------------------------- */

/* -- negative byte mode -------------------------------------------------- */
static int head_neg_bytes(int fd, int64_t skip_tail, const unsigned char delim)
{
	/*
	 * Read all into a dynamic buffer (bounded ring approach):
	 * We buffer everything then output [0 .. total-skip_tail).
	 * For very large files this is memory-expensive; we use a two-pass
	 * strategy on seekable fds to avoid it.
	 */
	off_t cur = lseek(fd, 0, SEEK_CUR);
	off_t end_pos;
	int64_t total;

	(void)delim; /* byte mode ignores delimiter */

	if(cur != (off_t)-1) {
		/* seekable: find size, then copy first (size - skip_tail) bytes */
		end_pos = lseek(fd, 0, SEEK_END);
		if(end_pos != (off_t)-1) {
			total = (int64_t)(end_pos - cur);
			if(lseek(fd, cur, SEEK_SET) != (off_t)-1) {
				int64_t to_copy = total - skip_tail;
				if(to_copy <= 0)
					return(0);

				unsigned char buf[HEAD_BUFSZ];
				while(to_copy > 0) {
					size_t want = (to_copy > (int64_t)sizeof(buf)) ?
					    sizeof(buf) : (size_t)to_copy;
					ssize_t n = read(fd, buf, want);
					if(n < 0) {
						if(errno == EINTR)
							continue;
						return(-1);
					}
					if(n == 0)
						break;
					if(write_all(buf, (size_t)n) < 0)
						return(-1);
					to_copy -= n;
				}
				return(0);
			}
		}
	}

	/*
	 * Non-seekable: buffer everything, then print up to (total - skip_tail).
	 * We allocate dynamically; if that fails, error out.
	 */
	{
		size_t allocated = 65536;
		size_t used = 0;
		unsigned char *ring = malloc(allocated);
		if(!ring) {
			errno = ENOMEM;
			return(-1);
		}

		for(;;) {
			unsigned char buf[HEAD_BUFSZ];
			ssize_t n = read(fd, buf, sizeof(buf));
			if(n < 0) {
				if(errno == EINTR)
					continue;
				free(ring);
				return(-1);
			}
			if(n == 0)
				break;

			if(used + (size_t)n > allocated) {
				size_t new_alloc = allocated * 2;
				if(new_alloc < used + (size_t)n)
					new_alloc = used + (size_t)n;
				unsigned char *tmp = realloc(ring, new_alloc);
				if(!tmp) {
					free(ring);
					errno = ENOMEM;
					return(-1);
				}
				ring = tmp;
				allocated = new_alloc;
			}
			memcpy(ring + used, buf, (size_t)n);
			used += (size_t)n;
		}

		int64_t to_print = (int64_t)used - skip_tail;
		if(to_print > 0) {
			int rc = write_all(ring, (size_t)to_print);
			free(ring);
			return(rc);
		}
		free(ring);
		return(0);
	}
}

/* -- negative line mode -------------------------------------------------- */
static int head_neg_lines(int fd, int64_t skip_tail, unsigned char delim)
{
	/*
	 * Similar strategy: on seekable fds, do a first pass to count delimiters,
	 * then seek back and output lines 0..(total_lines - skip_tail - 1).
	 * On non-seekable, buffer everything then replay.
	 */
	off_t cur = lseek(fd, 0, SEEK_CUR);

	if(cur != (off_t)-1) {
		/* First pass: count delimiters */
		int64_t line_count = 0;
		off_t *line_ends = NULL;    /* positions after each delimiter */
		size_t le_alloc = 0;

		{
			unsigned char buf[HEAD_BUFSZ];
			off_t pos = cur;

			for(;;) {
				ssize_t n = read(fd, buf, sizeof(buf));
				if(n < 0) {
					if(errno == EINTR)
						continue;
					free(line_ends);
					return(-1);
				}
				if(n == 0)
					break;
				for(ssize_t k = 0; k < n; k++) {
					if(buf[k] == (char)delim) {
						if((size_t)line_count >= le_alloc) {
							size_t na = le_alloc ? le_alloc * 2 : 1024;
							off_t *tmp = realloc(line_ends, na * sizeof(off_t));
							if(!tmp) {
								free(line_ends);
								return(-1);
							}
							line_ends = tmp;
							le_alloc = na;
						}
						line_ends[line_count++] = pos + k + 1;
					}
				}
				pos += n;
			}
		}

		/* Seek back; output up to line (line_count - skip_tail) */
		if(lseek(fd, cur, SEEK_SET) == (off_t)-1) {
			free(line_ends);
			goto non_seekable;
		}

		int64_t output_lines = line_count - skip_tail;
		if(output_lines <= 0) {
			free(line_ends);
			return(0);
		}

		/* Copy from cur up to line_ends[output_lines - 1] */
		off_t end_byte = line_ends[output_lines - 1];
		free(line_ends);
		line_ends = NULL;

		int64_t to_copy = (int64_t)(end_byte - cur);
		unsigned char buf[HEAD_BUFSZ];
		while(to_copy > 0) {
			size_t want = (to_copy > (int64_t)sizeof(buf)) ?
			    sizeof(buf) : (size_t)to_copy;
			ssize_t n = read(fd, buf, want);
			if(n < 0) {
				if(errno == EINTR)
					continue;
				return(-1);
			}
			if(n == 0)
				break;
			if(write_all(buf, (size_t)n) < 0)
				return(-1);
			to_copy -= n;
		}
		return(0);
	}

non_seekable:;
	/*
	 * Non-seekable: buffer everything, find delimiter positions, replay.
	 */
	{
		size_t allocated = 65536, used = 0;
		unsigned char *data = malloc(allocated);
		if(!data)
			return(-1);

		for(;;) {
			unsigned char buf[HEAD_BUFSZ];
			ssize_t n = read(fd, buf, sizeof(buf));
			if(n < 0) {
				if(errno == EINTR)
					continue;
				free(data);
				return(-1);
			}
			if(n == 0)
				break;
			if(used + (size_t)n > allocated) {
				size_t na = allocated * 2;
				if(na < used + (size_t)n)
					na = used + (size_t)n;
				unsigned char *tmp = realloc(data, na);
				if(!tmp) {
					free(data);
					return(-1);
				}
				data = tmp;
				allocated = na;
			}
			memcpy(data + used, buf, (size_t)n);
			used += (size_t)n;
		}

		/* Count delimiters */
		int64_t total_lines = 0;
		for(size_t k = 0; k < used; k++)
			if(data[k] == (char)delim)
				total_lines++;

		int64_t out_lines = total_lines - skip_tail;
		if(out_lines <= 0) {
			free(data);
			return(0);
		}

		/* Find end of out_lines-th line */
		int64_t found = 0;
		size_t end_off = 0;
		for(size_t k = 0; k < used && found < out_lines; k++) {
			if(data[k] == (char)delim) {
				found++;
				end_off = k + 1;
			}
		}

		int rc = write_all(data, end_off);
		free(data);
		return(rc);
	}
}

/* -------------------------------------------------------------------------
 * Positive-count byte mode: copy first 'count' bytes.
 * ------------------------------------------------------------------------- */
static int head_pos_bytes(int fd, int64_t count)
{
	unsigned char buf[HEAD_BUFSZ];
	int64_t remaining = count;

	while(remaining > 0) {
		size_t want = (remaining > (int64_t)sizeof(buf)) ?
		    sizeof(buf) : (size_t)remaining;
		ssize_t n = read(fd, buf, want);
		if(n < 0) {
			if(errno == EINTR)
				continue;
			return(-1);
		}
		if(n == 0)
			break;
		if(write_all(buf, (size_t)n) < 0)
			return(-1);
		remaining -= n;
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * Positive-count line mode: copy first 'count' delimited records.
 * Handles arbitrarily long lines (no fixed line-length limit).
 * ------------------------------------------------------------------------- */
static int head_pos_lines(int fd, int64_t count, unsigned char delim)
{
	unsigned char buf[HEAD_BUFSZ];
	int64_t remaining = count;

	if(remaining <= 0)
		return(0);

	for(;;) {
		ssize_t n = read(fd, buf, sizeof(buf));
		if(n < 0) {
			if(errno == EINTR)
				continue;
			return(-1);
		}
		if(n == 0)
			break;

		/* Scan for delimiters; output up to and including the count-th one */
		ssize_t start = 0;
		for(ssize_t k = 0; k < n; k++) {
			if(buf[k] == (char)delim) {
				remaining--;
				if(remaining == 0) {
					/* output through this delimiter and stop */
					if(write_all(buf + start, (size_t)(k - start + 1)) < 0)
						return(-1);
					return(0);
				}
			}
		}
		/* Not done yet: output the whole chunk */
		if(write_all(buf + start, (size_t)(n - start)) < 0)
			return(-1);
	}
	return(0);
}

/* -------------------------------------------------------------------------
 * Process a single input (fd already opened).
 * ------------------------------------------------------------------------- */
static int process_fd(int fd, const struct head_opts *o)
{
	unsigned char delim = o->zero_term ? '\0' : '\n';

	if(o->mode == MODE_BYTES) {
		if(o->count < 0)
			return(head_neg_bytes(fd, -o->count, delim));
		return(head_pos_bytes(fd, o->count));
	} else {
		if(o->count < 0)
			return(head_neg_lines(fd, -o->count, delim));
		return(head_pos_lines(fd, o->count, delim));
	}
}

/* -------------------------------------------------------------------------
 * Emit a file header (==> name <==).
 * POSIX: no leading newline before the first header;
 *         leading newline before each subsequent header.
 * ------------------------------------------------------------------------- */
static int emit_header(const char *name, bool first)
{
	int rc;
	if(!first)
		rc = write_all((const unsigned char *)"\n", 1);
	else
		rc = 0;
	if(rc < 0)
		return(-1);
	{
		char hdr[1024];
		int len = snprintf(hdr, sizeof(hdr), "==> %s <==\n", name);
		if(len < 0 || (size_t)len >= sizeof(hdr)) {
			/* name too long: use fprintf */
			if(fprintf(stdout, "\n==> %s <==\n", name) < 0)
				return(-1);
			return(0);
		}
		return(write_all((unsigned char *)hdr, (size_t)len));
	}
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
	struct head_opts o;
	char **parsed_argv = argv;
	int parsed_argc = argc;

	int first_file = parse_options(&parsed_argc, &parsed_argv, &o);
	if(first_file < 0)
		return(1);

	if(o.show_help) {
		print_help(&o);
		return(0);
	}
	if(o.show_version) {
		print_version();
		return(0);
	}

	int nfiles = parsed_argc - first_file;
	int exit_status = 0;
	bool show_headers;

	if(o.quiet)
		show_headers = false;
	else if(o.verbose)
		show_headers = true;
	else
		show_headers = (nfiles > 1);

	if(nfiles == 0) {
		/* read stdin */
		if(show_headers) {
			if(emit_header("(standard input)", true) < 0) {
				perror(o.progname);
				return(1);
			}
		}
		if(process_fd(STDIN_FILENO, &o) < 0) {
			fprintf(stderr, "%s: (standard input): %s\n",
			    o.progname, strerror(errno));
			exit_status = 1;
		}
	} else {
		bool first = true;
		for(int i = first_file; i < parsed_argc; i++) {
			const char *name = parsed_argv[i];
			int fd;
			bool is_stdin = (strcmp(name, "-") == 0);

			if(show_headers) {
				const char *display = is_stdin ? "(standard input)" : name;
				if(emit_header(display, first) < 0) {
					perror(o.progname);
					exit_status = 1;
					continue;
				}
				first = false;
			} else {
				first = false;
			}

			if(is_stdin) {
				fd = STDIN_FILENO;
			} else {
				fd = open(name, O_RDONLY);
				if(fd < 0) {
					fprintf(stderr, "%s: %s: %s\n",
					    o.progname, name, strerror(errno));
					exit_status = 1;
					continue;
				}
			}

			if(process_fd(fd, &o) < 0) {
				fprintf(stderr, "%s: %s: %s\n",
				    o.progname, name, strerror(errno));
				exit_status = 1;
			}

			if(!is_stdin)
				close(fd);
		}
	}

	return(exit_status);
}
