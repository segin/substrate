#include <kern/sched.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/futex.h>
#include <pm/pm.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#ifndef HOST_TEST
#include <vm/vm_kmem.h>
#else
#include <stdlib.h>
#endif

thread_t threads[MAX_THREADS];
thread_t *current_thread = NULL;
static thread_t **thread_chunks = NULL;
static size_t thread_chunk_count = 0;
static size_t thread_chunk_capacity = 0;
static int next_tid = 1;
static spinlock_t tid_lock;


#include <kern/arch.h>
#include <arch/i386/percpu.h>

#define THREAD_SLOT_CHUNK_SIZE ((size_t)MAX_THREADS)

#ifdef HOST_TEST
static void *sched_table_alloc(size_t size) {
    return malloc(size);
}

static void sched_table_free(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}
#else
static void *sched_table_alloc(size_t size) {
    return kmalloc(size);
}

static void sched_table_free(void *ptr, size_t size) {
    kfree(ptr, size);
}
#endif

size_t sched_thread_slot_count(void) {
    return (size_t)MAX_THREADS + (thread_chunk_count * THREAD_SLOT_CHUNK_SIZE);
}

thread_t *sched_thread_slot(size_t index) {
    if (index < (size_t)MAX_THREADS) {
        return &threads[index];
    }

    index -= (size_t)MAX_THREADS;
    if (index < (thread_chunk_count * THREAD_SLOT_CHUNK_SIZE)) {
        size_t chunk_index = index / THREAD_SLOT_CHUNK_SIZE;
        size_t slot_index = index % THREAD_SLOT_CHUNK_SIZE;

        return &thread_chunks[chunk_index][slot_index];
    }

    return NULL;
}

static void sched_reset_free_slot(thread_t *thread) {
    if (!thread) {
        return;
    }

    memset(thread, 0, sizeof(*thread));
    thread->tid = -1;
    thread->state = THREAD_ZOMBIE;
}

static int sched_tid_in_use_locked(tid_t tid) {
    size_t slots = sched_thread_slot_count();

    for (size_t i = 0; i < slots; i++) {
        thread_t *thread = sched_thread_slot(i);
        if (thread && thread->tid == tid) {
            return 1;
        }
    }
    return 0;
}

static thread_t *sched_grow_slots_locked(void) {
    thread_t **new_chunks = thread_chunks;
    size_t new_chunk_capacity = thread_chunk_capacity;
    size_t old_bytes = thread_chunk_capacity * sizeof(*thread_chunks);
    size_t new_bytes;
    thread_t *new_chunk;

    if (thread_chunk_count == thread_chunk_capacity) {
        new_chunk_capacity = thread_chunk_capacity ? thread_chunk_capacity * 2U : 4U;
        new_bytes = new_chunk_capacity * sizeof(*new_chunks);
        new_chunks = sched_table_alloc(new_bytes);
        if (!new_chunks) {
            return NULL;
        }

        if (thread_chunks && old_bytes != 0U) {
            memcpy(new_chunks, thread_chunks, old_bytes);
            sched_table_free(thread_chunks, old_bytes);
        }
        memset(new_chunks + thread_chunk_count, 0,
               (new_chunk_capacity - thread_chunk_count) * sizeof(*new_chunks));
    }

    new_chunk = sched_table_alloc(THREAD_SLOT_CHUNK_SIZE * sizeof(*new_chunk));
    if (!new_chunk) {
        if (new_chunks != thread_chunks) {
            sched_table_free(new_chunks, new_chunk_capacity * sizeof(*new_chunks));
        }
        return NULL;
    }

    if (new_chunks != thread_chunks) {
        thread_chunks = new_chunks;
        thread_chunk_capacity = new_chunk_capacity;
    }

    for (size_t i = 0; i < THREAD_SLOT_CHUNK_SIZE; i++) {
        sched_reset_free_slot(&new_chunk[i]);
    }
    thread_chunks[thread_chunk_count++] = new_chunk;

    return &new_chunk[0];
}

static thread_t *sched_find_free_slot_locked(void) {
    size_t slots = sched_thread_slot_count();

    for (size_t i = 0; i < slots; i++) {
        thread_t *thread = sched_thread_slot(i);
        if (thread && thread->tid == -1) {
            return thread;
        }
    }

    return sched_grow_slots_locked();
}

static int sched_alloc_tid_locked(process_t *proc) {
    int start;
    int candidate;

    if (proc && proc->pid == 0 && !sched_tid_in_use_locked(0)) {
        return 0;
    }

    start = next_tid;
    if (start < 1 || start > SUBSTRATE_TID_MAX) {
        start = 1;
    }

    candidate = start;
    do {
        if (!sched_tid_in_use_locked(candidate)) {
            next_tid = (candidate == SUBSTRATE_TID_MAX) ? 1 : candidate + 1;
            return candidate;
        }

        candidate++;
        if (candidate > SUBSTRATE_TID_MAX) {
            candidate = 1;
        }
    } while (candidate != start);

    return -1;
}

