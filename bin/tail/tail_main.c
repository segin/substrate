/*
 * tail_main.c - main() and header emission for tail(1)
 *
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "tail.h"
#include <sys/stat.h>
#include <sys/types.h>

/* -------------------------------------------------------------------------
 * Header emission (same POSIX rule as head: no leading NL before first).
 * ------------------------------------------------------------------------- */
int emit_header(const char *name, bool first)
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

	if(nfiles == 0) {
		/* stdin only */
		bool is_pipe = false;
		{
			struct stat st;
			if(fstat(STDIN_FILENO, &st) == 0)
				is_pipe = S_ISFIFO(st.st_mode) || S_ISSOCK(st.st_mode);
		}
		if(show_headers)
			if(emit_header("standard input", true) < 0) return(1);

		if(process_fd(STDIN_FILENO, &o) < 0) {
			fprintf(stderr, "%s: standard input: %s\n",
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
			const char *display = is_stdin ? "standard input" : name;
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
			/* Reject a directory operand rather than reading garbage
			 * (or spinning in follow) on it (TAIL-05..10). */
			struct stat dst;
			if(fstat(fd, &dst) == 0 && S_ISDIR(dst.st_mode)) {
				fprintf(stderr, "%s: error reading '%s': Is a directory\n",
					o.progname, name);
				exit_status = 1;
				close(fd);
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

	/* Enter the follow loop even when some operand failed to open: -F
	 * must wait for a missing file to appear, and -f on a mix of present
	 * and absent files must still follow the present ones (TAIL-04). */
	if(o.follow && ffs) {
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
