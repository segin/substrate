#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#define MAX_THREADS 64
#define MAX_WAITQS 32
#define ETIMEDOUT_SUBSTRATE 110

#include <sys/proc.h>
#include <sys/futex.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <arch/i386/pmap.h>

typedef struct {
    void *chan;
    int private_flag;
    int pid;
    thread_t *waiters[MAX_THREADS];
    int count;
} mock_waitq_t;

typedef void (*yield_hook_t)(void);

thread_t *current_thread = NULL;
process_t *current_process = NULL;
thread_t threads[MAX_THREADS];
static mock_waitq_t waitqs[MAX_WAITQS];
static int waitq_count = 0;
static yield_hook_t yield_hook = NULL;

static mock_waitq_t *waitq_lookup(void *chan, int private_flag, int pid, int create) {
    int i;

    for (i = 0; i < waitq_count; i++) {
        if (waitqs[i].chan == chan &&
            waitqs[i].private_flag == private_flag &&
            waitqs[i].pid == pid) {
            return &waitqs[i];
        }
    }

    if (!create || waitq_count >= MAX_WAITQS) {
        return NULL;
    }

    memset(&waitqs[waitq_count], 0, sizeof(waitqs[waitq_count]));
    waitqs[waitq_count].chan = chan;
    waitqs[waitq_count].private_flag = private_flag;
    waitqs[waitq_count].pid = pid;
    waitq_count++;
    return &waitqs[waitq_count - 1];
}

static void waitq_remove_if_empty(mock_waitq_t *q) {
    int idx;

    if (!q || q->count != 0) {
        return;
    }

    idx = (int)(q - waitqs);
    memmove(&waitqs[idx], &waitqs[idx + 1], sizeof(waitqs[0]) * (waitq_count - idx - 1));
    waitq_count--;
}

static int mock_waitq_count(void *chan, int private_flag, int pid) {
    mock_waitq_t *q = waitq_lookup(chan, private_flag, pid, 0);
    return q ? q->count : 0;
}

static void reset_env(void) {
    int i;

    memset(threads, 0, sizeof(threads));
    for (i = 0; i < MAX_THREADS; i++) {
        threads[i].tid = -1;
    }
    memset(waitqs, 0, sizeof(waitqs));
    waitq_count = 0;
    current_thread = NULL;
    current_process = NULL;
    yield_hook = NULL;
}

static void setup_thread(int index, int tid, int uid, int pid, int priority, process_t *proc) {
    memset(&threads[index], 0, sizeof(thread_t));
    memset(proc, 0, sizeof(*proc));
    proc->uid = uid;
    proc->euid = uid;
    proc->pid = pid;
    proc->pmap = (pmap_t)1;
    threads[index].tid = tid;
    threads[index].priority = priority;
    threads[index].proc = proc;
}

uintptr_t pmap_extract(void *pmap, uintptr_t va) {
    (void)pmap;
    return va;
}

void sched_wakeup(void *chan) {
    (void)chan;
}

void sched_sleep(void *chan) {
    (void)chan;
}

void sched_yield(void) {
    if (yield_hook) {
        yield_hook();
    }
}

thread_t *sched_get_thread(int tid) {
    int i;

    for (i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid) {
            return &threads[i];
        }
    }
    return NULL;
}

void sched_set_priority(int tid, sched_class_t cls, int prio) {
    thread_t *t;

    (void)cls;
    t = sched_get_thread(tid);
    assert(t != NULL);
    t->priority = prio;
}

uint64_t get_ticks(void) {
    return 1000;
}

uint32_t get_hz(void) {
    return 1000;
}

void sleepq_init(void) {
    memset(waitqs, 0, sizeof(waitqs));
    waitq_count = 0;
}

void sleepq_add(void *chan, thread_t *t) {
    mock_waitq_t *q = waitq_lookup(chan, 0, 0, 1);
    assert(q != NULL);
    assert(q->count < MAX_THREADS);
    q->waiters[q->count++] = t;
}

