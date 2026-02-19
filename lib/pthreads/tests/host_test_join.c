#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>

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
int mock_thr_new_calls = 0;
int mock_thr_join_calls = 0;
int mock_next_tid = 1;

// Forward declarations
void *mock_malloc(size_t size);
void mock_free(void *ptr);
int64_t mock_syscall(int num, ...);
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

int64_t mock_syscall(int num, ...) {
    va_list args;
    va_start(args, num);
    long arg1 = va_arg(args, long);
    // long arg2 = va_arg(args, long);
    va_end(args);

    if (num == SYS_THR_NEW) {
        mock_thr_new_calls++;
        struct thr_param *param = (struct thr_param *)arg1;
        if (param->child_tid) *(param->child_tid) = mock_next_tid++;
        return 0;
    }
    if (num == SYS_THR_JOIN) {
        mock_thr_join_calls++;
        return 0;
    }
    if (num == SYS_THR_EXIT) {
        return 0;
    }
    return -1;
}

void mock_exit(int status) {
    exit(status);
}

int main() {
    printf("Running host_test_join...\n");

    my_pthread_t t1;
    int ret;

    // 1. Create a thread
    mock_malloc_calls = 0;
    mock_free_calls = 0;
    mock_thr_new_calls = 0;

    ret = my_pthread_create(&t1, NULL, NULL, NULL);
    assert(ret == 0);
    assert(mock_thr_new_calls == 1);
    // 1 malloc for stack, 1 for args
    assert(mock_malloc_calls == 2);

    // 2. Join the thread
    mock_thr_join_calls = 0;
    mock_free_calls = 0;

    ret = my_pthread_join(t1, NULL);
    assert(ret == 0);
    assert(mock_thr_join_calls == 1);

    // Should have freed the stack (1 call)
    // Args are NOT freed because thread didn't run.
    assert(mock_free_calls == 1);

    // 3. Verify slot reuse
    // We already used 1 slot and freed it.
    // If we run MAX_PTHREADS more times, we should be fine.

    for (int i = 0; i < MAX_PTHREADS + 10; i++) {
        my_pthread_t t;
        ret = my_pthread_create(&t, NULL, NULL, NULL);
        assert(ret == 0);

        ret = my_pthread_join(t, NULL);
        assert(ret == 0);
    }

    printf("host_test_join PASSED\n");
    return 0;
}