static void sched_release_thread_storage(thread_t *t) {
    extern void kfree(void *ptr, size_t size);
    extern void pmm_free_block(void *p);
    extern void pmm_free_contiguous(void *p, size_t count);

    if (!t || !t->kstack_owned || t->kstack_base == 0) {
        return;
    }

    switch ((thread_kstack_type_t)t->kstack_type) {
    case THREAD_KSTACK_PMM_BLOCK:
        pmm_free_block((void *)(uintptr_t)t->kstack_base);
        break;
    case THREAD_KSTACK_PMM_CONTIG:
        pmm_free_contiguous((void *)(uintptr_t)t->kstack_base, t->kstack_units);
        break;
    case THREAD_KSTACK_KMALLOC:
        kfree((void *)(uintptr_t)t->kstack_base, t->kstack_units);
        break;
    case THREAD_KSTACK_NONE:
    default:
        break;
    }

    t->kstack_base = 0;
    t->kstack_units = 0;
    t->kstack_type = THREAD_KSTACK_NONE;
    t->kstack_owned = 0;
}



void sched_init_generic(void) {
    size_t slots;

    memset(threads, 0, sizeof(threads));
    next_tid = 1;
    spinlock_init(&tid_lock, "tid");
    
    // Initialize PM
    pm_init();

    // Zero out arrays
    slots = sched_thread_slot_count();
    for (size_t i = 0; i < slots; i++) {
        sched_reset_free_slot(sched_thread_slot(i));
    }
}

// Just sets up structures, doesn't touch hardware/stack
thread_t *sched_alloc_thread(process_t *proc) {
    thread_t *thread;
    int tid;

    spinlock_acquire(&tid_lock);
    thread = sched_find_free_slot_locked();
    if (!thread) {
        spinlock_release(&tid_lock);
        return NULL;
    }

    tid = sched_alloc_tid_locked(proc);
    if (tid < 0) {
        spinlock_release(&tid_lock);
        return NULL;
    }

    memset(thread, 0, sizeof(*thread));
    thread->tid = tid;
    thread->proc = proc;
    thread->state = THREAD_BLOCKED; // Set to BLOCKED until stack is ready
    thread->wait_chan = NULL;
    thread->wait_reason = NULL;
    thread->sleep_expiry = 0;
    thread->priority = current_thread ? current_thread->priority : 20;
    thread->base_priority = current_thread ? current_thread->base_priority : 20;
    thread->sched_class = current_thread ? current_thread->sched_class : SCHED_TIMESHARE;
    thread->kstack_base = 0;
    thread->kstack_units = 0;
    thread->kstack_type = THREAD_KSTACK_NONE;
    thread->kstack_owned = 0;
    thread->bound_cpu = -1;
    thread->exec_saved_bound_cpu = -1;
    thread->exec_pin_active = 0;
    thread->exec_saved_no_preempt = 0;
    
    // Initialize Signals
    thread->sig_mask = current_thread ? current_thread->sig_mask : 0;
    thread->sig_pending = 0;
    thread->sig_on_stack = 0;
    memset(&thread->sig_alt_stack, 0, sizeof(thread->sig_alt_stack));
    thread->in_syscall = 0;

    spinlock_release(&tid_lock);
    return thread;
}

static void sched_context_switch(thread_t *prev, thread_t *next) {
    if (prev && prev->state == THREAD_RUNNING) prev->state = THREAD_READY;

    // Handle thread exit notification after switching out but before changing address space
    if (prev && prev->state == THREAD_ZOMBIE && prev->exit_tid_ptr) {
        futex_wake_exited_thread(prev->exit_tid_ptr);
        prev->exit_tid_ptr = NULL;
    }

    next->state = THREAD_RUNNING;

    THIS_CPU()->current = next;
    current_thread = next;
    current_process = current_thread->proc;

    // Arch specific hooks
    if (current_thread->kstack_top) {
        arch_set_kernel_stack(current_thread->kstack_top);
    }

    // Activate new process's address space if switching processes
    if (prev && prev->proc != next->proc && next->proc && next->proc->pmap) {
        extern void pmap_activate(void *pmap);
        pmap_activate(next->proc->pmap);
    }

    if (prev && prev != next) {
        arch_switch_to(prev, next);
    }
}
void sched_yield(void) {
    if (!current_thread) return;

    proc_reap_autoreap_zombies();

    extern void kprint(const char *);
    extern int percpu_get_cpu_id(void);
    int cpu_id = percpu_get_cpu_id();

    thread_t *best_thread = NULL;
    int highest_prio = -1;
    sched_class_t best_class = SCHED_IDLE;

    /*
     * Round-robin index: start scanning from the thread AFTER the last
     * one we scheduled, so that equal-priority threads get fair turns.
     */
    static int rr_start = 0;
    size_t total_slots = sched_thread_slot_count();
    size_t best_index = 0;
    size_t start = 0;

    if (total_slots == 0) {
        return;
    }
    if ((size_t)rr_start >= total_slots) {
        rr_start = 0;
    }
    start = (size_t)rr_start;

    // Scan for best thread to run (Generic Policy)
    for (size_t n = 0; n < total_slots; n++) {
        size_t i = (start + n) % total_slots;
        thread_t *candidate = sched_thread_slot(i);

        if (!candidate || candidate->tid == -1 || candidate->state != THREAD_READY) continue;
        if (!sched_can_run_on_cpu(candidate, cpu_id)) continue;

        bool better = false;
        if (!best_thread) {
            better = true;
        } else if (candidate->sched_class < best_class) {
            better = true;
        } else if (candidate->sched_class == best_class) {
            if (candidate->priority > highest_prio) {
                better = true;
            }
        }

        if (better) {
            best_thread = candidate;
            best_index = i;
            highest_prio = candidate->priority;
            best_class = candidate->sched_class;
        }
    }

    if (best_thread == current_thread && current_thread && current_thread->state == THREAD_RUNNING) return;

    if (best_thread) {
        /* Advance round-robin start for next call */
        rr_start = (int)((best_index + 1U) % total_slots);
        sched_context_switch(current_thread, best_thread);
    }
}

