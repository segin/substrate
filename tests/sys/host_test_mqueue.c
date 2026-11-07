/*
 * kern_mq_send() and kern_mq_receive() compare a message length with the
 * queue's mq_msgsize unsigned.  The send's check was (long)len > msgsize:
 * on i386 a length of 2 GiB or more is a negative long and passed, and the
 * message's kmalloc(sizeof(struct mq_msg) + len) wrapped to a small block
 * the copy of len bytes then overran.  The receive's mirror image refused a
 * buffer of 2 GiB or more as too small.  Built -m32, as the kernel is.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/errno.h>
#include <sys/proc.h>

process_t *current_process;
thread_t *current_thread;
static int big_allocs;

void *kmalloc(size_t size) {
    if (size > (1u << 20)) {
        big_allocs++;
        return NULL;
    }
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyinstr(const void *src, void *dst, size_t max, size_t *got) {
    size_t n = strnlen(src, max - 1);
    memcpy(dst, src, n);
    ((char *)dst)[n] = '\0';
    if (got) *got = n + 1;
    return 0;
}

#include "../../sys/kern/posix_mqueue.c"

/* the rest of what posix_mqueue.c calls; the blocking paths are not reached */
void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
uint64_t get_ticks(void) { return 1; }
uint32_t get_hz(void) { return 100; }
void sched_wakeup(void *chan) { (void)chan; }
void sched_yield(void) { }
void psignal(struct process *p, int sig) { (void)p; (void)sig; }
process_t *proc_find(int pid) { (void)pid; return current_process; }
uint64_t abstime_ticks(int64_t sec, long nsec, int64_t now_sec, long now_nsec) {
    (void)sec; (void)nsec; (void)now_sec; (void)now_nsec;
    return 0;
}
int kern_clock_gettime(clockid_t clk, struct timespec *ts) { (void)clk; memset(ts, 0, sizeof(*ts)); return 0; }
int sigevent_copyin(const void *u, struct sigevent *k) { (void)u; memset(k, 0, sizeof(*k)); return 0; }
int timespec_copyin(const void *u, struct timespec *k) { memcpy(k, u, sizeof(*k)); return 0; }
void sleepq_add(void *chan, thread_t *t) { (void)chan; (void)t; }
int sleepq_remove_thread(thread_t *t) { (void)t; return 0; }
int sleepq_wake_all(void *chan) { (void)chan; return 0; }

int main(void) {
    static process_t proc;
    static thread_t thread;
    struct mq_attr attr;
    char msg[64];
    int mqd;

    proc.pid = 42;
    current_process = &proc;
    thread.proc = &proc;
    current_thread = &thread;

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 4;
    attr.mq_msgsize = 64;
    mqd = kern_mq_open("/q", O_RDWR | O_CREAT | O_NONBLOCK, 0600, &attr);
    assert(mqd >= 0);

    /* a length past 2 GiB is too long, not a negative one that passes */
    memset(msg, 'x', sizeof(msg));
    assert(kern_mq_send(mqd, msg, 0xFFFFFFF8U, 0, NULL) == -EMSGSIZE);
    assert(kern_mq_send(mqd, msg, 0x80000000U, 0, NULL) == -EMSGSIZE);
    assert(big_allocs == 0);
    assert(kern_mq_send(mqd, msg, 65, 0, NULL) == -EMSGSIZE);
    assert(kern_mq_send(mqd, msg, 64, 0, NULL) == 0);

    /* a receive buffer of 2 GiB or more is big enough */
    assert(kern_mq_receive(mqd, msg, 0x80000000U, NULL, NULL) == 64);
    assert(kern_mq_receive(mqd, msg, 63, NULL, NULL) == -EMSGSIZE);

    printf("host_test_mqueue: PASS\n");
    return 0;
}
