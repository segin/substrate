/*
 * tail_parse.c - CLI parsing for tail(1)
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "getopt.h"
#include "tail.h"
#include <sys/types.h>

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
 * Usage / version
 * ------------------------------------------------------------------------- */
void usage(FILE *f, const char *p)
{
	fprintf(f,
	    "Usage: %s [-f|-F|-r] [-c [-|+]N | -n [-|+]N | -b N] [-qvz] [file...]\n", p);
}

void print_help(const struct tail_opts *o)
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

void print_version(void) { puts(TAIL_VERSION); }

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
	} else if(*s == '-') {
		/* Explicit leading '-' is the documented from-end form
		 * (`tail -n -5` == `tail -n 5`); consume it and parse the
		 * magnitude, or the negative value would reach the printer
		 * as "output nothing" (TAIL-02). */
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
int parse_options(int *argcp, char ***argvp, struct tail_opts *o)
{
	int argc = *argcp;
	char **argv = *argvp;
	int i;
	bool seen_count = false;

	o->progname      = argv[0];
	o->mode          = MODE_LINES;
	o->count         = 10;
	o->from_start    = false;
	o->count_explicit = false;
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
			o->count_explicit = true;
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
			o->count_explicit = true;
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
