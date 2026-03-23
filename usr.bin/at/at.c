#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <fcntl.h>

#include <at.h>

int main(int argc, char *argv[]) {
    struct batch_submit_request req;
    memset(&req, 0, sizeof(req));

    /* Default `at` POSIX state */
    req.queue = 'a'; /* 'at' uses 'a', 'batch' uses 'b' */
    req.profile = AT_PROFILE_POSIX_STRICT;
    req.mail_mode = AT_MAIL_ON_OUTPUT; // default for at is usually to mail if output produced
    req.shell_policy = AT_SHELL_BOURNE;
    req.input_fd = STDIN_FILENO;
    req.source_file = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "bmMf:q:u:ovc")) != -1) {
        // Any explicitly given options trigger an extended profile if not POSIX basic
        switch (opt) {
            case 'b': /* GNU extension: run batch from at */
                req.queue = 'b';
                req.profile = AT_PROFILE_GNU_EXTENDED;
                break;
            case 'm':
                req.mail_mode = AT_MAIL_ALWAYS;
                break;
            case 'M': /* GNU extension: never mail */
                req.mail_mode = AT_MAIL_NEVER;
                req.profile = AT_PROFILE_GNU_EXTENDED;
                break;
            case 'f':
                req.source_file = optarg;
                req.input_fd = open(optarg, O_RDONLY);
                if (req.input_fd < 0) {
                    perror("at: failed to open source file");
                    return 1;
                }
                break;
            case 'q':
                if (strlen(optarg) != 1) {
                    fprintf(stderr, "at: queue must be a single character\n");
                    return 1;
                }
                req.queue = optarg[0];
                break;
            case 'u': case 'o': case 'v': case 'c':
                // Subsets of Phase 7.2
                // Mark GNU extended and skip detailed parse for stub
                req.profile = AT_PROFILE_GNU_EXTENDED;
                break;
            default:
                fprintf(stderr, "usage: at [-b] [-m|-M] [-f file] [-q queue] timespec...\n");
                return 1;
        }
    }

    if (optind < argc) {
        if (at_parse_time(argc, argv, optind, &req.deferred_time) != 0) {
            fprintf(stderr, "at: invalid time specification or poorly formatted extension.\n");
            return 1;
        }
    } else if (req.queue != 'b') {
        // `at` demands a timespec, `batch` (or `at -b`) does not.
        fprintf(stderr, "at: timespec required unless in batch mode\n");
        return 1;
    }

    req.submitter_uid = getuid();
    req.submitter_gid = getgid();

    if (at_acl_check_access(req.submitter_uid) != 0) {
        fprintf(stderr, "at: You do not have permission to submit jobs.\n");
        return 1;
    }

    struct batch_submit_result res;
    memset(&res, 0, sizeof(res));

    if (at_spool_create_job(&req, &res) != 0) {
        fprintf(stderr, "at: Failed to submit job: %s\n", res.diagnostics);
        return res.status_code > 0 ? res.status_code : 1;
    }

    /* Print job report */
    if (req.profile == AT_PROFILE_GNU_EXTENDED) {
        // GNU extension output: "warning: commands will be executed using /bin/sh\njob %s at %s\n"
        fprintf(stderr, "warning: commands will be executed using /bin/sh\njob %s at %s\n", 
                res.job_id, res.scheduled_display_time);
    } else {
        fprintf(stderr, "job %s at %s\n", res.job_id, res.scheduled_display_time);
    }

    return 0; // Return 0 on success
}
