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

void pm_init(void) {}
void proc_reap_autoreap_zombies(void) {}
void sched_periodic_balance(void) {}
int sched_can_run_on_cpu(thread_t *t, int cpu_id) { (void)t; (void)cpu_id; return 1; }
void arch_set_kernel_stack(uintptr_t stack) { (void)stack; }
void arch_switch_to(thread_t *prev, thread_t *next) { (void)prev; (void)next; }
void pmap_activate(void *pmap) { (void)pmap; }
void futex_wake_exited_thread(int *uaddr) { (void)uaddr; }
uint64_t get_ticks(void) { return 0; }

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

    assert(block_thread->tid == -1);
    assert(contig_thread->tid == -1);
    assert(kmem_thread->tid == -1);
    assert(borrowed_thread->tid == -1);
}

int main(void) {
    test_reap_process_threads_frees_owned_stacks();
    puts("host_test_sched_reap: PASS");
    return 0;
}
