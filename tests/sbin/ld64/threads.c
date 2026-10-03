/*
 * threads.c - a 64-bit program linked dynamically against
 * libpthread.so.0: four threads, a mutex, a pthread key, a __thread
 * variable (each thread's block comes from ld64.so's __ldso_alloc_tls)
 * and join values.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#define NTHREADS 4
#define NLOOPS   20000

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static long counter;
static pthread_key_t key;
static int destructed;

static __thread long tls_id = 100;      /* initialised in every thread */
static __thread long tls_sum;

static void key_destructor(void *p) {
    (void)p;
    __sync_fetch_and_add(&destructed, 1);
}

static void *worker(void *arg) {
    long n = (long)(intptr_t)arg;

    /* A fresh thread sees the initialisation image, not another
     * thread's value. */
    if (tls_id != 100 || tls_sum != 0) return (void *)-1L;
    tls_id += n;
    pthread_setspecific(key, (void *)(intptr_t)(n + 1000));

    for (int i = 0; i < NLOOPS; i++) {
        pthread_mutex_lock(&lock);
        counter++;
        pthread_mutex_unlock(&lock);
        tls_sum += n;
    }
    if (tls_id != 100 + n) return (void *)-2L;
    if (tls_sum != n * NLOOPS) return (void *)-3L;
    if ((long)(intptr_t)pthread_getspecific(key) != n + 1000)
        return (void *)-4L;
    return (void *)(intptr_t)(n * 11);
}

int main(void) {
    pthread_t t[NTHREADS];
    int bad = 0;

    pthread_key_create(&key, key_destructor);
    tls_id = 7;                         /* the main thread's own copy */

    for (long i = 0; i < NTHREADS; i++) {
        if (pthread_create(&t[i], NULL, worker, (void *)(intptr_t)(i + 1))) {
            printf("pthread_create %ld failed\n", i);
            return 1;
        }
    }
    for (long i = 0; i < NTHREADS; i++) {
        void *ret = NULL;
        pthread_join(t[i], &ret);
        printf("thread %ld joined, value %ld\n", i + 1, (long)(intptr_t)ret);
        if ((long)(intptr_t)ret != (i + 1) * 11) bad = 1;
    }
    printf("counter=%ld (want %d) main tls_id=%ld destructors=%d\n",
           counter, NTHREADS * NLOOPS, tls_id, destructed);
    if (counter != NTHREADS * NLOOPS || tls_id != 7 ||
        destructed != NTHREADS)
        bad = 1;
    printf("threads: %s\n", bad ? "FAIL" : "OK");
    return bad;
}
