/*
 * An 8-byte __atomic_load_n and __atomic_store_n on i386 move all eight
 * bytes at once: one thread stores 0 and ~0 by turns, another loads, and
 * no load may see half of each.  Two plain 32-bit moves are torn here
 * within a few thousand rounds on any machine with two CPUs.
 * run_atomic8.sh builds it with cc for each i386 -march.
 */
#include <pthread.h>
#include <stdio.h>

#define ROUNDS 20000000L

static unsigned long long shared;
static volatile int done;

static void *writer(void *arg) {
    long i;

    (void)arg;
    for (i = 0; i < ROUNDS; ++i) {
        __atomic_store_n(&shared, (i & 1) ? ~0ULL : 0ULL, __ATOMIC_RELAXED);
    }
    done = 1;
    return NULL;
}

int main(void) {
    pthread_t t;
    long torn = 0, reads = 0;

    if (pthread_create(&t, NULL, writer, NULL) != 0) {
        return 2;
    }
    while (!done) {
        unsigned long long v = __atomic_load_n(&shared, __ATOMIC_RELAXED);

        if (v != 0 && v != ~0ULL) {
            ++torn;
        }
        ++reads;
    }
    pthread_join(t, NULL);
    printf("torn %ld\n", torn);
    return torn != 0 || reads == 0;
}
