#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pm/pm.h>
#include <kern/sched.h>
#include <sys/wait.h>
#include <sys/session.h>
#include <arch/i386/pmap.h>
#include <vm/vm_map.h>

// Globals required by wait.c
process_t *current_process = NULL;
thread_t *current_thread = NULL;
mutex_t proctree_lock;

// wait.c enumerates threads via FOREACH_THREAD / thread_first /
// thread_next (the dynamic allthread list).  This benchmark has no live
// threads, so iteration is empty and wait_threads_all_zombie()
// trivially returns 1.
thread_t *thread_first(void) { return NULL; }
thread_t *thread_next(thread_t *t) { (void)t; return NULL; }

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }

void sched_yield(void) {}
void sched_sleep(void *chan) { (void)chan; }
void sched_reap_process_threads(process_t *proc) { (void)proc; }

void proc_remove_child(process_t *parent, process_t *child) {
    if (!parent || !child) return;
    if (parent->p_children == child) {
        parent->p_children = child->p_sibling;
    } else {
        process_t *prev = parent->p_children;
        while (prev && prev->p_sibling != child) prev = prev->p_sibling;
        if (prev) prev->p_sibling = child->p_sibling;
    }
    child->p_sibling = NULL;
}

void pgrp_remove_proc(struct process *proc) {
    (void)proc;
}

int copyout(const void *kaddr, void *uaddr, size_t len) {
    memcpy(uaddr, kaddr, len);
    return 0;
}

// Reap-path mocks
pmap_t pmap_kernel(void) { return (pmap_t)0xABCDEF00; }
void pmap_release(pmap_t pmap) { (void)pmap; }
void vm_map_destroy(vm_map_t *map) { (void)map; }
void ldt_free_process(struct process *proc) { (void)proc; }
void procfs_release_pid_nodes(int pid) { (void)pid; }
void proc_destroy(process_t *p) { free(p); }
uint64_t get_ticks(void) { return 0; }

// Include the implementation
#include "../../sys/pm/wait.c"

// Helpers for benchmark
static process_t *create_mock_process(int pid) {
    process_t *p = calloc(1, sizeof(process_t));
    assert(p != NULL);
    p->pid = pid;
    return p;
}

static void add_child(process_t *parent, process_t *child) {
    child->p_parent = parent;
    child->p_sibling = parent->p_children;
    parent->p_children = child;
}

static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("Benchmarking kern_wait4 (HOST_TEST)...\n");

    // Test cases
    int child_counts[] = {10, 100, 1000, 10000, 20000};

    printf("%-10s %-20s %-20s\n", "Children", "Wait Specific (s)", "Wait Any (s)");

    for (size_t i = 0; i < sizeof(child_counts)/sizeof(int); i++) {
        int N = child_counts[i];

        process_t parent;
        memset(&parent, 0, sizeof(parent));
        parent.pid = 1;
        current_process = &parent;
        thread_t thread;
        memset(&thread, 0, sizeof(thread));
        current_thread = &thread;

        // Create N children
        for (int j = 0; j < N; j++) {
            process_t *child = create_mock_process(100 + j);
            child->state = SZOMB;
            add_child(&parent, child);
        }

        // Measure Wait Specific (Tail - Worst Case: PID 100)
        double start = get_time_sec();
        int status;
        int ret = kern_wait4(100, &status, WNOHANG, NULL);
        double end = get_time_sec();

        assert(ret == 100);
        double time_specific = end - start;

        // Measure Wait Any (Head - Best Case)
        start = get_time_sec();
        ret = kern_wait4(-1, &status, WNOHANG, NULL);
        end = get_time_sec();

        assert(ret > 0);
        double time_any = end - start;

        printf("%-10d %-20.9f %-20.9f\n", N, time_specific, time_any);

        // Cleanup not strictly needed as we exit
    }

    return 0;
}
