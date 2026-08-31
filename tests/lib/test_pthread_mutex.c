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
/* libpthread was split into per-feature translation units; the mutex
 * implementation this test drives now lives in pthread_mutex.c. */
#include "../../lib/pthread/pthread_mutex.c"

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

/*
 * Stands in for the atomic exchange in pthread_mutex_lock's slow path.
 *
 * The first max_spins calls report the word as still held (M_LOCKED), so the
 * caller goes back to FUTEX_WAIT.  After that it reports M_UNLOCKED, which is
 * what the exchange returns once the owner has released -- and is the only
 * value that ends the retry loop.  Returning the word's previous value there
 * instead never yields 0, so the loop spins forever.
 */
int mock_sync_lock_test_and_set(int *ptr, int val) {
    if (spin_count < max_spins) {
        spin_count++;
        *ptr = val;                 /* the caller is marking it CONTENDED */
        return M_LOCKED;            /* ... and someone still holds it */
    }
    *ptr = val;
    return M_UNLOCKED;              /* released: the loop can exit */
}

void mock_sync_lock_release(int *ptr) {
    *ptr = 0;
}

/*
 * pthread_mutex_lock is a futex mutex now, not a spin loop.
 *
 * The fast path is a single compare-and-swap 0 -> 1; only when that finds the
 * word already taken does it mark the mutex CONTENDED and sleep in
 * FUTEX_WAIT, re-trying the exchange after each wake.  This test used to set
 * max_spins = 5 and assert the lock had spun exactly five times on
 * __sync_lock_test_and_set -- which an uncontended lock never touches at all.
 */
bool test_pthread_mutex_lock_uncontended() {
    local_pthread_mutex_t mutex;

    int ret = pthread_mutex_init(&mutex, NULL);
    assert(ret == 0);

    spin_count = 0;
    max_spins = 0;

    ret = pthread_mutex_lock(&mutex);

    assert(ret == 0);
    assert(mutex == M_LOCKED);      /* 0 -> 1 via the CAS fast path */
    assert(spin_count == 0);        /* no exchange, no futex wait */

    assert(pthread_mutex_unlock(&mutex) == 0);
    assert(mutex == M_UNLOCKED);

    return true;
}

bool test_pthread_mutex_lock_contention() {
    local_pthread_mutex_t mutex;

    int ret = pthread_mutex_init(&mutex, NULL);
    assert(ret == 0);

    /*
     * Hand the lock word over already held, so the CAS fails and the slow
     * path runs.  mock_sync_lock_test_and_set reports "still held" for the
     * first max_spins exchanges -- each costing one FUTEX_WAIT -- then lets
     * the exchange through, which is what an owner unlocking looks like from
     * in here.
     */
    mutex = M_LOCKED;
    spin_count = 0;
    max_spins = 5;

    ret = pthread_mutex_lock(&mutex);

    assert(ret == 0);
    assert(mutex == M_CONTENDED);   /* the waiter leaves the word marked */
    assert(spin_count == 5);        /* five failed exchanges, five sleeps */

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

    printf("test_pthread_mutex_lock_uncontended: ");
    if (test_pthread_mutex_lock_uncontended()) {
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
