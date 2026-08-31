#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Our public errno.h exposes the host-matching accessor by default.
 * Test the BSD/NetBSD compatibility entry points explicitly as exported by
 * lib/c/src/sys.c.
 */
int *__error(void);
int *__errno(void);

static int64_t mock_result;
static int mock_num;
static uintptr_t mock_args[6];

static void reset_mock(int64_t result) {
    mock_result = result;
    mock_num = -1;
    for (int i = 0; i < 6; i++) {
        mock_args[i] = 0;
    }
    errno = 0;
}

int64_t _syscall0(int num) {
    mock_num = num;
    return mock_result;
}

int64_t _syscall1(int num, uintptr_t a1) {
    mock_num = num;
    mock_args[0] = a1;
    return mock_result;
}

int64_t _syscall2(int num, uintptr_t a1, uintptr_t a2) {
    mock_num = num;
    mock_args[0] = a1;
    mock_args[1] = a2;
    return mock_result;
}

int64_t _syscall3(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3) {
    mock_num = num;
    mock_args[0] = a1;
    mock_args[1] = a2;
    mock_args[2] = a3;
    return mock_result;
}

int64_t _syscall4(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    mock_num = num;
    mock_args[0] = a1;
    mock_args[1] = a2;
    mock_args[2] = a3;
    mock_args[3] = a4;
    return mock_result;
}

/* lseek() uses the 64-bit-return variant; it arrived after this mock set was
 * written and nothing stubbed it, so the link failed on it.  Same contract as
 * _syscall4, it just returns the full int64_t rather than a truncated one. */
int64_t _syscall4_ll(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4) {
    mock_num = num;
    mock_args[0] = a1;
    mock_args[1] = a2;
    mock_args[2] = a3;
    mock_args[3] = a4;
    return mock_result;
}

int64_t _syscall5(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5) {
    mock_num = num;
    mock_args[0] = a1;
    mock_args[1] = a2;
    mock_args[2] = a3;
    mock_args[3] = a4;
    mock_args[4] = a5;
    return mock_result;
}

int64_t _syscall6(int num, uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6) {
    mock_num = num;
    mock_args[0] = a1;
    mock_args[1] = a2;
    mock_args[2] = a3;
    mock_args[3] = a4;
    mock_args[4] = a5;
    mock_args[5] = a6;
    return mock_result;
}

static void assert_true(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        exit(1);
    }
}

static void test_errno_accessors_share_storage(void) {
    int *loc = __errno_location();
    int *bsd = __error();
    int *nbsd = __errno();

    assert_true(loc == bsd, "__errno_location and __error share storage");
    assert_true(loc == nbsd, "__errno_location and __errno share storage");

    *loc = EIO;
    assert_true(errno == EIO, "errno macro sees __errno_location storage");

    *bsd = EINVAL;
    assert_true(errno == EINVAL, "__error writes visible errno");

    *nbsd = ENOENT;
    assert_true(errno == ENOENT, "__errno writes visible errno");
}

static void test_open_sets_errno_from_negative_syscall(void) {
    reset_mock(-ENOENT);
    assert_true(open("/missing", 0) == -1, "open returns -1 on negative syscall");
    assert_true(errno == ENOENT, "open maps negative syscall result to errno");
}

static void test_read_sets_errno_from_negative_syscall(void) {
    char ch;

    reset_mock(-EINTR);
    assert_true(read(3, &ch, 1) == -1, "read returns -1 on negative syscall");
    assert_true(errno == EINTR, "read maps negative syscall result to errno");
}

int main(void) {
    test_errno_accessors_share_storage();
    test_open_sets_errno_from_negative_syscall();
    test_read_sets_errno_from_negative_syscall();
    puts("test_errno: PASS");
    return 0;
}
