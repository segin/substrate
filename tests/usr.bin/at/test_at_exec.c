/*
 * test_at_exec.c -- an at job never runs with root's identity.
 *
 * at_exec_run_job() called setgid() and setuid() without checking them and
 * never dropped root's supplementary groups: if either call failed, the
 * job ran as root.  This host test links lib/at/at_exec.c against mocks of
 * the credential calls and of execl(), and checks for each failure that
 * the job is abandoned (child exit 126, execl never reached), and without
 * failures that groups, then group, then user are dropped before the job
 * starts.
 *
 * Build and run (tests/usr.bin/at/Makefile):  make -C tests/usr.bin/at check
 */
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <at.h>

#define OWNER_UID 12345             /* no passwd entry: setgroups() path */
#define OWNER_GID 23456

static const char *fail_step;       /* which credential call fails, or "" */
static char log_path[64];
static uid_t cur_uid;
static gid_t cur_gid;

static void note(const char *what) {
    FILE *f = fopen(log_path, "a");
    if (f) {
        fprintf(f, "%s\n", what);
        fclose(f);
    }
}

/* --- mocks: resolved ahead of libc for calls made from at_exec.o --- */
int setgroups(size_t n, const gid_t *list) {
    (void)list;
    note(n == 1 && list[0] == OWNER_GID ? "setgroups" : "setgroups-wrong");
    return strcmp(fail_step, "setgroups") == 0 ? -1 : 0;
}
int initgroups(const char *user, gid_t g) {
    (void)user; (void)g;
    note("initgroups");
    return strcmp(fail_step, "setgroups") == 0 ? -1 : 0;
}
int setgid(gid_t g) {
    note("setgid");
    if (strcmp(fail_step, "setgid") == 0) return -1;
    cur_gid = g;
    return 0;
}
int setuid(uid_t u) {
    if (u == 0 && cur_uid != 0) {       /* regaining root must fail */
        note("setuid0-refused");
        return -1;
    }
    note("setuid");
    if (strcmp(fail_step, "setuid") == 0) return -1;
    cur_uid = u;
    return 0;
}
uid_t getuid(void)  { return cur_uid; }
uid_t geteuid(void) { return cur_uid; }
gid_t getgid(void)  { return cur_gid; }
gid_t getegid(void) { return cur_gid; }
int execl(const char *path, const char *arg, ...) {
    (void)path; (void)arg;
    char buf[64];
    snprintf(buf, sizeof(buf), "exec uid=%d gid=%d", (int)cur_uid, (int)cur_gid);
    note(buf);
    _exit(0);
}

static int failures;

static void run(const char *step) {
    fail_step = step;
    cur_uid = 0;
    cur_gid = 0;
    unlink(log_path);
    struct batch_submit_request req;
    memset(&req, 0, sizeof(req));
    req.submitter_uid = OWNER_UID;
    req.submitter_gid = OWNER_GID;
    req.umask_snapshot = 022;
    int rc = at_exec_run_job(&req, "/nonexistent/job");

    char got[512] = "";
    FILE *f = fopen(log_path, "r");
    if (f) {
        size_t n = fread(got, 1, sizeof(got) - 1, f);
        got[n] = '\0';
        fclose(f);
    }
    int ran = strstr(got, "exec ") != NULL;
    if (*step) {
        if (ran || rc != 126) {
            printf("FAIL %s fails: job %s (exit %d)\n", step,
                   ran ? "RAN" : "did not run", rc);
            failures++;
        } else {
            printf("ok   %s fails: job abandoned\n", step);
        }
    } else {
        char want[64];
        snprintf(want, sizeof(want), "exec uid=%d gid=%d", OWNER_UID, OWNER_GID);
        const char *g = strstr(got, "setgroups\n");
        const char *gi = strstr(got, "setgid\n");
        const char *u = strstr(got, "setuid\n");
        if (!ran || !strstr(got, want) || !g || !gi || !u || !(g < gi && gi < u)) {
            printf("FAIL normal job: log was:\n%s", got);
            failures++;
        } else {
            printf("ok   normal job drops groups, group, user, then runs\n");
        }
    }
}

int main(void) {
    snprintf(log_path, sizeof(log_path), "/tmp/test_at_exec.%d", (int)getpid());
    run("setgroups");
    run("setgid");
    run("setuid");
    run("");
    unlink(log_path);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