void sleepq_add_private(void *chan, thread_t *t) {
    mock_waitq_t *q;

    assert(current_process != NULL);
    q = waitq_lookup(chan, 1, current_process->pid, 1);
    assert(q != NULL);
    assert(q->count < MAX_THREADS);
    q->waiters[q->count++] = t;
}

thread_t *sleepq_wake_one(void *chan) {
    return sleepq_wake_n(chan, 1) > 0 ? current_thread : NULL;
}

thread_t *sleepq_wake_one_private(void *chan) {
    return sleepq_wake_n_private(chan, 1) > 0 ? current_thread : NULL;
}

int sleepq_wake_all(void *chan) {
    return sleepq_wake_n(chan, -1);
}

int sleepq_wake_all_private(void *chan) {
    return sleepq_wake_n_private(chan, -1);
}

static int sleepq_wake_n_internal(void *chan, int private_flag, int pid, int n) {
    mock_waitq_t *q = waitq_lookup(chan, private_flag, pid, 0);

    if (!q || q->count == 0 || n == 0) {
        return 0;
    }
    if (n < 0 || n > q->count) {
        n = q->count;
    }

    memmove(&q->waiters[0], &q->waiters[n], sizeof(q->waiters[0]) * (q->count - n));
    q->count -= n;
    waitq_remove_if_empty(q);
    return n;
}

int sleepq_wake_n(void *chan, int n) {
    return sleepq_wake_n_internal(chan, 0, 0, n);
}

int sleepq_wake_n_private(void *chan, int n) {
    assert(current_process != NULL);
    return sleepq_wake_n_internal(chan, 1, current_process->pid, n);
}

int sleepq_has_waiters(void *chan) {
    return mock_waitq_count(chan, 0, 0) > 0;
}

int sleepq_has_waiters_private(void *chan) {
    assert(current_process != NULL);
    return mock_waitq_count(chan, 1, current_process->pid) > 0;
}

static int sleepq_requeue_internal(void *src_chan, void *dst_chan, int private_flag, int pid,
                                   int wake_n, int requeue_n) {
    mock_waitq_t *src = waitq_lookup(src_chan, private_flag, pid, 0);
    int woken = 0;
    mock_waitq_t *dst;

    if (!src || src->count == 0) {
        return 0;
    }

    if (wake_n < 0 || wake_n > src->count) {
        wake_n = src->count;
    }
    if (wake_n > 0) {
        memmove(&src->waiters[0], &src->waiters[wake_n], sizeof(src->waiters[0]) * (src->count - wake_n));
        src->count -= wake_n;
        woken = wake_n;
    }

    if (src->count > 0 && requeue_n > 0) {
        if (requeue_n > src->count) {
            requeue_n = src->count;
        }
        dst = waitq_lookup(dst_chan, private_flag, pid, 1);
        assert(dst != NULL);
        assert(dst->count + requeue_n <= MAX_THREADS);
        memcpy(&dst->waiters[dst->count], &src->waiters[0], sizeof(src->waiters[0]) * requeue_n);
        dst->count += requeue_n;
        memmove(&src->waiters[0], &src->waiters[requeue_n], sizeof(src->waiters[0]) * (src->count - requeue_n));
        src->count -= requeue_n;
    }

    waitq_remove_if_empty(src);
    return woken;
}

int sleepq_requeue(void *src_chan, void *dst_chan, int wake_n, int requeue_n) {
    return sleepq_requeue_internal(src_chan, dst_chan, 0, 0, wake_n, requeue_n);
}

int sleepq_requeue_private(void *src_chan, void *dst_chan, int wake_n, int requeue_n) {
    assert(current_process != NULL);
    return sleepq_requeue_internal(src_chan, dst_chan, 1, current_process->pid, wake_n, requeue_n);
}

#include "../../sys/kern/futex.c"

