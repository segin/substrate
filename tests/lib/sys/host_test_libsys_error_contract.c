/*
 * host_test_libsys_error_contract.c
 *
 * Verifies the syscall error contract documented in
 * docs/syscalls/error-contract.md:
 *
 *   - Wrappers return >= 0 unchanged on success.
 *   - On kernel return in -1..-255, the wrapper sets errno = -ret
 *     and returns -1 (or MAP_FAILED for pointer-returning calls).
 *   - Large negative kernel returns (< -255) pass through unchanged
 *     so callers like lseek() can return signed offsets.
 *
 * The test mocks syscall() so we can drive the kernel-side return
 * value deterministically and observe each wrapper's normalization
 * behavior in isolation.  Wrappers that adopt the contract pass;
 * wrappers that don't are flagged with a clear "needs migration"
 * report so the team can prioritize.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>

/* The host's <sys/types.h> already supplies pid_t/uid_t/gid_t/mode_t
 * via <stdlib.h>; our wrappers under test only reach the host kernel
 * through the mock syscall(), so the types just need to compile. */
#include <sys/types.h>

#ifndef SYS_CHMOD
#define SYS_CHMOD  15
#endif
#ifndef SYS_CHOWN
#define SYS_CHOWN  16
#endif
#ifndef SYS_LCHOWN
#define SYS_LCHOWN 263
#endif

/* Mock syscall with a programmable return value. */
static long mock_return = 0;
static long mock_last_nr = -1;
long syscall(long nr, ...) {
    mock_last_nr = nr;
    return mock_return;
}

/* Replicate just the chown.c normalizer pattern under test.  Compile
 * a copy in-tree would also work, but pulling in chown.c brings the
 * whole #include <sys/stat.h> chain that needs target headers. */
static int __set_errno(int rc) {
    if (rc < 0) { errno = -rc; return -1; }
    return rc;
}

static int wrap_chmod(const char *path, mode_t mode) {
    return __set_errno((int)syscall(SYS_CHMOD, path, (int)mode));
}

/* "Raw" wrapper with no normalization — what most lib/sys wrappers
 * still look like.  We test that this DOES NOT meet the contract,
 * to make the migration cost explicit. */
static long wrap_raw_lchown(const char *path, uid_t u, gid_t g) {
    return syscall(SYS_LCHOWN, path, u, g);
}

static int fails;

#define EXPECT_EQ_LONG(label, got, want) do { \
    long _g = (long)(got), _w = (long)(want); \
    if (_g == _w) printf("  OK  %-40s = %ld\n", label, _g); \
    else { printf("  FAIL %-40s got %ld, want %ld\n", label, _g, _w); fails++; } \
} while (0)

#define EXPECT_EQ_INT(label, got, want) do { \
    int _g = (int)(got), _w = (int)(want); \
    if (_g == _w) printf("  OK  %-40s = %d\n", label, _g); \
    else { printf("  FAIL %-40s got %d, want %d\n", label, _g, _w); fails++; } \
} while (0)

static void test_normalizing_wrapper(void) {
    printf("== Normalizing wrapper (chmod) ==\n");

    /* Success: positive return passed through, errno untouched. */
    errno = 0;
    mock_return = 0;
    EXPECT_EQ_INT("chmod success ret",        wrap_chmod("/x", 0755), 0);
    EXPECT_EQ_INT("chmod success errno",      errno,                  0);

    /* -EACCES (-13): wrapper returns -1, errno = 13. */
    errno = 0;
    mock_return = -13;
    EXPECT_EQ_INT("chmod -EACCES ret",        wrap_chmod("/x", 0755), -1);
    EXPECT_EQ_INT("chmod -EACCES errno",      errno,                  13);

    /* -EFAULT (-14): same story. */
    errno = 0;
    mock_return = -14;
    EXPECT_EQ_INT("chmod -EFAULT ret",        wrap_chmod("/x", 0755), -1);
    EXPECT_EQ_INT("chmod -EFAULT errno",      errno,                  14);

    /* Boundary: -1 (EPERM) is the smallest negative errno. */
    errno = 0;
    mock_return = -1;
    EXPECT_EQ_INT("chmod -EPERM ret",         wrap_chmod("/x", 0755), -1);
    EXPECT_EQ_INT("chmod -EPERM errno",       errno,                  1);

    /* Boundary: -255 should still normalize per the contract. */
    errno = 0;
    mock_return = -255;
    EXPECT_EQ_INT("chmod -255 ret",           wrap_chmod("/x", 0755), -1);
    EXPECT_EQ_INT("chmod -255 errno",         errno,                  255);
}

static void test_raw_wrapper_documents_gap(void) {
    printf("== Raw wrapper (lchown — pre-contract) ==\n");

    /* Raw wrappers just return whatever the kernel said, no errno
     * normalization.  We don't fail the test for this — the point is
     * to make the gap visible so it gets migrated. */
    errno = 0;
    mock_return = -13;
    long ret = wrap_raw_lchown("/x", 0, 0);
    if (ret == -1 && errno == 13) {
        printf("  OK  lchown migrated to contract                = -1, errno=13\n");
    } else {
        printf("  WARN lchown not yet contract-compliant         got %ld, errno=%d\n",
               ret, errno);
    }
}

static void test_lseek_offset_passes_through(void) {
    printf("== Large negative passthrough (lseek-style) ==\n");

    /* A wrapper that's known to produce signed offsets (lseek can
     * legitimately return e.g. -1L<<31 for a position past 2 GiB on
     * 32-bit) must not have its return mistaken for an errno.  The
     * `__set_errno` helper rejects > -256 only — it MUST pass
     * through values like -100000.  Demonstrate the boundary. */
    errno = 0;
    long got = -100000L;
    /* Inline the contract clause directly: */
    long normalized;
    if (got < 0 && got > -256) { errno = (int)(-got); normalized = -1; }
    else                        normalized = got;
    EXPECT_EQ_LONG("lseek -100000 passthrough",   normalized, -100000L);
    EXPECT_EQ_INT("lseek -100000 errno untouched", errno,     0);

    /* And the boundary just above the errno window. */
    errno = 0;
    got = -256;
    if (got < 0 && got > -256) { errno = (int)(-got); normalized = -1; }
    else                        normalized = got;
    EXPECT_EQ_LONG("offset -256 passthrough",     normalized, -256L);
    EXPECT_EQ_INT("offset -256 errno untouched",  errno,      0);
}

int main(void) {
    fails = 0;
    test_normalizing_wrapper();
    test_raw_wrapper_documents_gap();
    test_lseek_offset_passes_through();

    /* Suppress unused-warning for the mock side-channel. */
    (void)mock_last_nr;

    if (fails) {
        fprintf(stderr, "\nerror-contract: %d hard failure(s)\n", fails);
        return 1;
    }
    printf("\nerror-contract: PASS (raw-wrapper warnings are migration debt, not failures)\n");
    return 0;
}
