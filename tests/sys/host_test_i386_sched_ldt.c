#include <stdio.h>
#include <string.h>

#include <kern/sched.h>
#include <sys/smp.h>
#include <pm/pm.h>
#include <arch/i386/percpu.h>
#include <arch/i386/pmap.h>

process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
fs_node_t *fs_root;

static struct percpu_data cpu0;
static process_t *last_ldt_proc;
static pmap_t last_pmap;
static thread_t *switched_prev;
static thread_t *switched_next;

struct percpu_data *percpu_get(void) { return &cpu0; }
struct percpu_data *percpu_get_cpu(int cpu_id) { (void)cpu_id; return &cpu0; }
int percpu_get_cpu_id(void) { return 0; }
int smp_get_cpu_count(void) { return 1; }

void ldt_activate(process_t *proc) { last_ldt_proc = proc; }
void pmap_activate(pmap_t pmap) { last_pmap = pmap; }
void switch_to(thread_t *prev, thread_t *next) { switched_prev = prev; switched_next = next; }
void set_kernel_stack(uint32_t stack) { (void)stack; }
uint32_t get_time(void) { return 0; }
thread_t *sched_alloc_thread(process_t *proc) { static thread_t t; memset(&t, 0, sizeof(t)); t.proc = proc; return &t; }
void sched_init_generic(void) {}
void sched_smp_init(int cpus) { (void)cpus; }
void sched_yield(void) {}
void *pmm_alloc_contiguous(size_t pages) { (void)pages; return (void *)0x00100000U; }
void fork_child_return(void) {}
pmap_t pmap_kernel(void) { return (pmap_t)0xCAFEB000U; }

#include "../../sys/arch/i386/sched.c"

int main(void) {
    process_t proc_a;
    process_t proc_b;
    thread_t prev;
    thread_t next_same;
    thread_t next_other;

    memset(&cpu0, 0, sizeof(cpu0));
    memset(&proc_a, 0, sizeof(proc_a));
    memset(&proc_b, 0, sizeof(proc_b));
    memset(&prev, 0, sizeof(prev));
    memset(&next_same, 0, sizeof(next_same));
    memset(&next_other, 0, sizeof(next_other));

    proc_a.pmap = (pmap_t)0x11111000U;
    proc_b.pmap = (pmap_t)0x22222000U;

    prev.proc = &proc_a;
    next_same.proc = &proc_a;
    next_other.proc = &proc_b;

    last_ldt_proc = (process_t *)0x1;
    last_pmap = NULL;
    switched_prev = NULL;
    switched_next = NULL;
    arch_switch_to(&prev, &next_same);
    if (last_pmap != proc_a.pmap) {
        fprintf(stderr, "FAIL: same-process switch did not activate next pmap\n");
        return 1;
    }
    if (last_ldt_proc != (process_t *)0x1) {
        fprintf(stderr, "FAIL: same-process switch unnecessarily reloaded LDT\n");
        return 1;
    }
    if (switched_prev != &prev || switched_next != &next_same) {
        fprintf(stderr, "FAIL: same-process switch did not invoke switch_to\n");
        return 1;
    }

    last_ldt_proc = NULL;
    last_pmap = NULL;
    switched_prev = NULL;
    switched_next = NULL;
    arch_switch_to(&prev, &next_other);
    if (last_pmap != proc_b.pmap) {
        fprintf(stderr, "FAIL: process switch did not activate next pmap\n");
        return 1;
    }
    if (last_ldt_proc != &proc_b) {
        fprintf(stderr, "FAIL: process switch did not activate next LDT\n");
        return 1;
    }
    if (switched_prev != &prev || switched_next != &next_other) {
        fprintf(stderr, "FAIL: process switch did not invoke switch_to\n");
        return 1;
    }

    puts("host_test_i386_sched_ldt: ok");
    return 0;
}
