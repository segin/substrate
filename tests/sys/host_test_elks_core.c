#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/core.h>
#include <sys/proc.h>
#include <sys/ldt.h>
#include <sys/signal.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <arch/i386/idt.h>
#include <exec/formats/elks_aout.h>
#include <exec/perso/personality.h>
#include <pm/pm.h>
#include <kern/sched.h>

thread_t threads[MAX_THREADS];
process_t processes[MAX_PROCS];
process_t *current_process;
thread_t *current_thread;
mutex_t proctree_lock;

static int proc_exit_called;
static int proc_exit_status;
static char last_log[160];

void mutex_init(mutex_t *m, const char *name) { (void)m; (void)name; }
void mutex_lock(mutex_t *m) { (void)m; }
void mutex_unlock(mutex_t *m) { (void)m; }
void sched_yield(void) {}
void sched_sleep(void *chan) { (void)chan; }
void sched_wakeup(void *chan) { (void)chan; }
int sleepq_remove_thread(thread_t *t) { (void)t; return 0; }
void panic(const char *msg) { (void)msg; assert(!"panic"); }
uint64_t get_ticks(void) { return 0; }
uint32_t get_hz(void) { return 128; }
int copyin(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
int copyout(const void *src, void *dst, size_t size) { memcpy(dst, src, size); return 0; }
struct personality *perso_lookup(int id) { (void)id; return NULL; }
const char *perso_name(int id) { (void)id; return "test"; }
struct pgrp *pgrp_find(int pgid) { (void)pgid; return NULL; }
void pgrp_signal(struct pgrp *pgrp, int sig) { (void)pgrp; (void)sig; }
int pgrp_is_orphaned(struct pgrp *pgrp) { (void)pgrp; return 0; }
void sendsig(sig_t handler, int sig, uint32_t mask, uint32_t flags, registers_t *regs) {
    (void)handler; (void)sig; (void)mask; (void)flags; (void)regs;
}
void proc_exit(int status) { proc_exit_called = 1; proc_exit_status = status; }
void kprint(const char *msg) {
    strncpy(last_log, msg ? msg : "", sizeof(last_log) - 1U);
    last_log[sizeof(last_log) - 1U] = '\0';
}
const uint8_t sigprop[NSIG] = {
    [SIGSEGV] = SA_KILL | SA_CORE,
};

#include "../../sys/kern/core.c"
#include "../../sys/kern/signal.c"

static void setup_elks_process(process_t *proc, thread_t *thread, gdt_entry_t *ldt) {
    uintptr_t base = 0x00120000U;

    memset(proc, 0, sizeof(*proc));
    memset(thread, 0, sizeof(*thread));
    memset(ldt, 0, sizeof(gdt_entry_t) * 4U);

    proc->pid = 77;
    proc->perso_id = PERS_ELKS;
    proc->bitness = BITNESS_16;
    strcpy(proc->comm, "hello");
    strcpy(proc->exec_path, "/perso/elks/bin/hello");
    proc->rlimits[RLIMIT_CORE].rlim_cur = RLIM_INFINITY;
    proc->rlimits[RLIMIT_CORE].rlim_max = RLIM_INFINITY;
    proc->ldt = ldt;
    proc->ldt_entry_count = 4;

    thread->tid = 77;
    thread->proc = proc;
    thread->state = THREAD_RUNNING;
    thread->trap_signo = SIGSEGV;
    thread->trap_code = SEGV_MAPERR;
    thread->trap_addr = 0x4321U;

    ldt[ELKS_LDT_CS_INDEX].limit_low = 0xFFFFU;
    ldt[ELKS_LDT_CS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_CS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_CS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_CS_INDEX].access = 0xFAU;

    ldt[ELKS_LDT_DS_INDEX].limit_low = 0xEFFFU;
    ldt[ELKS_LDT_DS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_DS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_DS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_DS_INDEX].access = 0xF2U;

    ldt[ELKS_LDT_SS_INDEX].limit_low = 0x0FFFU;
    ldt[ELKS_LDT_SS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_SS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_SS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_SS_INDEX].access = 0xF2U;
}

int main(void) {
    process_t *proc = &processes[0];
    thread_t *thread = &threads[0];
    gdt_entry_t ldt[4];
    registers_t regs;
    const struct core_record *rec;

    memset(processes, 0, sizeof(processes));
    memset(threads, 0, sizeof(threads));
    memset(&regs, 0, sizeof(regs));
    memset(last_log, 0, sizeof(last_log));
    proc_exit_called = 0;
    proc_exit_status = 0;

    setup_elks_process(proc, thread, ldt);
    current_process = proc;
    current_thread = thread;

    regs.eax = 0x11U;
    regs.ebx = 0x22U;
    regs.ecx = 0x33U;
    regs.edx = 0x44U;
    regs.esi = 0x55U;
    regs.edi = 0x66U;
    regs.ebp = 0x7777U;
    regs.esp = 0x8888U;
    regs.ds = (uint32_t)((ELKS_LDT_DS_INDEX << 3) | 4U | 3U);
    regs.es = regs.ds;
    regs.fs = regs.ds;
    regs.gs = regs.ds;
    regs.cs = (uint32_t)((ELKS_LDT_CS_INDEX << 3) | 4U | 3U);
    regs.ss = (uint32_t)((ELKS_LDT_SS_INDEX << 3) | 4U | 3U);
    regs.eip = 0x1234U;
    regs.useresp = 0x0FF0U;
    regs.eflags = 0x202U;

    core_capture_trapframe(proc, &regs);
    sigexit(proc, SIGSEGV);

    assert(proc_exit_called == 1);
    assert(WIFSIGNALED(proc_exit_status));
    assert(WTERMSIG(proc_exit_status) == SIGSEGV);
    assert(WCOREDUMP(proc_exit_status));

    rec = core_last_record();
    assert(rec->valid);
    assert(rec->pid == proc->pid);
    assert(rec->perso_id == PERS_ELKS);
    assert(rec->bitness == BITNESS_16);
    assert(rec->signal == SIGSEGV);
    assert(rec->trap_code == SEGV_MAPERR);
    assert(rec->trap_addr == 0x4321U);
    assert(strcmp(rec->exec_path, "/perso/elks/bin/hello") == 0);
    assert(rec->has_regs);
    assert(rec->regs.cs == regs.cs);
    assert(rec->regs.ss == regs.ss);
    assert(rec->regs.eip == regs.eip);
    assert(rec->regs.useresp == regs.useresp);
    assert(rec->segment_count >= 3);
    assert(rec->segments[ELKS_LDT_CS_INDEX].selector == regs.cs);
    assert(rec->segments[ELKS_LDT_DS_INDEX].selector == regs.ds);
    assert(rec->segments[ELKS_LDT_SS_INDEX].selector == regs.ss);
    assert(strstr(last_log, "CORE: captured crash state") != NULL);

    puts("host_test_elks_core: OK");
    return 0;
}
