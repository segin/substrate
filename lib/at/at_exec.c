#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <grp.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <at.h>

#define AT_SPOOL_OUT "/var/spool/at/spool"

/* Phase 4.1: runner logic */
static pid_t
at_setsid(void) {
    return (pid_t)syscall(SYS_SETSID);
}

/*
 * Become the job's owner: its supplementary groups first (root's own must
 * not leak into the job), then its group, then its user.  Every step is
 * checked, and so is the result -- a job that cannot shed root's identity
 * must not run at all, since it would otherwise run as root.  Returns 0,
 * or -1 if the job must be abandoned.
 */
static int drop_to_owner(uid_t uid, gid_t gid) {
    struct passwd *pw = getpwuid(uid);
    int rc = pw ? initgroups(pw->pw_name, gid) : setgroups(1, &gid);
    if (rc != 0)
        return -1;
    if (setgid(gid) != 0 || setuid(uid) != 0)
        return -1;
    if (getuid() != uid || geteuid() != uid ||
        getgid() != gid || getegid() != gid)
        return -1;
    /* Having dropped from root, getting it back must be impossible. */
    if (uid != 0 && setuid(0) == 0)
        return -1;
    return 0;
}

static int setup_job_environment(const struct batch_submit_request *req) {
    /* Phase 4.2: Restore cwd, umask, retained environment */
    if (req->cwd_snapshot) {
        chdir(req->cwd_snapshot);
    }

    umask(req->umask_snapshot);

    return drop_to_owner(req->submitter_uid, req->submitter_gid);
}

static int ensure_dir(const char *path, mode_t mode) {
    if (mkdir(path, mode) == 0 || errno == EEXIST) return 0;
    return -1;
}

int at_exec_run_job(const struct batch_submit_request *req, const char *job_file_path) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        /* Child process - Phase 4.1: run in separate process group without ctty */
        at_setsid();

        /* Phase 4.3: Stdout/stderr capture.
         * For now, pipe stdout and stderr to a file for the mailer to pick up.
         * The spool is root's and mode 0700, so the file is created while
         * still root and handed to the job's owner; opened after the
         * identity switch it could not be created for anyone but root.
         */
        char out_path[160];
        const char *job_name = job_file_path;
        const char *slash = strrchr(job_file_path, '/');
        if (slash && slash[1] != '\0') job_name = slash + 1;

        ensure_dir("/var/spool/at", 0700);
        ensure_dir(AT_SPOOL_OUT, 0700);
        snprintf(out_path, sizeof(out_path), "%s/%s.out", AT_SPOOL_OUT, job_name);

        int fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW, 0600);
        if (fd >= 0 &&
            fchown(fd, req->submitter_uid, req->submitter_gid) != 0) {
            close(fd);
            fd = -1;
        }

        if (setup_job_environment(req) != 0) {
            /* Could not become the job's owner: never run it as root. */
            _exit(126);
        }

        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }

        /* Determine shell policy */
        const char *shell = "/bin/sh";
        if (req->shell_policy == AT_SHELL_USER) {
            /* Fallback to /bin/sh if policy specifies user but no env logic available yet */
            shell = "/bin/sh";
        }

        /* Execute job script */
        execl(shell, shell, job_file_path, (char*)NULL);
        
        /* If execl fails */
        exit(127);
    }

    /* Parent process: depending on daemon logic, could wait or return */
    int status;
    waitpid(pid, &status, 0);

    /* Phase 4.3 Pipeline to mailer:
     * Check output file. If there's content and mail_mode is ON_OUTPUT or ALWAYS,
     * execute `mail` command. This is stubbed for now.
     */
     
    if (!WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}
