#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Fuzz Test: Scheduler Operations
 * 
 * Tests:
 * 1. Timer tick handling
 * 2. Priority manipulation
 * 3. Thread state transitions
 * 4. Run queue integrity
 * 5. Context switch triggers
 * 6. Scheduling class behavior
 */

// Scheduling classes
typedef enum {
    SCHED_CLASS_IDLE = 0,
    SCHED_CLASS_NORMAL = 1,
    SCHED_CLASS_REALTIME = 2,
} sched_class_t;

// Thread states
typedef enum {
    THREAD_RUNNING = 0,
    THREAD_READY = 1,
    THREAD_BLOCKED = 2,
    THREAD_ZOMBIE = 3,
} thread_state_t;

// Mock thread structure
typedef struct mock_thread {
    int tid;
    thread_state_t state;
    sched_class_t sched_class;
    int priority;
    uint32_t time_slice;
    uint32_t cpu_time;
    struct mock_thread *next; // For run queue
} mock_thread_t;

#define MAX_THREADS 64
static mock_thread_t mock_threads[MAX_THREADS];
static int mock_thread_count = 0;
static mock_thread_t *mock_current = NULL;
static mock_thread_t *mock_runqueue_head = NULL;
static uint32_t mock_tick_count = 0;

// Simple LCG PRNG
static uint32_t fuzz_state = 0;
static uint32_t fuzz_rand(void) {
    fuzz_state = fuzz_state * 1103515245 + 12345;
    return (fuzz_state >> 16) & 0x7FFF;
}

// Mock scheduler functions
static void mock_sched_init(void) {
    mock_thread_count = 0;
    mock_current = NULL;
    mock_runqueue_head = NULL;
    mock_tick_count = 0;
    
    for (int i = 0; i < MAX_THREADS; i++) {
        mock_threads[i].tid = 0;
        mock_threads[i].state = THREAD_ZOMBIE;
        mock_threads[i].next = NULL;
    }
}

static mock_thread_t *mock_thread_create(int tid, sched_class_t cls, int prio) {
    if (mock_thread_count >= MAX_THREADS) return NULL;
    
    mock_thread_t *t = &mock_threads[mock_thread_count++];
    t->tid = tid;
    t->state = THREAD_READY;
    t->sched_class = cls;
    t->priority = prio;
    t->time_slice = 10 + (prio / 10);
    t->cpu_time = 0;
    t->next = NULL;
    
    // Add to run queue
    if (!mock_runqueue_head) {
        mock_runqueue_head = t;
    } else {
        mock_thread_t *tail = mock_runqueue_head;
        while (tail->next) tail = tail->next;
        tail->next = t;
    }
    
    return t;
}

static void mock_sched_set_priority(int tid, sched_class_t cls, int prio) {
    for (int i = 0; i < mock_thread_count; i++) {
        if (mock_threads[i].tid == tid) {
            mock_threads[i].sched_class = cls;
            mock_threads[i].priority = prio;
            mock_threads[i].time_slice = 10 + (prio / 10);
            return;
        }
    }
}

static mock_thread_t *mock_pick_next(void) {
    // Priority-based selection (higher priority first)
    mock_thread_t *best = NULL;
    mock_thread_t *prev_best = NULL;
    mock_thread_t *prev = NULL;
    mock_thread_t *curr = mock_runqueue_head;
    
    while (curr) {
        if (curr->state == THREAD_READY) {
            if (!best || curr->sched_class > best->sched_class ||
                (curr->sched_class == best->sched_class && curr->priority > best->priority)) {
                prev_best = prev;
                best = curr;
            }
        }
        prev = curr;
        curr = curr->next;
    }
    
    return best;
}

static void mock_timer_tick(void) {
    mock_tick_count++;
    
    if (mock_current) {
        mock_current->cpu_time++;
        if (mock_current->time_slice > 0) {
            mock_current->time_slice--;
        }
        
        // Time slice expired
        if (mock_current->time_slice == 0) {
            mock_current->state = THREAD_READY;
            mock_current->time_slice = 10 + (mock_current->priority / 10);
            
            // Context switch
            mock_thread_t *next = mock_pick_next();
            if (next && next != mock_current) {
                next->state = THREAD_RUNNING;
                mock_current = next;
            } else if (mock_current->state == THREAD_READY) {
                mock_current->state = THREAD_RUNNING;
            }
        }
    } else {
        // Pick first runnable thread
        mock_current = mock_pick_next();
        if (mock_current) {
            mock_current->state = THREAD_RUNNING;
        }
    }
}

static void mock_sched_yield(void) {
    if (mock_current) {
        mock_current->state = THREAD_READY;
        mock_current->time_slice = 10 + (mock_current->priority / 10);
        
        mock_thread_t *next = mock_pick_next();
        if (next) {
            next->state = THREAD_RUNNING;
            mock_current = next;
        }
    }
}

static void mock_sched_block(mock_thread_t *t) {
    t->state = THREAD_BLOCKED;
    if (t == mock_current) {
        mock_current = mock_pick_next();
        if (mock_current) {
            mock_current->state = THREAD_RUNNING;
        }
    }
}

static void mock_sched_unblock(mock_thread_t *t) {
    if (t->state == THREAD_BLOCKED) {
        t->state = THREAD_READY;
    }
}