#define RUN_TEST(fn) \
    do { \
        printf("Running %s...\n", #fn); \
        fn(); \
        printf("PASS\n"); \
    } while (0)

static void test_sys_get_robust_list_current_thread(void) {
    process_t proc;
    struct robust_list_head *head;
    size_t len;
    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    reset_env();
    setup_thread(0, 100, 1000, 10, 20, &proc);
    current_thread = &threads[0];
    current_process = &proc;

    head = (void *)0x1000;
    len = sizeof(*head);
    assert(sys_set_robust_list(head, len) == 0);
    assert(sys_get_robust_list(0, &out_head, &out_len) == 0);
    assert(out_head == head);
    assert(out_len == len);
}

static void test_sys_get_robust_list_permissions(void) {
    process_t proc1;
    process_t proc2;
    process_t proc3;
    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    reset_env();
    setup_thread(0, 100, 1000, 10, 20, &proc1);
    setup_thread(1, 200, 1000, 10, 20, &proc1);
    setup_thread(2, 300, 1000, 20, 20, &proc2);
    setup_thread(3, 400, 2000, 30, 20, &proc3);
    current_thread = &threads[0];
    current_process = &proc1;

    threads[1].robust_list = (void *)0x2000;
    threads[1].robust_list_len = sizeof(struct robust_list_head);
    threads[2].robust_list = (void *)0x3000;
    threads[2].robust_list_len = sizeof(struct robust_list_head);
    threads[3].robust_list = (void *)0x4000;
    threads[3].robust_list_len = sizeof(struct robust_list_head);

    assert(sys_get_robust_list(200, &out_head, &out_len) == 0);
    assert(out_head == (void *)0x2000);
    assert(sys_get_robust_list(300, &out_head, &out_len) == 0);
    assert(out_head == (void *)0x3000);
    assert(sys_get_robust_list(400, &out_head, &out_len) == -EPERM);

    proc1.euid = 0;
    assert(sys_get_robust_list(400, &out_head, &out_len) == 0);
    assert(out_head == (void *)0x4000);
    assert(sys_get_robust_list(999, &out_head, &out_len) == -ESRCH);
}

static void wake_shared_waiter(void) {
    int *futex_word = (int *)current_thread->robust_list;
    thread_t *waiter = current_thread;
    process_t *waiter_proc = current_process;
    process_t waker_proc;

    setup_thread(1, 200, 1000, 20, 20, &waker_proc);
    current_thread = &threads[1];
    current_process = &waker_proc;
    assert(sys_futex(futex_word, FUTEX_WAKE, 1, NULL, NULL, 0) == 1);
    current_thread = waiter;
    current_process = waiter_proc;
}

static void mark_waiter_timed_out(void) {
    current_thread->sleep_status = -ETIMEDOUT_SUBSTRATE;
}

static void test_futex_wait_wake_handshake(void) {
    process_t proc;
    int futex_word = 42;

    reset_env();
    setup_thread(0, 100, 1000, 10, 20, &proc);
    current_thread = &threads[0];
    current_process = &proc;
    current_thread->robust_list = (struct robust_list_head *)(uintptr_t)&futex_word;
    yield_hook = wake_shared_waiter;

    assert(sys_futex(&futex_word, FUTEX_WAIT, 42, NULL, NULL, 0) == 0);
    assert(!sleepq_has_waiters((void *)(uintptr_t)&futex_word));
}

static void test_futex_wait_timeout(void) {
    process_t proc;
    int futex_word = 42;
    struct timespec ts;

    reset_env();
    setup_thread(0, 100, 1000, 10, 20, &proc);
    current_thread = &threads[0];
    current_process = &proc;
    ts.tv_sec = 0;
    ts.tv_nsec = 50000000;
    yield_hook = mark_waiter_timed_out;

    assert(sys_futex(&futex_word, FUTEX_WAIT, 42, &ts, NULL, 0) == -ETIMEDOUT_SUBSTRATE);
}

static void test_futex_requeue_moves_waiters(void) {
    process_t proc;
    int futex1 = 7;
    int futex2 = 9;

    reset_env();
    setup_thread(0, 100, 1000, 10, 20, &proc);
    current_thread = &threads[0];
    current_process = &proc;

    sleepq_add((void *)(uintptr_t)&futex1, current_thread);
    assert(sleepq_has_waiters((void *)(uintptr_t)&futex1));
    assert(sys_futex(&futex1, FUTEX_REQUEUE, 0, (void *)1, &futex2, 0) == 0);
    assert(!sleepq_has_waiters((void *)(uintptr_t)&futex1));
    assert(sleepq_has_waiters((void *)(uintptr_t)&futex2));
}

struct robust_node {
    struct robust_list list;
    int futex;
};

static void test_futex_robust_cleanup_on_exit(void) {
    process_t owner_proc;
    process_t waiter_proc;
    struct robust_list_head head;
    struct robust_node node;

    reset_env();
    setup_thread(0, 100, 1000, 10, 20, &owner_proc);
    setup_thread(1, 200, 1000, 20, 20, &waiter_proc);
    current_thread = &threads[0];
    current_process = &owner_proc;

    memset(&head, 0, sizeof(head));
    memset(&node, 0, sizeof(node));
    head.list.next = &node.list;
    head.futex_offset = offsetof(struct robust_node, futex);
    node.list.next = &head.list;
    node.futex = threads[0].tid;
    threads[0].robust_list = &head;
    threads[0].robust_list_len = sizeof(head);

    sleepq_add((void *)(uintptr_t)&node.futex, &threads[1]);
    assert(sleepq_has_waiters((void *)(uintptr_t)&node.futex));

    futex_thread_exit(&threads[0]);

    assert((node.futex & FUTEX_OWNER_DIED) != 0);
    assert((node.futex & FUTEX_TID_MASK) == 0);
    assert(!sleepq_has_waiters((void *)(uintptr_t)&node.futex));
}

static void unlock_pi_from_owner(void) {
    int *futex_word = (int *)current_thread->robust_list;
    thread_t *waiter = current_thread;
    process_t *waiter_proc = current_process;
    process_t *owner_proc = threads[0].proc;

    assert(threads[0].priority == waiter->priority);

    current_thread = &threads[0];
    current_process = owner_proc;
    assert(sys_futex(futex_word, FUTEX_UNLOCK_PI, 0, NULL, NULL, 0) == 0);
    current_thread = waiter;
    current_process = waiter_proc;
}

static void test_futex_pi_inheritance(void) {
    process_t owner_proc;
    process_t waiter_proc;
    int futex_word;
    const int owner_priority = 10;
    const int waiter_priority = 50;

    reset_env();
    setup_thread(0, 100, 1000, 10, owner_priority, &owner_proc);
    setup_thread(1, 200, 1000, 20, waiter_priority, &waiter_proc);
    futex_word = threads[0].tid;

    current_thread = &threads[1];
    current_process = &waiter_proc;
    current_thread->robust_list = (struct robust_list_head *)(uintptr_t)&futex_word;
    yield_hook = unlock_pi_from_owner;

    assert(sys_futex(&futex_word, FUTEX_LOCK_PI, 0, NULL, NULL, 0) == 0);
    assert((futex_word & FUTEX_TID_MASK) == (uint32_t)threads[1].tid);
    assert(threads[0].priority == owner_priority);
}

int main(void) {
    RUN_TEST(test_sys_get_robust_list_current_thread);
    RUN_TEST(test_sys_get_robust_list_permissions);
    RUN_TEST(test_futex_wait_wake_handshake);
    RUN_TEST(test_futex_wait_timeout);
    RUN_TEST(test_futex_requeue_moves_waiters);
    RUN_TEST(test_futex_robust_cleanup_on_exit);
    RUN_TEST(test_futex_pi_inheritance);
    printf("All tests passed!\n");
    return 0;
}
