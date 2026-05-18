/*
 * torture_fcntl.c — fd duplication + fcntl surface stress.
 *
 * Pure POSIX.  Builds against host libc by default; against
 * substrate's libc + cross toolchain when built with
 * CROSS=/opt/substrate/bin/i386-unknown-substrate-.  MUST PASS on
 * Linux first per CLAUDE.md — anything that fails on the host but
 * the substrate code path "passes" is masking a real bug.
 *
 * Targeted bug: zsh's `exec 7<&0` reports
 *
 *    ./configure:554: cannot duplicate fd 7: operation not permitted
 *
 * zsh's movefd() calls fcntl(fd, F_DUPFD, 10) on fd 7.  fd 7 isn't
 * open.  The expected errno is EBADF; zsh has an explicit
 * `if (errno != EBADF) zerr("...")` so EBADF is silently absorbed
 * as "no existing fd to save."  Substrate's proc_fcntl source
 * literally returns -EBADF on `if (!f) return -EBADF;` for the
 * not-open case, yet zsh's errno comes out 1 (EPERM).  Somewhere
 * between kernel and userspace the error code is being scrambled.
 *
 * The scenarios:
 *
 *   1. dup_open_fd               dup(0) on stdin succeeds, returns
 *                                fd >= 3
 *   2. dup_closed_fd             dup(999) on guaranteed-closed fd
 *                                returns -1 errno=EBADF
 *   3. dup2_open_to_target       dup2(0, 9) succeeds, returns 9
 *   4. dup2_open_to_target_in_use
 *                                dup2(0, target) where target was
 *                                a different open fd; the old one
 *                                is silently closed and returns target
 *   5. dup2_bad_source           dup2(999, 7) returns -1 errno=EBADF
 *   6. dup3_invalid_flags        dup3(0, 9, 0xff) returns -1 errno=EINVAL
 *   7. dup3_same_fd              dup3(0, 0, 0) returns -1 errno=EINVAL
 *   8. fcntl_dupfd_open          fcntl(0, F_DUPFD, 10) returns fd >= 10
 *   9. fcntl_dupfd_closed        fcntl(7, F_DUPFD, 10) where fd 7 is
 *                                closed → returns -1 errno=EBADF.
 *                                THIS IS THE BUG.  Substrate currently
 *                                reports EPERM here.
 *  10. fcntl_dupfd_negative_arg  fcntl(0, F_DUPFD, -1) → -1 EINVAL
 *  11. fcntl_dupfd_huge_arg      fcntl(0, F_DUPFD, 100000) → -1 EINVAL
 *  12. fcntl_getfd_closed        fcntl(7, F_GETFD) on closed fd → EBADF
 *  13. fcntl_getfl_closed        fcntl(7, F_GETFL) on closed fd → EBADF
 *  14. fcntl_setfd_closed        fcntl(7, F_SETFD, FD_CLOEXEC) on
 *                                closed fd → EBADF
 *  15. zsh_exec_redir_pattern    end-to-end recreation of zsh's
 *                                `exec 7<&0` redirection sequence:
 *                                  a. fcntl(7, F_DUPFD, 10)  // expected -1 EBADF
 *                                  b. dup2(0, 7)             // expected 7
 *                                  c. open("/dev/null", RDONLY) → dup2 onto 0
 *                                  d. close the temp open
 *
 * Each scenario prints PASS/FAIL/SKIP with the observed errno and
 * what was expected.  The final line is a one-shot summary;
 * non-zero exit if any FAIL.
 *
 * Invariant: every fcntl/dup return that produces -1 sets errno to
 * a recognised POSIX value.  If you see errno=1 (EPERM) where EBADF
 * or EINVAL is expected, that's the substrate bug we're hunting.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif

static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;

static const char *
errno_name(int e)
{
    switch (e) {
    case 0:       return "0/no-error";
    case EPERM:   return "EPERM";
    case EBADF:   return "EBADF";
    case EINVAL:  return "EINVAL";
    case EMFILE:  return "EMFILE";
    case ENFILE:  return "ENFILE";
    case ENOENT:  return "ENOENT";
    case EAGAIN:  return "EAGAIN";
    case EINTR:   return "EINTR";
    case ENOTTY:  return "ENOTTY";
    case ENOSYS:  return "ENOSYS";
    case ENOTSUP: return "ENOTSUP";
    default:      return "?";
    }
}

#define BANNER(name) \
    do { printf("---- %s ----\n", (name)); } while (0)

#define EXPECT_RET(label, actual, expected) do {                \
    if ((actual) == (expected)) {                               \
        printf("  PASS  %s: returned %d as expected\n",         \
               (label), (actual));                              \
        g_pass++;                                               \
    } else {                                                    \
        printf("  FAIL  %s: returned %d; expected %d\n",        \
               (label), (actual), (expected));                  \
        g_fail++;                                               \
    }                                                           \
} while (0)

#define EXPECT_RET_GE(label, actual, lower) do {                \
    if ((actual) >= (lower)) {                                  \
        printf("  PASS  %s: returned %d (>= %d)\n",             \
               (label), (actual), (lower));                     \
        g_pass++;                                               \
    } else {                                                    \
        printf("  FAIL  %s: returned %d; expected >= %d "       \
               "(errno=%d=%s)\n",                               \
               (label), (actual), (lower),                      \
               errno, errno_name(errno));                       \
        g_fail++;                                               \
    }                                                           \
} while (0)

#define EXPECT_ERR(label, actual, expected_errno) do {          \
    int _err = errno;                                           \
    if ((actual) == -1 && _err == (expected_errno)) {           \
        printf("  PASS  %s: returned -1 errno=%d=%s\n",         \
               (label), _err, errno_name(_err));                \
        g_pass++;                                               \
    } else {                                                    \
        printf("  FAIL  %s: returned %d errno=%d=%s; "          \
               "expected -1 errno=%d=%s\n",                     \
               (label), (actual), _err, errno_name(_err),       \
               (expected_errno), errno_name(expected_errno));   \
        g_fail++;                                               \
    }                                                           \
} while (0)

/* Find a fd we can guarantee is closed.  Walks down from 30 so we
 * don't collide with anything stdio may have opened. */
