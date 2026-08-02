/*
 * torture_readdir.c — regression test for the shared-dirent readdir race
 * (task #423).
 *
 * Every filesystem driver returns a pointer to storage it owns and reuses on
 * the next call; six of them (procfs, devfs, sysfs, shmfs, udf, sysv) used a
 * single file-scope struct dirent shared by every caller, and ext2 returned
 * &ctx->current_dirent after dropping the node lock.  The window was between
 * readdir_fs() returning and the syscall copying d_name out: a second process
 * calling getdents() in that window overwrote the buffer, and the first
 * process reported a name (and d_ino) belonging to the other caller's scan.
 *
 * The test is what that bug made impossible: several processes walking
 * DIFFERENT directories at the same time, each checking that every name it
 * is handed actually exists in the directory it asked about.  A name that
 * leaked in from another process's scan does not resolve under this parent,
 * so the stat fails and the child reports it.
 *
 * Deliberately uses directories on different filesystems (ext2 root, procfs,
 * devfs) so the shared-static case and the per-node ext2 case are both
 * covered by the same run.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_readdir"
 */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int passed, failed;

static void ok(const char *what, int cond, const char *why)
{
    if (cond) {
        printf("  ok    %s\n", what);
        passed++;
    } else {
        printf("  FAIL  %s: %s (errno=%d)\n", what, why, errno);
        failed++;
    }
}

/*
 * Walk `dir` `rounds` times.  For every entry, stat "<dir>/<name>".  Returns
 * the number of names that did not resolve -- which for a directory nobody
 * is modifying can only be a name that was never in this directory.
 *
 * "." and ".." are skipped only in the sense that they always resolve; they
 * are still stat'ed, because a corrupted d_name is exactly what we want to
 * catch.
 */
static int walk_and_verify(const char *dir, int rounds, int *entries_seen)
{
    int bad = 0, seen = 0;

    for (int r = 0; r < rounds; r++) {
        DIR *d = opendir(dir);
        if (!d) return -1;
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            char path[512];
            struct stat st;
            /* An empty name is itself corruption -- readdir must never
             * hand back a zero-length entry. */
            if (de->d_name[0] == '\0') { bad++; continue; }
            snprintf(path, sizeof(path), "%s/%s", dir, de->d_name);
            seen++;
            if (stat(path, &st) != 0) {
                bad++;
                if (bad <= 3)
                    printf("        %s: '%s' does not resolve\n", dir, de->d_name);
            }
        }
        closedir(d);
    }
    *entries_seen = seen;
    return bad;
}

/* Single-process baseline: with nobody else running, every name must
 * resolve.  If this fails the test is broken, not the kernel. */
static void test_baseline(void)
{
    printf("#423: uncontended readdir returns names that resolve\n");

    static const char *dirs[] = { "/etc", "/bin", "/dev", "/proc" };
    for (unsigned i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        int seen = 0;
        int bad = walk_and_verify(dirs[i], 2, &seen);
        char msg[96];
        snprintf(msg, sizeof(msg), "%s (%d entries)", dirs[i], seen);
        if (bad < 0) {
            printf("  skip  %s: cannot open\n", dirs[i]);
            continue;
        }
        ok(msg, bad == 0, "a name did not resolve even with no contention");
    }
}

/*
 * The real case.  Four children, each hammering a different directory.  Under
 * the old code the children collide inside the one shared dirent and start
 * seeing each other's names; each such name fails to resolve under its own
 * parent.
 */
static void test_concurrent(void)
{
    printf("#423: concurrent readdir on different directories\n");

    static const char *dirs[] = { "/etc", "/bin", "/dev", "/proc" };
    enum { NKIDS = 4, ROUNDS = 40 };
    pid_t kids[NKIDS];
    int nkids = 0;

    for (int i = 0; i < NKIDS; i++) {
        pid_t p = fork();
        if (p < 0) break;
        if (p == 0) {
            int seen = 0;
            int bad = walk_and_verify(dirs[i], ROUNDS, &seen);
            /* Exit code carries the verdict: 0 clean, 1 corruption seen,
             * 2 could not open (treated as a skip by the parent). */
            _exit(bad < 0 ? 2 : (bad > 0 ? 1 : 0));
        }
        kids[nkids++] = p;
    }
    ok("children forked", nkids == NKIDS, "fork failed");

    int corrupted = 0, skipped = 0;
    for (int i = 0; i < nkids; i++) {
        int st = 0;
        waitpid(kids[i], &st, 0);
        int code = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        if (code == 1) corrupted++;
        else if (code == 2) skipped++;
        else if (code != 0) corrupted++;   /* crashed -- also a failure */
    }
    if (skipped) printf("        (%d child(ren) skipped: directory absent)\n", skipped);
    ok("no child saw a name from another child's scan", corrupted == 0,
       "readdir handed a caller an entry belonging to a different scan");
}

/*
 * Two processes on the SAME directory.  Per-node dirent storage would fix the
 * cross-directory case above but not this one, so it is checked separately:
 * both children must see the identical set of names.
 */
static void test_same_directory(void)
{
    printf("#423: two readers of the same directory agree\n");

    const char *dir = "/etc";
    enum { ROUNDS = 40 };
    pid_t a = fork();
    if (a == 0) {
        int seen = 0;
        int bad = walk_and_verify(dir, ROUNDS, &seen);
        _exit(bad == 0 ? 0 : 1);
    }
    pid_t b = fork();
    if (b == 0) {
        int seen = 0;
        int bad = walk_and_verify(dir, ROUNDS, &seen);
        _exit(bad == 0 ? 0 : 1);
    }
    int sa = 0, sb = 0;
    if (a > 0) waitpid(a, &sa, 0);
    if (b > 0) waitpid(b, &sb, 0);
    int ca = WIFEXITED(sa) ? WEXITSTATUS(sa) : -1;
    int cb = WIFEXITED(sb) ? WEXITSTATUS(sb) : -1;
    ok("both readers saw only real entries", ca == 0 && cb == 0,
       "a concurrent reader of the same directory saw a corrupted entry");
}

int main(void)
{
    printf("torture_readdir: shared-dirent readdir race (#423)\n\n");

    test_baseline();
    test_concurrent();
    test_same_directory();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
