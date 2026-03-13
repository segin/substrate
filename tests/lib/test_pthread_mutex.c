#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>

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

// We want to mock __sync_lock_test_and_set to simulate contention
int mock_sync_lock_test_and_set(int *ptr, int val);
void mock_sync_lock_release(int *ptr);
#define __sync_lock_test_and_set mock_sync_lock_test_and_set
#define __sync_lock_release mock_sync_lock_release

// Include the source to test
#include "../../lib/pthreads/pthread.c"

/*
 * Validates the missing edge case for null attributes.
 * Simply calls the init function with a valid mutex pointer and a NULL attribute pointer and asserts success.
 */
bool test_pthread_mutex_init_null_attr() {
    local_pthread_mutex_t mutex;
    mutex = 42; // arbitrary value to check if it's zeroed

    // Call the init function with a valid mutex pointer and a NULL attribute pointer
    int ret = pthread_mutex_init(&mutex, NULL);

    // Assert success
    assert(ret == 0);

    // Check if it's correctly zeroed
    assert(mutex == 0);

    return true;
}

// Variables to control the behavior of mock_sync_lock_test_and_set
int spin_count = 0;
int max_spins = 0;

int mock_sync_lock_test_and_set(int *ptr, int val) {
    if (spin_count < max_spins) {
        spin_count++;
        return 1; // Simulate that the lock is already held
    }
    // Simulate successful lock acquisition
    int old = *ptr;
    *ptr = val;
    return old;
}

void mock_sync_lock_release(int *ptr) {
    *ptr = 0;
}

/*
 * Validates pthread_mutex_lock acquisition and contention behavior.
 */
bool test_pthread_mutex_lock_contention() {
    local_pthread_mutex_t mutex;

    // Initialize the mutex
    int ret = pthread_mutex_init(&mutex, NULL);
    assert(ret == 0);

    // Test mutex lock - with contention
    spin_count = 0;
    max_spins = 5; // Should loop 5 times then succeed

    ret = pthread_mutex_lock(&mutex);

    // Assert success and correct spinning behavior
    assert(ret == 0);
    assert(mutex == 1);
    assert(spin_count == 5); // Verify it spun the expected number of times

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

    printf("test_pthread_mutex_lock_contention: ");
    if (test_pthread_mutex_lock_contention()) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
        passed = false;
    }

    return passed ? 0 : 1;
}
