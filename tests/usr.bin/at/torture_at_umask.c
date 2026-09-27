/*
 * torture_at_umask.c -- at jobs run with the submitter's umask.
 *
 * atd ran every job with umask 0, so files a job created were world-
 * writable whatever the submitter's umask.  This test starts atd, then:
 *
 *   submitter  as uid 1000 with umask 077, queues "touch FILE" with at and
 *              expects FILE to be created 0600;
 *   default    places a job without a recorded umask straight in the
 *              spool and expects the 022 default: 0644.
 *
 * Needs /usr/bin/at setuid root.  Runs as init (root); prints "Result:".
 */
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define USER 1000
#define F1   "/tmp/at-umask-submitter"
#define F2   "/tmp/at-umask-default"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

/* Wait up to 20 s for `path`; returns its permission bits or -1. */
static int wait_mode(const char *path) {
    struct stat st;
    for (int i = 0; i < 80; i++) {
        if (stat(path, &st) == 0)
            return (int)(st.st_mode & 07777);
        usleep(250000);
    }
    return -1;
}

int main(void) {
    char why[64];
    printf("torture_at_umask\n");
    mkdir("/tmp", 01777);
    unlink(F1);
    unlink(F2);
    close(open("/etc/at.deny", O_WRONLY | O_CREAT | O_TRUNC, 0644));

    pid_t atd = fork();
    if (atd == 0) {
        execl("/usr/sbin/atd", "atd", "-f", "-b", "1", (char *)NULL);
        _exit(127);
    }

    int in[2];
    pipe(in);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        close(in[0]);
        close(in[1]);
        if (setgid(USER) != 0 || setuid(USER) != 0)
            _exit(99);
        umask(077);
        execl("/usr/bin/at", "at", "now", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    const char *job = "touch " F1 "\n";
    write(in[1], job, strlen(job));
    close(in[1]);
    waitpid(pid, NULL, 0);

    int mode = wait_mode(F1);
    snprintf(why, sizeof(why), "mode %o, want 600", mode);
    check("submitter: job uses the submitter's umask (077)", mode == 0600, why);

    /* A job file with no recorded umask, owned by the user, due now. */
    mkdir("/var/spool/at", 0700);
    mkdir("/var/spool/at/a", 0700);
    int fd = open("/var/spool/at/a/zumask.0001", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    const char *bare = "touch " F2 "\n";
    write(fd, bare, strlen(bare));
    close(fd);
    chown("/var/spool/at/a/zumask.0001", USER, USER);

    mode = wait_mode(F2);
    snprintf(why, sizeof(why), "mode %o, want 644", mode);
    check("default: a job without a umask gets 022", mode == 0644, why);

    kill(atd, SIGKILL);
    waitpid(atd, NULL, 0);
    unlink(F1);
    unlink(F2);
    unlink("/etc/at.deny");
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
