/*
 * wakeopatomic — FUTEX_WAKE_OP updates *uaddr2 atomically.
 *
 * The kernel read the word, computed the new value and wrote it back in
 * two steps, so an increment another thread made in between was lost.
 * Two threads add 1 to one word through FUTEX_WAKE_OP while two more add
 * 1 with a locked add; the total must come out exact.
 *
 * Linux build (raw futex); runs on the host too.
 */
#define _GNU_SOURCE
#include <linux/futex.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

#define ROUNDS 200000

static int dummy;
static int word;

static void *by_wakeop(void *arg)
{
    (void)arg;
    for (int i = 0; i < ROUNDS; i++)
        syscall(SYS_futex, &dummy, FUTEX_WAKE_OP, 0, 0, &word,
                FUTEX_OP(FUTEX_OP_ADD, 1, FUTEX_OP_CMP_EQ, 0));
    return NULL;
}

static void *by_add(void *arg)
{
    (void)arg;
    for (int i = 0; i < ROUNDS; i++)
        __sync_fetch_and_add(&word, 1);
    return NULL;
}

int main(void)
{
    pthread_t t[4];
    pthread_create(&t[0], NULL, by_wakeop, NULL);
    pthread_create(&t[1], NULL, by_add, NULL);
    pthread_create(&t[2], NULL, by_wakeop, NULL);
    pthread_create(&t[3], NULL, by_add, NULL);
    for (int i = 0; i < 4; i++)
        pthread_join(t[i], NULL);
    int ok = word == 4 * ROUNDS;
    printf("wakeopatomic: %d of %d increments: %s\n", word, 4 * ROUNDS,
           ok ? "PASS" : "FAIL");
    return !ok;
}
