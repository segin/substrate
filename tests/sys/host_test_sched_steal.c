#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Mock percpu
static int current_cpu_id = 0;
int percpu_get_cpu_id(void) {
    return current_cpu_id;
}

// Include kernel headers
// We rely on Makefile adding -idirafter ../../sys/include
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <kern/runqueue.h>
#include <kern/sched.h>

// Mock spinlocks
void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    // In single-threaded test, we just check we don't recurse
    // Note: cpu_id check is simplistic here since we fake CPUs
    lock->locked = 1;
    lock->cpu_id = current_cpu_id;
}

void spinlock_release(spinlock_t *lock) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
}

// Stub for panic if used
void panic(const char *fmt, ...) {
    printf("PANIC: %s\n", fmt);
    exit(1);
}

// Include source files under test
// We need runqueue.c for runqueue operations
#include "../../sys/kern/runqueue.c"
// We need sched_smp.c for sched_steal_thread
#include "../../sys/kern/sched_smp.c"

// Test Case
void test_sched_steal_with_affinity_block() {
    printf("Running test_sched_steal_with_affinity_block...\n");

    // Initialize SMP
    sched_smp_init(2); // 2 CPUs

    // Setup:
    // CPU 0: Idle (will try to steal)
    // CPU 1: Has 2 threads.
    //   - Thread A (Head): Running or ready.
    //   - Thread B (Tail): Pinned to CPU 1.
    //   - Thread C (Middle): Can run on any CPU.

    runqueue_t *rq1 = sched_get_runqueue(1);

    // Create Thread 2 (Unpinned)
    thread_t t2;
    memset(&t2, 0, sizeof(t2));
    t2.sched_class = SCHED_TIMESHARE;
    t2.priority = 0; // Highest timeshare priority
    t2.cpu_affinity = 0; // Any CPU
    t2.state = THREAD_READY;
    t2.tid = 2;

    // Create Thread 1 (Pinned to CPU 1)
    thread_t t1;
    memset(&t1, 0, sizeof(t1));
    t1.sched_class = SCHED_TIMESHARE;
    t1.priority = 0;
    t1.cpu_affinity = (1 << 1); // Only CPU 1
    t1.state = THREAD_READY;
    t1.tid = 1;

    // Add T2 first
    // Note: We access rq1 directly, so lock it manually or rely on runqueue_add not locking (it doesn't, sched_enqueue does)
    // runqueue_add expects caller to hold lock if needed, but it modifies the struct.
    // sched_steal_thread locks the rq.

    // We manually populate the queue to simulate the state
    spinlock_acquire(&rq1->lock);
    runqueue_add(rq1, &t2);
    // Add T1 (Pinned) second -> Becomes Tail
    runqueue_add(rq1, &t1);
    spinlock_release(&rq1->lock);

    assert(rq1->total_threads == 2);
    int level = runqueue_level_for_thread(&t1);
    assert(rq1->queues[level].tail == &t1);
    assert(t1.rq_prev == &t2);

    // Now try to steal from CPU 0
    current_cpu_id = 0;

    thread_t *stolen = sched_steal_thread(1);

    // Current behavior: Stolen should be NULL because T1 blocks it.
    if (stolen == NULL) {
        printf("Reproduced: Failed to steal thread due to pinned tail.\n");
    } else if (stolen == &t2) {
        printf("Fixed: Successfully stole unpinned thread behind pinned tail.\n");
    } else if (stolen == &t1) {
        printf("Error: Stole pinned thread! (Affinity ignored?)\n");
        assert(0);
    } else {
        printf("Error: Stole unknown thread.\n");
    }

    // Assert success
    assert(stolen == &t2);
}

int main() {
    test_sched_steal_with_affinity_block();
    printf("Test passed!\n");
    return 0;
}
