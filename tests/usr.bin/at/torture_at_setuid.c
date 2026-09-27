/*
 * torture_at_setuid.c -- at and batch are safe to install setuid root.
 *
 * at and batch must be setuid root for ordinary users to queue jobs in the
 * root-owned 0700 spool.  Run that way, they opened a "-f file" with root's
 * privilege and copied it into a job file owned by the user, so any user
 * could read /etc/shadow; and "-q" took any character, including "/" and
 * the running queue "=".  With /bin/at and /bin/batch setuid root and an
 * empty /etc/at.deny (every user allowed), this test checks as uid 1000:
 *
 *   queue     "echo ... | at now" and "| batch" queue a job owned by the user;
 *   fileread  "at -f /etc/shadow now" fails and queues nothing;
 *   queuechar "at -q = now" and "at -q / now" fail and queue nothing.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define USER 1000

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

/* Remove every queued job and count what was there, noting whether each
 * belonged to USER and whether any contained `needle`. */
static int drain_spool(int *all_users, const char *needle, int *has_needle) {
    int n = 0;
    *all_users = 1;
    if (has_needle)
        *has_needle = 0;
    DIR *top = opendir("/var/spool/at");
    struct dirent *q;
    while (top && (q = readdir(top)) != NULL) {
        if (strlen(q->d_name) != 1 || q->d_name[0] == '.')
            continue;
        char qdir[64];
        snprintf(qdir, sizeof(qdir), "/var/spool/at/%s", q->d_name);
        DIR *d = opendir(qdir);
        struct dirent *j;
        while (d && (j = readdir(d)) != NULL) {
            if (j->d_name[0] == '.')
                continue;
            char path[384];
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", qdir, j->d_name);
            if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
                continue;
            if (st.st_uid != USER)
                *all_users = 0;
            if (needle && has_needle) {
                char buf[4096];
                int fd = open(path, O_RDONLY);
                ssize_t r = fd >= 0 ? read(fd, buf, sizeof(buf) - 1) : -1;
                if (fd >= 0)
                    close(fd);
                buf[r > 0 ? r : 0] = '\0';
                if (strstr(buf, needle))
                    *has_needle = 1;
            }
            unlink(path);
            n++;
        }
        if (d)
            closedir(d);
    }
    if (top)
        closedir(top);
    return n;
}

/* Run `prog args...` as USER with `input` on stdin; returns exit status. */
static int run_as_user(const char *input, const char *prog, const char *a1,
                       const char *a2, const char *a3, const char *a4) {
    int in[2];
    pipe(in);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        close(in[0]);
        close(in[1]);
        if (setgid(USER) != 0 || setuid(USER) != 0)
            _exit(99);
        const char *argv[] = { prog, a1, a2, a3, a4, NULL };
        execv(prog, (char **)argv);
        _exit(127);
    }
    close(in[0]);
    write(in[1], input, strlen(input));
    close(in[1]);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

int main(void) {
    int all_users, leaked, n, rc;
    char marker[128] = "";

    printf("torture_at_setuid\n");
    close(open("/etc/at.deny", O_WRONLY | O_CREAT | O_TRUNC, 0644));
    drain_spool(&all_users, NULL, NULL);

    /* A line of /etc/shadow that the job file must never contain. */
    FILE *f = fopen("/etc/shadow", "r");
    if (f) {
        if (fgets(marker, sizeof(marker), f))
            marker[strcspn(marker, "\n")] = '\0';
        fclose(f);
    }

    rc = run_as_user("echo hi\n", "/usr/bin/at", "now", NULL, NULL, NULL);
    n = drain_spool(&all_users, NULL, NULL);
    check("queue: at queues a job owned by the user", rc == 0 && n == 1 && all_users,
          "no job, or not the user's");

    rc = run_as_user("echo hi\n", "/usr/bin/batch", NULL, NULL, NULL, NULL);
    n = drain_spool(&all_users, NULL, NULL);
    check("queue: batch queues a job owned by the user", rc == 0 && n == 1 && all_users,
          "no job, or not the user's");

    rc = run_as_user("", "/usr/bin/at", "-f", "/etc/shadow", "now", NULL);
    n = drain_spool(&all_users, marker, &leaked);
    check("fileread: at -f /etc/shadow refused", rc != 0 && n == 0 && !leaked,
          leaked ? "shadow copied into a job" : "at accepted it");

    rc = run_as_user("", "/usr/bin/batch", "-f", "/etc/shadow", NULL, NULL);
    n = drain_spool(&all_users, marker, &leaked);
    check("fileread: batch -f /etc/shadow refused", rc != 0 && n == 0 && !leaked,
          leaked ? "shadow copied into a job" : "batch accepted it");

    rc = run_as_user("echo hi\n", "/usr/bin/at", "-q", "=", "now", NULL);
    n = drain_spool(&all_users, NULL, NULL);
    check("queuechar: at -q = refused", rc != 0 && n == 0, "job queued in '='");

    rc = run_as_user("echo hi\n", "/usr/bin/at", "-q", "/", "now", NULL);
    n = drain_spool(&all_users, NULL, NULL);
    struct stat st;
    int stray = 0;
    DIR *d = opendir("/var/spool/at");
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        char p[384];
        snprintf(p, sizeof(p), "/var/spool/at/%s", e->d_name);
        if (e->d_name[0] != '.' && lstat(p, &st) == 0 && S_ISREG(st.st_mode)) {
            unlink(p);
            stray = 1;
        }
    }
    if (d)
        closedir(d);
    check("queuechar: at -q / refused", rc != 0 && n == 0 && !stray, "job queued");

    unlink("/etc/at.deny");
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