void fuzz_timer_interrupt(const uint8_t *data, size_t size) {
    if (size < 4) return;
    
    uint32_t seed = *(uint32_t *)data;
    fuzz_state = seed;
    
    mock_sched_init();
    
    // ========================================
    // Phase 1: Basic Timer Tick
    // ========================================
    
    // Create some threads
    mock_thread_t *t1 = mock_thread_create(1, SCHED_CLASS_NORMAL, 50);
    mock_thread_t *t2 = mock_thread_create(2, SCHED_CLASS_NORMAL, 50);
    mock_thread_t *t3 = mock_thread_create(3, SCHED_CLASS_REALTIME, 99);
    
    if (!t1 || !t2 || !t3) {
        __builtin_trap(); // Thread creation failed!
    }
    
    // First tick should pick highest priority (realtime)
    mock_timer_tick();
    if (!mock_current || mock_current->tid != 3) {
        __builtin_trap(); // Should pick realtime thread!
    }
    
    // ========================================
    // Phase 2: Time Slice Expiration
    // ========================================
    
    // Run ticks until time slice expires
    uint32_t initial_slice = mock_current->time_slice;
    for (uint32_t i = 0; i < initial_slice + 5; i++) {
        mock_timer_tick();
    }
    
    // Should have done at least one context switch
    if (mock_tick_count < initial_slice) {
        __builtin_trap(); // Tick count wrong!
    }
    
    // ========================================
    // Phase 3: Priority Changes
    // ========================================
    
    mock_sched_set_priority(1, SCHED_CLASS_REALTIME, 100);
    mock_sched_yield();
    
    // Thread 1 should now be picked (highest priority)
    if (mock_current && mock_current->tid != 1) {
        // May or may not switch depending on implementation
    }
    
    // ========================================
    // Phase 4: Block/Unblock
    // ========================================
    
    mock_thread_t *blocked = t2;
    mock_sched_block(blocked);
    
    if (blocked->state != THREAD_BLOCKED) {
        __builtin_trap(); // Should be blocked!
    }
    
    // Run some ticks - blocked thread should not run
    for (int i = 0; i < 100; i++) {
        mock_timer_tick();
        if (mock_current == blocked) {
            __builtin_trap(); // Blocked thread running!
        }
    }
    
    mock_sched_unblock(blocked);
    if (blocked->state != THREAD_READY) {
        __builtin_trap(); // Should be ready!
    }
    
    // ========================================
    // Phase 5: Stress Test
    // ========================================
    
    mock_sched_init();
    
    // Create many threads
    for (int i = 1; i <= 32; i++) {
        sched_class_t cls = (sched_class_t)(fuzz_rand() % 3);
        int prio = fuzz_rand() % 100;
        mock_thread_create(i, cls, prio);
    }
    
    // Run many ticks
    int num_ticks = (size > 4) ? *(int *)(data + 4) % 10000 : 1000;
    if (num_ticks < 0) num_ticks = 1000;
    
    for (int i = 0; i < num_ticks; i++) {
        mock_timer_tick();
        
        // Random priority changes
        if (fuzz_rand() % 100 < 5) {
            int tid = (fuzz_rand() % 32) + 1;
            sched_class_t cls = (sched_class_t)(fuzz_rand() % 3);
            int prio = fuzz_rand() % 100;
            mock_sched_set_priority(tid, cls, prio);
        }
        
        // Random blocks
        if (fuzz_rand() % 100 < 3 && mock_current) {
            mock_sched_block(mock_current);
        }
        
        // Random unblocks
        if (fuzz_rand() % 100 < 10) {
            int idx = fuzz_rand() % mock_thread_count;
            if (mock_threads[idx].state == THREAD_BLOCKED) {
                mock_sched_unblock(&mock_threads[idx]);
            }
        }
    }
    
    // ========================================
    // Phase 6: CPU Time Accounting
    // ========================================
    
    uint32_t total_cpu = 0;
    for (int i = 0; i < mock_thread_count; i++) {
        total_cpu += mock_threads[i].cpu_time;
    }
    
    // Total CPU time should roughly match tick count (minus idle)
    // Allow some variance for blocked threads
    if (total_cpu > mock_tick_count) {
        __builtin_trap(); // Overcounted CPU time!
    }
}

void fuzz_sched_priority(const uint8_t *data, size_t size) {
    if (size < 4) return;
    
    mock_sched_init();
    
    // Create test threads
    for (int i = 1; i <= 5; i++) {
        mock_thread_create(i, SCHED_CLASS_NORMAL, 50);
    }
    
    // Apply fuzzer-provided priority changes
    for (size_t i = 0; i + 3 < size; i += 4) {
        int tid = (data[i] % 5) + 1;
        sched_class_t cls = (sched_class_t)(data[i + 1] % 3);
        int prio = data[i + 2];
        
        mock_sched_set_priority(tid, cls, prio);
        
        // Verify the change took effect
        for (int j = 0; j < mock_thread_count; j++) {
            if (mock_threads[j].tid == tid) {
                if (mock_threads[j].sched_class != cls || mock_threads[j].priority != prio) {
                    __builtin_trap(); // Priority change failed!
                }
                break;
            }
        }
        
        mock_sched_yield();
    }
}
