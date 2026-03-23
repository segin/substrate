#ifndef _AT_H_
#define _AT_H_

#include <sys/types.h>
#include <time.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compatibility profiles locking the behavior matrix */
typedef enum {
    AT_PROFILE_POSIX_STRICT,
    AT_PROFILE_BSD_EXTENDED,
    AT_PROFILE_GNU_EXTENDED
} at_profile_t;

/* Mail delivery policies */
typedef enum {
    AT_MAIL_NEVER,      /* OpenBSD default */
    AT_MAIL_ON_OUTPUT,  /* GNU/NetBSD default (implied) */
    AT_MAIL_ALWAYS      /* POSIX strict default (-m) */
} at_mail_mode_t;

/* Shell execution policies */
typedef enum {
    AT_SHELL_BOURNE,    /* Force /bin/sh */
    AT_SHELL_USER,      /* Use $SHELL from submission environment */
    AT_SHELL_LOGIN      /* Look up login shell from passwd */
} at_shell_policy_t;

/* Phase 2.1: batch_submit_request */
struct batch_submit_request {
    /* Source of commands */
    int     input_fd;           /* FD containing job commands (e.g., STDIN_FILENO or opened file) */
    const char *source_file;    /* Optional filename for diagnostics */

    /* Job parameters */
    char    queue;              /* 'a'-'z', 'A'-'Z', '=' (GNU) */
    time_t  deferred_time;      /* Scheduled execution time, 0 = immediate/batch */
    
    /* Execution & Output policies */
    at_mail_mode_t      mail_mode;
    at_shell_policy_t   shell_policy;
    at_profile_t        profile;

    /* Snapshot of submission context */
    char    *cwd_snapshot;
    char    **env_snapshot;     /* Null-terminated array of allowed environment strings */
    mode_t  umask_snapshot;

    /* Submitter identity */
    uid_t   submitter_uid;
    gid_t   submitter_gid;
};

/* Phase 2.2: batch_submit_result */
struct batch_submit_result {
    int     status_code;        /* 0 on success, >0 on error */
    char    job_id[32];         /* Alphanumeric + periods */
    char    scheduled_display_time[64]; /* Timezone-adjusted string for stderr */
    char    effective_queue;    
    at_profile_t profile;

    /* Diagnostics for error reporting */
    char    diagnostics[256];
};

/*
 * Phase 2.3: Narrow privileged interface
 * These functions encapsulate operations requiring elevated privileges
 * (such as writing to the spool directory, reading ACL files, etc.)
 */

/* Checks if the given user is allowed to submit jobs via at.allow/at.deny */
int at_acl_check_access(uid_t submitter);

/* Atomically stages and commits a job to the spool directory. */
int at_spool_create_job(const struct batch_submit_request *req, struct batch_submit_result *out_res);

/* Executes a spooled job, managing shell, environment, and setsid per Phase 4. */
int at_exec_run_job(const struct batch_submit_request *req, const char *job_file_path);

/* Parses human readable time strings into time_t (Phase 6.2). */
int at_parse_time(int argc, char *argv[], int optind, time_t *out_time);

#ifdef __cplusplus
}
#endif

#endif /* _AT_H_ */
