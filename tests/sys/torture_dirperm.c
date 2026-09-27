/*
 * torture_dirperm.c -- changing a directory's entries needs permission on it.
 *
 * The kernel checked no permission on the parent directory for any call
 * that adds, removes or renames an entry, so an ordinary user could create,
 * delete or replace files in root's /etc -- /etc/passwd included.  POSIX
 * requires write and search permission on the directory, and in a sticky
 * directory the caller must also own the directory or the entry.
 *
 *   denied    uid 1000 in a root-owned 0755 directory: open(O_CREAT), mkdir,
 *             mknod, symlink, link, unlink, rmdir, rename (either end) all
 *             fail EACCES and change nothing;
 *   allowed   the same calls succeed in a directory uid 1000 owns;
 *   sticky    in a 1777 directory uid 1000 cannot unlink, rmdir or rename
 *             away root's entries (EPERM), but can create and remove its own;
 *   root      root still does all of it in the 0755 directory.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define BASE   "/tmp/dirperm"
#define LOCKED BASE "/locked"          /* root 0755 */
#define MINE   BASE "/mine"            /* uid 1000 0755 */
#define STICKY BASE "/sticky"          /* root 1777 */
#define USER   1000

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static int exists(const char *p) {
    struct stat st;
    return lstat(p, &st) == 0;
}

static void touch(const char *p) {
    int fd = open(p, O_WRONLY | O_CREAT, 0644);
    if (fd >= 0)
        close(fd);
}

/* Run `fn` in a child with uid/gid 1000; returns its exit status. */
static int as_user(int (*fn)(void)) {
    pid_t pid = fork();
    if (pid == 0) {
        if (setgid(USER) != 0 || setuid(USER) != 0)
            _exit(99);
        _exit(fn());
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 98;
}

/* Each call must fail with `want`; returns how many did not. */
static int expect_err(const char *what, int rc, int want) {
    if (rc == -1 && errno == want)
        return 0;
    printf("    %s: rc %d errno %d (%s), want %s\n", what, rc,
           rc == -1 ? errno : 0, rc == -1 ? strerror(errno) : "-",
           want == EACCES ? "EACCES" : "EPERM");
    return 1;
}

static int expect_ok(const char *what, int rc) {
    if (rc == 0)
        return 0;
    printf("    %s: rc %d errno %d (%s)\n", what, rc, errno, strerror(errno));
    return 1;
}

static int user_denied(void) {
    int bad = 0;
    bad += expect_err("open O_CREAT", open(LOCKED "/new", O_WRONLY | O_CREAT, 0644) < 0 ? -1 : 0, EACCES);
    bad += expect_err("mkdir", mkdir(LOCKED "/newdir", 0755), EACCES);
    bad += expect_err("mknod fifo", mknod(LOCKED "/fifo", S_IFIFO | 0644, 0), EACCES);
    bad += expect_err("symlink", symlink("x", LOCKED "/sl"), EACCES);
    bad += expect_err("link in", link(MINE "/own", LOCKED "/hl"), EACCES);
    bad += expect_err("unlink", unlink(LOCKED "/victim"), EACCES);
    bad += expect_err("rmdir", rmdir(LOCKED "/vdir"), EACCES);
    bad += expect_err("rename out", rename(LOCKED "/victim", MINE "/stolen"), EACCES);
    bad += expect_err("rename in", rename(MINE "/own", LOCKED "/victim"), EACCES);
    bad += expect_err("rename within", rename(LOCKED "/victim", LOCKED "/renamed"), EACCES);
    return bad;
}

static int user_allowed(void) {
    int bad = 0;
    int fd = open(MINE "/new", O_WRONLY | O_CREAT, 0644);
    bad += expect_ok("open O_CREAT", fd < 0 ? -1 : 0);
    if (fd >= 0)
        close(fd);
    bad += expect_ok("mkdir", mkdir(MINE "/newdir", 0755));
    bad += expect_ok("mknod fifo", mknod(MINE "/fifo", S_IFIFO | 0644, 0));
    bad += expect_ok("symlink", symlink("x", MINE "/sl"));
    bad += expect_ok("link", link(MINE "/new", MINE "/hl"));
    bad += expect_ok("rename", rename(MINE "/hl", MINE "/hl2"));
    bad += expect_ok("unlink", unlink(MINE "/hl2"));
    bad += expect_ok("rmdir", rmdir(MINE "/newdir"));
    return bad;
}

static int user_sticky(void) {
    int bad = 0;
    bad += expect_err("unlink root's file", unlink(STICKY "/rootfile"), EPERM);
    bad += expect_err("rmdir root's dir", rmdir(STICKY "/rootdir"), EPERM);
    bad += expect_err("rename root's file", rename(STICKY "/rootfile", STICKY "/taken"), EPERM);
    int fd = open(STICKY "/userfile", O_WRONLY | O_CREAT, 0644);
    bad += expect_ok("create own file", fd < 0 ? -1 : 0);
    if (fd >= 0)
        close(fd);
    bad += expect_err("replace root's file", rename(STICKY "/userfile", STICKY "/rootfile"), EPERM);
    bad += expect_ok("unlink own file", unlink(STICKY "/userfile"));
    return bad;
}

int main(void) {
    printf("torture_dirperm\n");
    mkdir("/tmp", 01777);
    mkdir(BASE, 0755);
    mkdir(LOCKED, 0755);
    chmod(LOCKED, 0755);
    mkdir(MINE, 0755);
    chown(MINE, USER, USER);
    mkdir(STICKY, 0755);
    chmod(STICKY, 01777);
    touch(LOCKED "/victim");
    mkdir(LOCKED "/vdir", 0755);
    touch(MINE "/own");
    chown(MINE "/own", USER, USER);
    touch(STICKY "/rootfile");
    chmod(STICKY "/rootfile", 0666);
    mkdir(STICKY "/rootdir", 0777);

    check("denied: user cannot change a root 0755 directory",
          as_user(user_denied) == 0, "a call was allowed (see above)");
    check("denied: nothing changed",
          !exists(LOCKED "/new") && !exists(LOCKED "/newdir") &&
          !exists(LOCKED "/fifo") && !exists(LOCKED "/sl") &&
          !exists(LOCKED "/hl") && !exists(LOCKED "/renamed") &&
          !exists(MINE "/stolen") && exists(LOCKED "/victim") &&
          exists(LOCKED "/vdir") && exists(MINE "/own"),
          "the directory changed");
    check("allowed: user changes its own directory",
          as_user(user_allowed) == 0, "a call failed (see above)");
    check("sticky: user cannot remove root's entries",
          as_user(user_sticky) == 0, "see above");
    check("sticky: root's entries remain",
          exists(STICKY "/rootfile") && exists(STICKY "/rootdir"),
          "an entry was removed");

    int bad = 0;
    int fd = open(LOCKED "/rootnew", O_WRONLY | O_CREAT, 0644);
    bad += expect_ok("root open O_CREAT", fd < 0 ? -1 : 0);
    if (fd >= 0)
        close(fd);
    bad += expect_ok("root rename", rename(LOCKED "/rootnew", LOCKED "/rootnew2"));
    bad += expect_ok("root unlink", unlink(LOCKED "/rootnew2"));
    bad += expect_ok("root rmdir", rmdir(LOCKED "/vdir"));
    bad += expect_ok("root sticky unlink", unlink(STICKY "/rootfile"));
    check("root: unaffected", bad == 0, "a root call failed");

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
