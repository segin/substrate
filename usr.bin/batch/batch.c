#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <at.h>

int main(int argc, char *argv[]) {
    struct batch_submit_request req;
    memset(&req, 0, sizeof(req));

    /* batch is setuid root only so it can write the root-owned spool.  Run
     * as the invoking user -- a -f file opened as root would be copied into
     * a job the user owns, disclosing any file on the system -- and take
     * the privilege back just to create the job. */
    if (seteuid(getuid()) != 0) {
        perror("batch: seteuid");
        return 1;
    }

    /* Strict POSIX defaults for bare `batch` */
    req.queue = 'b';
    req.deferred_time = 0; // immediate
    req.profile = AT_PROFILE_POSIX_STRICT;
    req.mail_mode = AT_MAIL_ALWAYS; 
    req.shell_policy = AT_SHELL_BOURNE;
    req.input_fd = STDIN_FILENO;
    req.source_file = NULL;

    /* Phase 6.1: Parse BSD options [-m] [-f file] [-q queue] [timespec] */
    int opt;
    int bsd_mode_activated = 0;

    while ((opt = getopt(argc, argv, "mf:q:")) != -1) {
        bsd_mode_activated = 1;
        switch (opt) {
            case 'm':
                req.mail_mode = AT_MAIL_ALWAYS;
                break;
            case 'f':
                req.source_file = optarg;
                req.input_fd = open(optarg, O_RDONLY);
                if (req.input_fd < 0) {
                    perror("batch: failed to open source file");
                    return 1;
                }
                break;
            case 'q':
                /* Queues are the letters; anything else names no queue
                 * directory ("/", or "=" for the jobs being run). */
                if (strlen(optarg) != 1 || !isalpha((unsigned char)optarg[0])) {
                    fprintf(stderr, "batch: queue must be a single letter\n");
                    return 1;
                }
                req.queue = optarg[0];
                break;
            default:
                fprintf(stderr, "usage: batch [-m] [-f file] [-q queue] [timespec...]\n");
                return 1;
        }
    }

    if (optind < argc) {
        /* User provided a timespec. Activate BSD mode. */
        bsd_mode_activated = 1;
        
        // Phase 6.2: timespec parsing should be done via a libatparse routine.
        if (at_parse_time(argc, argv, optind, &req.deferred_time) != 0) {
            fprintf(stderr, "batch: invalid time specification or poorly formatted extension.\n");
            return 1;
        }
    }

    /* Phase 6.3: Apply BSD profile openbsd-defaults if activated */
    if (bsd_mode_activated) {
        req.profile = AT_PROFILE_BSD_EXTENDED;
        if (req.queue == 'b' && !strchr(argv[0], 'a')) { // if queue wasn't changed
            req.queue = 'E'; // OpenBSD default
        }
        if (req.mail_mode == AT_MAIL_ALWAYS && getopt(argc, argv, "m") != 'm') {
            // Did not explicitly request mail? OpenBSD doesn't mail.
            req.mail_mode = AT_MAIL_NEVER;
        }
    }

    /* Snapshot identity and environment */
    req.umask_snapshot = umask(0);
    umask(req.umask_snapshot);
    req.submitter_uid = getuid();
    req.submitter_gid = getgid();

    /* REQ-BATCH-009 to 012: Check ACL */
    if (at_acl_check_access(req.submitter_uid) != 0) {
        fprintf(stderr, "batch: You do not have permission to submit batch jobs.\n");
        return 1;
    }

    struct batch_submit_result res;
    memset(&res, 0, sizeof(res));

    /* Submit job.  Back to root for the spool; if batch is not setuid this
     * fails, and so does the spool write, with its own message. */
    (void)seteuid(0);
    if (at_spool_create_job(&req, &res) != 0) {
        fprintf(stderr, "batch: Failed to submit job: %s\n", res.diagnostics);
        return res.status_code > 0 ? res.status_code : 1;
    }

    /* REQ-BATCH-003: Write job report to stderr */
    fprintf(stderr, "job %s at %s\n", res.job_id, res.scheduled_display_time);

    return 0; // Return 0 on success
}
