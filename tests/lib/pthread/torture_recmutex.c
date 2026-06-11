/* torture_recmutex.c — substrate-target test for recursive pthread mutexes.
 *
 * Exercises exactly the pattern that deadlocked HexChat: glib's GRecMutex
 * is a PTHREAD_MUTEX_RECURSIVE mutex, and a single thread re-locks it.
 * Before recursive support landed, the second lock parked in FUTEX_WAIT
 * forever.  Also checks cross-thread contention on a recursive mutex (the
 * real futex slow path) and that plain mutexes are unaffected.
 */
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static int g_pass, g_fail;
#define OK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (errno=%d)\n", (msg), errno); } \
} while (0)

static pthread_mutex_t rm;
static volatile int shared;

static void *contender(void *a) {
    (void)a;
    pthread_mutex_lock(&rm);   /* must block until main fully releases */
    shared++;
    pthread_mutex_unlock(&rm);
    return NULL;
}

int main(void) {
    printf("torture_recmutex: recursive pthread mutex\n");

    pthread_mutexattr_t at;
    pthread_mutexattr_init(&at);
    OK(pthread_mutexattr_settype(&at, PTHREAD_MUTEX_RECURSIVE) == 0, "settype RECURSIVE");
    OK(pthread_mutex_init(&rm, &at) == 0, "init recursive mutex");

    /* The deadlock case: same thread re-locks. Must NOT hang. */
    OK(pthread_mutex_lock(&rm) == 0, "lock depth 1");
    OK(pthread_mutex_lock(&rm) == 0, "lock depth 2 (recursive, no deadlock)");
    OK(pthread_mutex_lock(&rm) == 0, "lock depth 3 (recursive)");
    OK(pthread_mutex_unlock(&rm) == 0, "unlock depth 3");
    OK(pthread_mutex_unlock(&rm) == 0, "unlock depth 2");

    /* Still held once: a second thread must block until we release. */
    shared = 0;
    pthread_t t;
    pthread_create(&t, NULL, contender, NULL);
    usleep(80000);
    OK(shared == 0, "contender blocked while we still hold the lock");

    OK(pthread_mutex_unlock(&rm) == 0, "unlock depth 1 (final release)");
    pthread_join(t, NULL);
    OK(shared == 1, "contender acquired after our release");

    /* Plain mutex regression. */
    pthread_mutex_t nm;
    OK(pthread_mutex_init(&nm, NULL) == 0, "init normal mutex");
    OK(pthread_mutex_lock(&nm) == 0, "normal lock");
    OK(pthread_mutex_trylock(&nm) == EBUSY, "normal trylock -> EBUSY");
    OK(pthread_mutex_unlock(&nm) == 0, "normal unlock");
    OK(pthread_mutex_trylock(&nm) == 0, "normal trylock -> 0 after unlock");
    OK(pthread_mutex_unlock(&nm) == 0, "normal unlock 2");

    pthread_mutex_destroy(&rm);
    pthread_mutex_destroy(&nm);

    printf("==== torture_recmutex: %d passed, %d FAILED ====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
