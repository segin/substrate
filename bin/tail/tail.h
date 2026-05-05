/*
 * tail.h - shared types and declarations for tail(1)
 */

#ifndef TAIL_H
#define TAIL_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */
#define TAIL_VERSION	"tail (Substrate) 1.0"
#define TAIL_BUFSZ	65536u
#define BLOCK_SIZE	512	/* -b unit */
#define MAX_PIDS	64

/* -------------------------------------------------------------------------
 * Enumerations
 * ------------------------------------------------------------------------- */
typedef enum { MODE_LINES, MODE_BYTES, MODE_BLOCKS } tail_mode_t;
typedef enum { FOLLOW_NONE, FOLLOW_DESCRIPTOR, FOLLOW_NAME } follow_mode_t;

/* -------------------------------------------------------------------------
 * Option structure
 * ------------------------------------------------------------------------- */
struct tail_opts {
	const char *progname;
	tail_mode_t  mode;
	int64_t      count;		/* positive = from end, negative = from start (POSIX '+') */
	bool         from_start;	/* true if leading '+' (start-relative) */
	bool         count_explicit;	/* true if -n/-c/-b was given by user */
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
 * Follow state per tracked file
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
 * Function declarations: tail_parse.c
 * ------------------------------------------------------------------------- */
int parse_options(int *argcp, char ***argvp, struct tail_opts *o);
void usage(FILE *f, const char *p);
void print_help(const struct tail_opts *o);
void print_version(void);

/* -------------------------------------------------------------------------
 * Function declarations: tail_io.c
 * ------------------------------------------------------------------------- */
int write_all(const unsigned char *buf, size_t n);
int process_fd(int fd, const struct tail_opts *o);

/* -------------------------------------------------------------------------
 * Function declarations: tail_follow.c
 * ------------------------------------------------------------------------- */
int follow_files(struct follow_file *files, int nfiles,
		 const struct tail_opts *o,
		 bool show_headers,
		 int *last_active_idx);

/* -------------------------------------------------------------------------
 * Function declarations: tail_main.c
 * ------------------------------------------------------------------------- */
int emit_header(const char *name, bool first);

#endif /* TAIL_H */
