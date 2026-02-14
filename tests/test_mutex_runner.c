#include <stdio.h>
#include <stdbool.h>
#include <sys/lock.h>
#include <kern/sched.h>

// Externs from test_sync.c
extern bool test_mutex_basic(void);
extern bool test_mutex_trylock(void);
extern bool test_mutex_ownership(void);
extern bool test_mutex_multiple_waiters(void);
extern bool test_mutex_contention(void);

int main() {
    sched_init();

    printf("Running Mutex Tests...\n");

    printf("test_mutex_basic: ");
    if (test_mutex_basic()) printf("PASS\n");
    else { printf("FAIL\n"); return 1; }

    printf("test_mutex_trylock: ");
    if (test_mutex_trylock()) printf("PASS\n");
    else { printf("FAIL\n"); return 1; }

    printf("test_mutex_ownership: ");
    if (test_mutex_ownership()) printf("PASS\n");
    else { printf("FAIL\n"); return 1; }

    printf("test_mutex_multiple_waiters: ");
    if (test_mutex_multiple_waiters()) printf("PASS\n");
    else { printf("FAIL\n"); return 1; }

    printf("test_mutex_contention: ");
    if (test_mutex_contention()) printf("PASS\n");
    else { printf("FAIL\n"); return 1; }

    printf("All Mutex tests passed!\n");
    return 0;
}