void sched_switch(thread_t *next) {
    if (!next) return;
    if (!current_thread) return;
    if (next == current_thread && current_thread->state == THREAD_RUNNING) return;
    
    sched_context_switch(current_thread, next);
}

int sched_get_current_tid(void) {
    if (current_thread) return current_thread->tid;
    return -1;
}

thread_t *sched_get_thread(int tid) {
    if (tid < 0) return NULL;

    for (size_t i = 0; i < sched_thread_slot_count(); i++) {
        thread_t *thread = sched_thread_slot(i);
        if (thread && thread->tid == tid) {
            return thread;
        }
    }
    return NULL;
}

void sched_set_priority(int tid, sched_class_t cls, int prio) {
    thread_t *t = sched_get_thread(tid);
    if (!t) return;
    t->sched_class = cls;
    t->priority = prio;
    t->base_priority = prio;
}

void sched_sleep(void *chan) {
    if (!current_thread) return;
    
    // Check for pending signals before sleeping if interruptible
    if ((current_thread->flags & THREAD_F_INTERRUPTIBLE) && 
        (current_thread->sig_pending & ~current_thread->sig_mask)) {
        return;
    }

    current_thread->wait_chan = chan;
    current_thread->state = THREAD_BLOCKED;
    sched_yield();
}

int sched_sleep_until(void *chan, uint64_t deadline_tick) {
    if (!current_thread) return 0;

    current_thread->sleep_expiry = deadline_tick;
    current_thread->sleep_status = 0;

    sched_sleep(chan);

    current_thread->sleep_expiry = 0;
    return current_thread->sleep_status;
}

void sched_tick(void) {
    uint64_t now = get_ticks();

    // Perform periodic SMP load balancing
    extern void sched_periodic_balance(void);
    sched_periodic_balance();

    for (size_t i = 0; i < sched_thread_slot_count(); i++) {
        thread_t *thread = sched_thread_slot(i);

        if (thread &&
            thread->tid != -1 &&
            thread->state == THREAD_BLOCKED &&
            thread->sleep_expiry > 0 &&
            now >= thread->sleep_expiry) {

            thread->state = THREAD_READY;
            thread->wait_chan = NULL;
            thread->sleep_status = -ETIMEDOUT;
            thread->sleep_expiry = 0;
        }
    }
}

void sched_wakeup(void *chan) {
    sched_wakeup_n(chan, -1);
}

void sched_wakeup_n(void *chan, int n) {
    int woken = 0;

    for (size_t i = 0; i < sched_thread_slot_count(); i++) {
        thread_t *thread = sched_thread_slot(i);

        if (thread && thread->tid != -1 && thread->state == THREAD_BLOCKED && thread->wait_chan == chan) {
            thread->state = THREAD_READY;
            thread->wait_chan = NULL;
            woken++;
            if (n > 0 && woken >= n) break;
        }
    }

    if (woken > 0 && current_thread) {
        current_thread->needs_resched = 1;
    }
}

void sched_iterate_threads(void (*callback)(thread_t *t, void *arg), void *arg) {
    for (size_t i = 0; i < sched_thread_slot_count(); i++) {
        thread_t *thread = sched_thread_slot(i);
        if (thread && thread->tid != -1) {
            callback(thread, arg);
        }
    }
}

void sched_reap_process_threads(process_t *proc) {
    if (!proc) {
        return;
    }

    for (size_t i = 0; i < sched_thread_slot_count(); i++) {
        thread_t *thread = sched_thread_slot(i);

        if (!thread || thread->tid == -1 || thread->proc != proc) {
            continue;
        }

        /*
         * The process is already waitable and no thread in this group should
         * remain schedulable or visible after the parent reaps it.
         */
        sched_release_thread_storage(thread);
        sched_reset_free_slot(thread);
    }
}
