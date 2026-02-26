#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

// Mock abort
static jmp_buf abort_jmp;
static int abort_called = 0;

void tested_abort(void) {
    abort_called = 1;
    longjmp(abort_jmp, 1);
}

// Rename standard functions to avoid conflict and mock abort
#define abort tested_abort
// Rename __assert_fail to test it directly
#define __assert_fail tested_assert_fail

// Include the source file directly
#include "../../../lib/c/src/assert.c"

#undef __assert_fail
#undef abort

void test_assert_output(void) {
    printf("Testing __assert_fail output and abort...\n");

    // Create a pipe to capture stderr
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("pipe");
        exit(1);
    }

    int original_stderr = dup(STDERR_FILENO);
    if (original_stderr == -1) {
        perror("dup");
        exit(1);
    }

    fflush(stderr);
    if (dup2(pipefd[1], STDERR_FILENO) == -1) {
        perror("dup2");
        exit(1);
    }
    close(pipefd[1]);

    abort_called = 0;
    if (setjmp(abort_jmp) == 0) {
        // Trigger assert fail
        tested_assert_fail("x > 0", "test.c", 42, "test_func");
    }

    // Restore stderr
    fflush(stderr);
    if (dup2(original_stderr, STDERR_FILENO) == -1) {
        perror("dup2 restore");
        exit(1);
    }
    close(original_stderr);

    if (!abort_called) {
        fprintf(stderr, "FAIL: abort() was not called\n");
        exit(1);
    }

    // Read from pipe
    char buffer[1024] = {0};
    ssize_t count = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);

    if (count < 0) {
        perror("read");
        exit(1);
    }

    const char *expected = "Assertion failed: x > 0 (test.c: test_func: 42)\n";
    if (strstr(buffer, expected) == NULL) {
        fprintf(stderr, "FAIL: Message mismatch.\nExpected: '%s'\nActual: '%s'\n", expected, buffer);
        exit(1);
    }

    printf("PASS: __assert_fail output correct and abort called.\n");
}

int main(void) {
    test_assert_output();
    return 0;
}
