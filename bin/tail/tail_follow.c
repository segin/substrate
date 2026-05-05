/*
 * tail_follow.c - follow mode for tail(1)
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "tail.h"

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
 * Follow loop: called after initial tail output for each file.
 * For simplicity we use polling (nanosleep); event backends can be added later.
 * TAIL-FR-040 notes event-driven as SHOULD, so polling is conformant.
 * ------------------------------------------------------------------------- */
int follow_files(struct follow_file *files, int nfiles,
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
				if(ff->unchanged_count >= o->max_unchanged) {
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
