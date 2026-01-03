#include <assert.h>
#include <stdio.h>
#include <stdbool.h>

typedef enum { READY, RUNNING, BLOCKED } state_t;
typedef struct { int tid; state_t state; } thread_t;

thread_t threads[2];

thread_t* mock_sched_yield(thread_t* current) {
    if (current->state == RUNNING) current->state = READY;
    
    // Simple Round Robin: find the next thread AFTER current
    int start_index = (current->tid % 2); // Start at next thread
    for (int i = 0; i < 2; i++) {
        int idx = (start_index + i) % 2;
        if (threads[idx].state == READY) {
            threads[idx].state = RUNNING;
            return &threads[idx];
        }
    }
    return current;
}

void test_sched_property() {
    printf("Testing scheduler property: non-starvation of READY threads...\n");
    threads[0].tid = 1; threads[0].state = RUNNING;
    threads[1].tid = 2; threads[1].state = READY;

    thread_t* next = mock_sched_yield(&threads[0]);
    assert(next->tid == 2);
    assert(threads[0].state == READY);
    assert(threads[1].state == RUNNING);

    printf("Scheduler property test passed!\n");
}

int main() {
    test_sched_property();
    return 0;
}