static int
find_closed_fd(void)
{
    for (int fd = 30; fd >= 4; fd--) {
        errno = 0;
        if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
            return fd;
        }
    }
    return -1;
}

static void
ensure_closed(int fd)
{
    /* close(fd) on already-closed fd is a no-op (returns EBADF).
     * close() return value is ignored — we just want fd cleared. */
    (void)close(fd);
}

static void
test_dup_open_fd(void)
{
    BANNER("dup_open_fd");
    int fd = dup(0);
    EXPECT_RET_GE("dup(0)", fd, 3);
    if (fd >= 0) close(fd);
}

static void
test_dup_closed_fd(void)
{
    BANNER("dup_closed_fd");
    int closed = find_closed_fd();
    if (closed < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = dup(closed);
    EXPECT_ERR("dup(closed_fd)", r, EBADF);
}

static void
test_dup2_open_to_target(void)
{
    BANNER("dup2_open_to_target");
    int target = find_closed_fd();
    if (target < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    int r = dup2(0, target);
    EXPECT_RET("dup2(0,target)", r, target);
    if (r >= 0) close(r);
}

static void
test_dup2_open_to_target_in_use(void)
{
    BANNER("dup2_open_to_target_in_use");
    int t = open("/dev/null", O_RDONLY);
    if (t < 0) { printf("  SKIP  open /dev/null failed\n"); g_skip++; return; }
    int r = dup2(0, t);
    EXPECT_RET("dup2(0,in_use_target)", r, t);
    if (r >= 0) close(r);
}

static void
test_dup2_bad_source(void)
{
    BANNER("dup2_bad_source");
    int closed = find_closed_fd();
    if (closed < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = dup2(closed, 7);
    EXPECT_ERR("dup2(closed,7)", r, EBADF);
}

static void
test_dup3_invalid_flags(void)
{
    BANNER("dup3_invalid_flags");
#if defined(__linux__) || defined(__substrate__)
    int target = find_closed_fd();
    if (target < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = dup3(0, target, 0xff);
    EXPECT_ERR("dup3(0,target,0xff)", r, EINVAL);
#else
    printf("  SKIP  dup3 not POSIX, not on this platform\n");
    g_skip++;
#endif
}

static void
test_dup3_same_fd(void)
{
    BANNER("dup3_same_fd");
#if defined(__linux__) || defined(__substrate__)
    errno = 0;
    int r = dup3(0, 0, 0);
    EXPECT_ERR("dup3(0,0,0)", r, EINVAL);
#else
    printf("  SKIP  dup3 not POSIX, not on this platform\n");
    g_skip++;
#endif
}

static void
test_fcntl_dupfd_open(void)
{
    BANNER("fcntl_dupfd_open");
    errno = 0;
    int r = fcntl(0, F_DUPFD, 10);
    EXPECT_RET_GE("fcntl(0,F_DUPFD,10)", r, 10);
    if (r >= 0) close(r);
}

static void
test_fcntl_dupfd_closed(void)
{
    BANNER("fcntl_dupfd_closed  *** the zsh bug ***");
    int closed = find_closed_fd();
    if (closed < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = fcntl(closed, F_DUPFD, 10);
    EXPECT_ERR("fcntl(closed,F_DUPFD,10)", r, EBADF);
}

static void
test_fcntl_dupfd_negative_arg(void)
{
    BANNER("fcntl_dupfd_negative_arg");
    errno = 0;
    int r = fcntl(0, F_DUPFD, -1);
    EXPECT_ERR("fcntl(0,F_DUPFD,-1)", r, EINVAL);
}

static void
test_fcntl_dupfd_huge_arg(void)
{
    BANNER("fcntl_dupfd_huge_arg");
    /* POSIX: if `arg' exceeds the maximum allowable fd, behavior
     * depends.  Linux returns -1 EINVAL.  glibc on some hosts
     * happily succeeds up to RLIMIT_NOFILE.  Substrate's MAX_FD=32
     * means anything >= 32 must fail with EINVAL or EMFILE.
     * Accept either: -1 with EINVAL/EMFILE/EBADF, OR >= 32. */
    errno = 0;
    int r = fcntl(0, F_DUPFD, 100000);
    if (r == -1 && (errno == EINVAL || errno == EMFILE || errno == EBADF)) {
        printf("  PASS  fcntl(0,F_DUPFD,100000) returned -1 errno=%d=%s\n",
               errno, errno_name(errno));
        g_pass++;
    } else if (r >= 100000) {
        printf("  PASS  fcntl(0,F_DUPFD,100000) returned %d "
               "(host allows large fd numbers)\n", r);
        g_pass++;
        close(r);
    } else {
        printf("  FAIL  fcntl(0,F_DUPFD,100000) returned %d errno=%d=%s\n",
               r, errno, errno_name(errno));
        g_fail++;
    }
}

static void
test_fcntl_getfd_closed(void)
{
    BANNER("fcntl_getfd_closed");
    int closed = find_closed_fd();
    if (closed < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = fcntl(closed, F_GETFD);
    EXPECT_ERR("fcntl(closed,F_GETFD)", r, EBADF);
}

static void
test_fcntl_getfl_closed(void)
{
    BANNER("fcntl_getfl_closed");
    int closed = find_closed_fd();
    if (closed < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = fcntl(closed, F_GETFL);
    EXPECT_ERR("fcntl(closed,F_GETFL)", r, EBADF);
}

static void
test_fcntl_setfd_closed(void)
{
    BANNER("fcntl_setfd_closed");
    int closed = find_closed_fd();
    if (closed < 0) { printf("  SKIP  no closed fd available\n"); g_skip++; return; }
    errno = 0;
    int r = fcntl(closed, F_SETFD, FD_CLOEXEC);
    EXPECT_ERR("fcntl(closed,F_SETFD)", r, EBADF);
}

/*
 * The zsh `exec 7<&0` pattern.  In source form:
 *
 *   exec 7<&0 </dev/null 6>&1
 *
 * What zsh does (from Src/exec.c around the redir loop):
 *
 *   1. movefd(7) — save the existing fd 7 to a slot >= 10.
 *      → fcntl(7, F_DUPFD, 10).  fd 7 is closed, so errno should
 *        be EBADF.  zsh tests `errno != EBADF` and continues
 *        silently when it matches.
 *
 *   2. dup2(0, 7) — install stdin as fd 7.
 *
 *   3. open("/dev/null", O_RDONLY) — get a /dev/null fd, then
 *      dup2 it onto fd 0 to replace stdin.
 *
 * If step 1 reports EPERM instead of EBADF, zsh treats it as a
 * fatal error: "cannot duplicate fd 7: operation not permitted".
 */
static void
test_zsh_exec_redir_pattern(void)
{
    BANNER("zsh_exec_redir_pattern (the actual configure failure)");
    ensure_closed(7);

    errno = 0;
    int save = fcntl(7, F_DUPFD, 10);
    if (save < 0 && errno == EBADF) {
        printf("  PASS  step 1: movefd(7) returned EBADF as zsh expects\n");
        g_pass++;
    } else if (save < 0) {
        printf("  FAIL  step 1: movefd(7) returned %d errno=%d=%s "
               "— zsh would zerr() here\n",
               save, errno, errno_name(errno));
        g_fail++;
    } else {
        printf("  ??    step 1: movefd(7) returned %d (fd 7 was supposedly closed)\n",
               save);
        close(save);
        g_pass++;
    }

    int r = dup2(0, 7);
    EXPECT_RET("step 2: dup2(0,7)", r, 7);

    int nf = open("/dev/null", O_RDONLY);
    EXPECT_RET_GE("step 3: open /dev/null", nf, 0);
    if (nf >= 0) {
        int r2 = dup2(nf, 0);
        EXPECT_RET("step 3b: dup2(/dev/null,0)", r2, 0);
        close(nf);
    }
    close(7);
}

int
main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    printf("==== fcntl / dup / dup2 / dup3 torture ====\n");

    test_dup_open_fd();
    test_dup_closed_fd();
    test_dup2_open_to_target();
    test_dup2_open_to_target_in_use();
    test_dup2_bad_source();
    test_dup3_invalid_flags();
    test_dup3_same_fd();
    test_fcntl_dupfd_open();
    test_fcntl_dupfd_closed();
    test_fcntl_dupfd_negative_arg();
    test_fcntl_dupfd_huge_arg();
    test_fcntl_getfd_closed();
    test_fcntl_getfl_closed();
    test_fcntl_setfd_closed();
    test_zsh_exec_redir_pattern();

    printf("\n==== %d PASS, %d FAIL, %d SKIP ====\n",
           g_pass, g_fail, g_skip);
    return (g_fail == 0) ? 0 : 1;
}
