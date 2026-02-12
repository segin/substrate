#define HOST_TEST
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <sys/types.h>

/* Helper for tests */
#define MAX_THREADS 64

/* Mocks are provided via -I tests/sys/mocks */
#include <sys/proc.h>
#include <sys/futex.h>
#include <kern/sched.h>
#include <arch/i386/pmap.h>

/* Mock implementations */
uint32_t pmap_extract(pmap_t pmap, uint32_t va) {
    return va;
}
void sched_sleep(void *chan) {}
int sched_sleep_until(void *chan, uint64_t deadline) {
    /* Verify deadline if needed, or return ETIMEDOUT to simulate timeout */
    return -110; /* ETIMEDOUT in host Linux usually, but generic errno is 110 */
}
uint64_t get_ticks(void) { return 1000; }
uint32_t get_hz(void) { return 1000; }
void sched_set_priority(int tid, int cls, int prio) {}
int sleepq_wake_n(void *chan, int n) { return 0; }
int sleepq_requeue(void *src, void *dst, int wake_n, int requeue_n) { return 0; }

thread_t *current_thread = NULL;
process_t *current_process = NULL;
thread_t threads[MAX_THREADS];

thread_t *sched_get_thread(int tid) {
    for (int i = 0; i < MAX_THREADS; i++) {
        if (threads[i].tid == tid) return &threads[i];
    }
    return NULL;
}

/* Include source to test */
#include "../../sys/kern/futex.c"

/* Test helpers */
void setup_thread(int index, int tid, int uid, process_t *proc) {
    memset(&threads[index], 0, sizeof(thread_t));
    threads[index].tid = tid;
    threads[index].proc = proc;
    memset(proc, 0, sizeof(process_t));
    proc->uid = uid;
    proc->euid = uid;
    proc->pmap = (pmap_t)1; /* Dummy non-null */
}

/* Test cases */

void test_sys_get_robust_list_current_thread() {
    printf("Running test_sys_get_robust_list_current_thread...\n");

    process_t proc;
    setup_thread(0, 100, 1000, &proc);
    current_thread = &threads[0];
    current_process = &proc;

    struct robust_list_head *head = (void*)0x1000;
    size_t len = sizeof(*head);

    sys_set_robust_list(head, len);

    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    int ret = sys_get_robust_list(0, &out_head, &out_len);
    assert(ret == 0);
    assert(out_head == head);
    assert(out_len == len);

    printf("PASS\n");
}

void test_sys_get_robust_list_other_thread_allowed() {
    printf("Running test_sys_get_robust_list_other_thread_allowed...\n");

    process_t proc1;
    /* Current thread: TID 100 */
    setup_thread(0, 100, 1000, &proc1);
    current_thread = &threads[0];
    current_process = &proc1;

    /* Target thread: TID 200, Same process */
    threads[1].tid = 200;
    threads[1].proc = &proc1;

    struct robust_list_head *head = (void*)0x2000;
    size_t len = sizeof(*head);
    threads[1].robust_list = head;
    threads[1].robust_list_len = len;

    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    int ret = sys_get_robust_list(200, &out_head, &out_len);

    if (ret != 0) {
        printf("FAIL: ret=%d (expected 0)\n", ret);
    } else {
        assert(out_head == head);
        assert(out_len == len);
        printf("PASS\n");
    }
}

void test_sys_get_robust_list_other_thread_same_uid() {
    printf("Running test_sys_get_robust_list_other_thread_same_uid...\n");

    process_t proc1, proc2;
    /* Current thread: TID 100, UID 1000 */
    setup_thread(0, 100, 1000, &proc1);
    current_thread = &threads[0];
    current_process = &proc1;

    /* Target thread: TID 300, UID 1000 (Diff process, same UID) */
    setup_thread(2, 300, 1000, &proc2);

    struct robust_list_head *head = (void*)0x3000;
    size_t len = sizeof(*head);
    threads[2].robust_list = head;
    threads[2].robust_list_len = len;

    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    int ret = sys_get_robust_list(300, &out_head, &out_len);
    if (ret != 0) {
        printf("FAIL: ret=%d (expected 0)\n", ret);
    } else {
        assert(out_head == head);
        printf("PASS\n");
    }
}

void test_sys_get_robust_list_denied() {
    printf("Running test_sys_get_robust_list_denied...\n");

    process_t proc1, proc2;
    /* Current thread: TID 100, UID 1000 */
    setup_thread(0, 100, 1000, &proc1);
    current_thread = &threads[0];
    current_process = &proc1;

    /* Target thread: TID 400, UID 2000 (Diff process, diff UID) */
    setup_thread(3, 400, 2000, &proc2);

    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    int ret = sys_get_robust_list(400, &out_head, &out_len);

    if (ret == 0) {
        printf("FAIL: Should have denied access\n");
        exit(1);
    }
    printf("PASS (ret=%d)\n", ret);
}

void test_sys_get_robust_list_root_allowed() {
    printf("Running test_sys_get_robust_list_root_allowed...\n");

    process_t proc1, proc2;
    /* Current thread: TID 100, UID 0 (Root) */
    setup_thread(0, 100, 0, &proc1);
    current_thread = &threads[0];
    current_process = &proc1;

    /* Target thread: TID 500, UID 2000 (Diff process, diff UID) */
    setup_thread(4, 500, 2000, &proc2);

    struct robust_list_head *head = (void*)0x5000;
    size_t len = sizeof(*head);
    threads[4].robust_list = head;
    threads[4].robust_list_len = len;

    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    int ret = sys_get_robust_list(500, &out_head, &out_len);
    assert(ret == 0);
    assert(out_head == head);

    printf("PASS\n");
}

void test_sys_get_robust_list_not_found() {
    printf("Running test_sys_get_robust_list_not_found...\n");

    struct robust_list_head *out_head = NULL;
    size_t out_len = 0;

    int ret = sys_get_robust_list(999, &out_head, &out_len);
    assert(ret == -ESRCH);

    printf("PASS\n");
}

int main() {
    /* Clear threads */
    memset(threads, 0, sizeof(threads));
    for(int i=0; i<MAX_THREADS; i++) threads[i].tid = -1;

    test_sys_get_robust_list_current_thread();
    test_sys_get_robust_list_other_thread_allowed();
    test_sys_get_robust_list_other_thread_same_uid();
    test_sys_get_robust_list_denied();
    test_sys_get_robust_list_root_allowed();
    test_sys_get_robust_list_not_found();

    printf("Running test_futex_wait_timeout...\n");
    int futex_word = 42;
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = 50000000; /* 50ms */

    int ret = sys_futex(&futex_word, FUTEX_WAIT, 42, &ts, NULL, 0);
    assert(ret == -110); /* ETIMEDOUT */
    printf("PASS\n");

    printf("All tests passed!\n");
    return 0;
}
