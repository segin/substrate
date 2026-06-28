#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/sched.h>
#include <pm/pm.h>
#include <arch/i386/percpu.h>

process_t *current_process;

static struct percpu_data cpu0;
static int kfree_calls;
static int pmm_free_block_calls;
static int pmm_free_contig_calls;
static void *last_kfree_ptr;
static size_t last_kfree_size;
static void *last_pmm_block_ptr;
static void *last_pmm_contig_ptr;
static size_t last_pmm_contig_count;

void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
void pm_init(void) {}
void proc_reap_autoreap_zombies(void) {}
void sched_periodic_balance(void) {}
int sched_can_run_on_cpu(thread_t *t, int cpu_id) { (void)t; (void)cpu_id; return 1; }
void arch_set_kernel_stack(uintptr_t stack) { (void)stack; }
void arch_switch_to(thread_t *prev, thread_t *next) { (void)prev; (void)next; }
void pmap_activate(void *pmap) { (void)pmap; }
void futex_wake_exited_thread(int *uaddr) { (void)uaddr; }
uint64_t get_ticks(void) { return 0; }
uint32_t get_hz(void) { return 100; }

struct percpu_data *percpu_get(void) { return &cpu0; }
struct percpu_data *percpu_get_cpu(int cpu_id) { (void)cpu_id; return &cpu0; }
void percpu_init_cpu(int cpu_id) { (void)cpu_id; }
void percpu_init(void) {}
int percpu_get_cpu_id(void) { return 0; }

void kfree(void *ptr, size_t size) {
    kfree_calls++;
    last_kfree_ptr = ptr;
    last_kfree_size = size;
}

void pmm_free_block(void *p) {
    pmm_free_block_calls++;
    last_pmm_block_ptr = p;
}

void pmm_free_contiguous(void *p, size_t count) {
    pmm_free_contig_calls++;
    last_pmm_contig_ptr = p;
    last_pmm_contig_count = count;
}

/* The real <arch/i386/intr.h> uses 32-bit inline asm (pushfl/popfl) that
 * won't assemble in this 64-bit host build.  Pre-claim its include guard
 * and supply host-safe no-op intr_disable/intr_restore (the only two
 * symbols sched.c needs from it). */
#define _ARCH_I386_INTR_H
static inline uint32_t intr_disable(void) { return 0; }
static inline void intr_restore(uint32_t f) { (void)f; }

#include "../../sys/pm/sched.c"

static void reset_env(void) {
    memset(&cpu0, 0, sizeof(cpu0));
    current_process = NULL;
    kfree_calls = 0;
    pmm_free_block_calls = 0;
    pmm_free_contig_calls = 0;
    last_kfree_ptr = NULL;
    last_kfree_size = 0;
    last_pmm_block_ptr = NULL;
    last_pmm_contig_ptr = NULL;
    last_pmm_contig_count = 0;
    sched_init_generic();
}

static void test_reap_process_threads_frees_owned_stacks(void) {
    process_t proc;
    thread_t *block_thread;
    thread_t *contig_thread;
    thread_t *kmem_thread;
    thread_t *borrowed_thread;

    reset_env();
    memset(&proc, 0, sizeof(proc));
    proc.pid = 41;

    block_thread = sched_alloc_thread(&proc);
    contig_thread = sched_alloc_thread(&proc);
    kmem_thread = sched_alloc_thread(&proc);
    borrowed_thread = sched_alloc_thread(&proc);

    assert(block_thread != NULL);
    assert(contig_thread != NULL);
    assert(kmem_thread != NULL);
    assert(borrowed_thread != NULL);

    block_thread->kstack_owned = 1;
    block_thread->kstack_type = THREAD_KSTACK_PMM_BLOCK;
    block_thread->kstack_base = 0xC0100000u;
    block_thread->kstack_units = 1;

    contig_thread->kstack_owned = 1;
    contig_thread->kstack_type = THREAD_KSTACK_PMM_CONTIG;
    contig_thread->kstack_base = 0xC0200000u;
    contig_thread->kstack_units = 2;

    kmem_thread->kstack_owned = 1;
    kmem_thread->kstack_type = THREAD_KSTACK_KMALLOC;
    kmem_thread->kstack_base = 0xC0300000u;
    kmem_thread->kstack_units = 4096;

    borrowed_thread->kstack_owned = 0;
    borrowed_thread->kstack_type = THREAD_KSTACK_NONE;
    borrowed_thread->kstack_base = 0xC0400000u;

    sched_reap_process_threads(&proc);

    assert(pmm_free_block_calls == 1);
    assert(last_pmm_block_ptr == (void *)(uintptr_t)0xC0100000u);
    assert(pmm_free_contig_calls == 1);
    assert(last_pmm_contig_ptr == (void *)(uintptr_t)0xC0200000u);
    assert(last_pmm_contig_count == 2);
    assert(kfree_calls == 1);
    assert(last_kfree_ptr == (void *)(uintptr_t)0xC0300000u);
    assert(last_kfree_size == 4096);

    /* The kernel now allocates threads dynamically and frees them on
     * reap (sched_storage_free) rather than marking a static-pool slot
     * with tid == -1.  So instead of inspecting the (now-freed) thread
     * structs, verify every thread owned by the process was unlinked
     * from the live allthread list. */
    for (thread_t *t = thread_first(); t; t = thread_next(t)) {
        assert(t->proc != &proc);
    }
}

static void test_sched_alloc_thread_wraps_tid_at_arch_limit(void) {
    process_t kernel_proc;
    process_t user_proc;
    thread_t *idle_thread;
    thread_t *user_thread;
    thread_t *wrapped_thread;

    reset_env();

    memset(&kernel_proc, 0, sizeof(kernel_proc));
    kernel_proc.pid = 0;

    memset(&user_proc, 0, sizeof(user_proc));
    user_proc.pid = 42;

    idle_thread = sched_alloc_thread(&kernel_proc);
    assert(idle_thread != NULL);
    assert(idle_thread->tid == 0);

    next_tid = SUBSTRATE_TID_MAX;

    user_thread = sched_alloc_thread(&user_proc);
    wrapped_thread = sched_alloc_thread(&user_proc);

    assert(user_thread != NULL);
    assert(wrapped_thread != NULL);
    assert(user_thread->tid == SUBSTRATE_TID_MAX);
    assert(wrapped_thread->tid == 1);
    assert(sched_get_thread(0) == idle_thread);
    assert(sched_get_thread(SUBSTRATE_TID_MAX) == user_thread);
    assert(sched_get_thread(1) == wrapped_thread);
}

int main(void) {
    test_reap_process_threads_frees_owned_stacks();
    test_sched_alloc_thread_wraps_tid_at_arch_limit();
    puts("host_test_sched_reap: PASS");
    return 0;
}
