#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <setjmp.h>

// Rename to avoid conflict with system headers
#define pthread_t       my_pthread_t
#define pthread_attr_t  my_pthread_attr_t
#define pthread_mutex_t my_pthread_mutex_t
#define pthread_create  my_pthread_create
#define pthread_join    my_pthread_join
#define pthread_exit    my_pthread_exit
#define pthread_mutex_init my_pthread_mutex_init
#define pthread_mutex_lock my_pthread_mutex_lock
#define pthread_mutex_unlock my_pthread_mutex_unlock
#define pthread_mutex_destroy my_pthread_mutex_destroy

// Mock infrastructure
int mock_malloc_calls = 0;
int mock_free_calls = 0;
int mock_thr_exit_calls = 0;
int mock_exit_calls = 0;
void *last_exit_retval = NULL;
jmp_buf exit_jmp_buf;

// Forward declarations
void *mock_malloc(size_t size);
void mock_free(void *ptr);
long mock_syscall(long num, ...);
void mock_exit(int status);

// Redefine symbols to use mocks
#define malloc mock_malloc
#define free mock_free
#define syscall mock_syscall
#define _exit mock_exit

// Include the source file
#include "../pthread.c"

// Undefine so we can implement mocks using real libc
#undef malloc
#undef free
#undef syscall
#undef _exit

extern void *calloc(size_t nmemb, size_t size);
extern void free(void *ptr);

void *mock_malloc(size_t size) {
    mock_malloc_calls++;
    return calloc(1, size);
}

void mock_free(void *ptr) {
    if (ptr) {
        mock_free_calls++;
        free(ptr);
    }
}

long mock_syscall(long num, ...) {
    va_list args;
    va_start(args, num);
    long arg1 = va_arg(args, long);
    va_end(args);

    if (num == SYS_THR_EXIT) {
        mock_thr_exit_calls++;
        last_exit_retval = (void *)(uintptr_t)arg1;
        return 0;
    }
    return -1;
}

void mock_exit(int status) {
    mock_exit_calls++;
    longjmp(exit_jmp_buf, 1);
}

int my_start_routine_calls = 0;
void *last_start_routine_arg = NULL;
void *start_routine_retval = (void *)0x12345678;

void *my_start_routine(void *arg) {
    my_start_routine_calls++;
    last_start_routine_arg = arg;
    return start_routine_retval;
}

int main() {
    printf("Running host_test_trampoline...\n");

    struct trampoline_args *ta = calloc(1, sizeof(struct trampoline_args));
    ta->start_routine = my_start_routine;
    ta->arg = (void *)(uintptr_t)0xDEADBEEF;

    if (setjmp(exit_jmp_buf) == 0) {
        __pthread_trampoline(ta);
        // Should not be reached
        assert(0);
    }

    assert(my_start_routine_calls == 1);
    assert(last_start_routine_arg == (void *)(uintptr_t)0xDEADBEEF);

    // ta should be freed
    assert(mock_free_calls == 1);

    // thr_exit should have been called
    assert(mock_thr_exit_calls == 1);
    assert(last_exit_retval == (void *)0x12345678);

    // exit should have been called
    assert(mock_exit_calls == 1);

    printf("host_test_trampoline PASSED\n");
    return 0;
}
