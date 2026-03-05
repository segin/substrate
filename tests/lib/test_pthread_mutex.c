#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Mocking required things to test pthread.c
#define sys_thr_create mock_sys_thr_create
#define sys_thr_exit mock_sys_thr_exit
#define sys_futex_wait mock_sys_futex_wait
#define sys_futex_wake mock_sys_futex_wake
#define mmap mock_mmap
#define munmap mock_munmap

int mock_sys_thr_create(uintptr_t stack_base, uintptr_t stack_size, void *entry, void *arg) { (void)stack_base; (void)stack_size; (void)entry; (void)arg; return 0; }
void mock_sys_thr_exit(void *retval) { (void)retval; }
int mock_sys_futex_wait(int *uaddr, int val) { (void)uaddr; (void)val; return 0; }
int mock_sys_futex_wake(int *uaddr, int count) { (void)uaddr; (void)count; return 0; }
void *mock_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) { (void)addr; (void)length; (void)prot; (void)flags; (void)fd; (void)offset; return NULL; }
int mock_munmap(void *addr, size_t length) { (void)addr; (void)length; return 0; }

// Rename types to avoid conflict
#define pthread_t local_pthread_t
#define pthread_attr_t local_pthread_attr_t
#define pthread_mutex_t local_pthread_mutex_t

// Include standard types that might be needed by pthread.c
#include <sys/types.h>
#include <sys/mman.h>

// Include the source to test
#include "../../lib/pthreads/pthread.c"

bool test_pthread_mutex_init_null_attr() {
    local_pthread_mutex_t mutex;
    mutex = 42; // arbitrary value to check if it's zeroed

    // Call the init function with a valid mutex pointer and a NULL attribute pointer
    int ret = pthread_mutex_init(&mutex, NULL);

    // Assert success
    if (ret != 0) {
        printf("pthread_mutex_init returned %d (expected 0)\n", ret);
        return false;
    }

    // Check if it's correctly zeroed
    if (mutex != 0) {
        printf("pthread_mutex_init did not set mutex to 0 (got %d)\n", mutex);
        return false;
    }

    return true;
}

int main() {
    bool passed = true;
    printf("test_pthread_mutex_init_null_attr: ");
    if (test_pthread_mutex_init_null_attr()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    return passed ? 0 : 1;
}
