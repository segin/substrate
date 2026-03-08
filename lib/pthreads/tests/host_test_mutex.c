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
long mock_syscall(long num, ...);
void mock_exit(int status);

// Redefine symbols to use mocks
#define malloc mock_malloc
#define free mock_free
#define syscall mock_syscall
#define _exit mock_exit

// We want to mock __sync_lock_test_and_set to simulate contention
int mock_sync_lock_test_and_set(int *ptr, int val);
void mock_sync_lock_release(int *ptr);
#define __sync_lock_test_and_set mock_sync_lock_test_and_set
#define __sync_lock_release mock_sync_lock_release

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
    return 0;
}

void mock_exit(int status) {
    exit(status);
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

int main() {
    printf("Running host_test_mutex...\n");

    my_pthread_mutex_t mutex = 1; // start with 1 to make sure init zeros it
    int ret;

    // Test mutex init with NULL attributes
    ret = my_pthread_mutex_init(&mutex, NULL);
    assert(ret == 0);
    assert(mutex == 0);

    // Test mutex lock - success on first try
    spin_count = 0;
    max_spins = 0;
    ret = my_pthread_mutex_lock(&mutex);
    assert(ret == 0);
    assert(mutex == 1);

    ret = my_pthread_mutex_unlock(&mutex);
    assert(ret == 0);
    assert(mutex == 0);

    // Test mutex lock - with contention
    spin_count = 0;
    max_spins = 5; // Should loop 5 times then succeed
    ret = my_pthread_mutex_lock(&mutex);
    assert(ret == 0);
    assert(mutex == 1);
    assert(spin_count == 5); // Verify it spun the expected number of times

    ret = my_pthread_mutex_unlock(&mutex);
    assert(ret == 0);
    assert(mutex == 0);

    ret = my_pthread_mutex_destroy(&mutex);
    assert(ret == 0);

    printf("host_test_mutex PASSED\n");
    return 0;
}
