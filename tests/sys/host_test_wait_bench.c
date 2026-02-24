#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <assert.h>

// Mock Kernel Types before they are needed
typedef int mutex_t;
typedef int spinlock_t;

// Mock kernel-specific defines
#define AC_COMM_LEN 16
#define NSIG 32
#define MAX_FD 32
#define MAX_THREADS 64

// Mock Forward Declarations
struct personality;
struct fs_node;
typedef struct fs_node fs_node_t;
struct runqueue;
struct file;
typedef struct file file_t;
struct pmap;
struct registers;
struct vm_area;
struct vm_map;
struct tty;

// We need to define _SYS_LOCK_H to skip kernel sys/lock.h if it conflicts or if we want to mock it simply.
// sys/lock.h usually defines mutex_t, spinlock_t.
// If we define them above, we should skip sys/lock.h.
#define _SYS_LOCK_H

// Helper macro to skip sys/sched.h?
// sys/pm/wait.c includes <kern/sched.h>.
// We need to provide a mock for it.
// We can't prevent include by macro unless it has a guard we know.
// kern/sched.h likely has _KERN_SCHED_H.
#define _KERN_SCHED_H
// But we need to define sched_sleep decl.
void sched_sleep(void *chan);


// Include Kernel headers
// sys/types.h handles HOST_TEST
#include "../../sys/include/sys/types.h"
#include "../../sys/include/sys/proc.h"

// Undefine host macros before including kernel wait.h
#undef WIFEXITED
#undef WEXITSTATUS
#undef WIFSIGNALED
#undef WTERMSIG
#undef WCOREDUMP
#undef WIFSTOPPED
#undef WSTOPSIG
#undef WIFCONTINUED

#include "../../sys/include/sys/wait.h"
#include "../../sys/include/sys/resource.h"
#include "../../sys/include/sys/session.h"

// Globals required by wait.c
process_t *current_process = NULL;
thread_t *current_thread = NULL;

// Mock Functions
void kprint(const char *msg) {
    // printf("%s", msg);
    (void)msg;
}

void panic(const char *msg) {
    printf("PANIC: %s\n", msg);
    exit(1);
}

void sched_sleep(void *chan) {
    (void)chan;
}

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

// Mock other functions used by wait.c?
// wait.c uses:
// - current_process
// - sched_sleep
// - find_waitable_child (static)
// - proc_remove_child
// - pgrp_remove_proc
// - copyout
// - pid_t, process_t, etc.

// It includes sys/kern_syscalls.h.
// We need to make sure that doesn't fail.
// It declares function prototypes. Should be fine.

// Include the implementation
#include "../../sys/pm/wait.c"

// Helpers for benchmark
process_t *create_mock_process(int pid) {
    process_t *p = calloc(1, sizeof(process_t));
    p->pid = pid;
    return p;
}

void add_child(process_t *parent, process_t *child) {
    child->p_parent = parent;
    child->p_sibling = parent->p_children;
    parent->p_children = child;
}

double get_time_sec() {
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
