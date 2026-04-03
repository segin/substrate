#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <at.h>

extern pid_t setsid(void);

/* Phase 4.1: runner logic */
static int setup_job_environment(const struct batch_submit_request *req) {
    /* Phase 4.2: Restore cwd, umask, retained environment */
    if (req->cwd_snapshot) {
        chdir(req->cwd_snapshot);
    }
    
    if (req->umask_snapshot) {
        umask(req->umask_snapshot);
    }

    /* Set UID/GID */
    setgid(req->submitter_gid);
    setuid(req->submitter_uid);

    return 0;
}

int at_exec_run_job(const struct batch_submit_request *req, const char *job_file_path) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid == 0) {
        /* Child process - Phase 4.1: run in separate process group without ctty */
        setsid();

        setup_job_environment(req);

        /* Phase 4.3: Stdout/stderr capture.
         * For now, pipe stdout and stderr to a file for the mailer to pick up.
         */
        char out_path[128];
        snprintf(out_path, sizeof(out_path), "/var/spool/at/spool/%s.out", "job"); // stubbed ID
        
        int fd = open(out_path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
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
     
    return WEXITSTATUS(status);
}
