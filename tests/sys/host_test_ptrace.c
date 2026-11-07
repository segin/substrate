/*
 * sys_ptrace(): a request on a tracee that is not stopped is refused
 * (ESRCH), but PTRACE_KILL; and a PEEK or POKE outside user space is
 * refused (EIO) before anything is faulted in or copied.  sys_ptrace()
 * read and wrote a running tracee, and handed any address -- the kernel
 * half, mapped in every pmap -- to vm_fault() and the pmap copy, which
 * alone stood between a tracer and kernel memory.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <machine/pmap.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/ptrace.h>
#include <vm/vm_fault.h>

process_t *current_process;
mutex_t proctree_lock;

static process_t tracer_proc;
static process_t tracee_proc;
static int copy_other_calls;
static int vm_fault_calls;
static int killed;

process_t *proc_find(int pid) {
    return pid == tracee_proc.pid ? &tracee_proc : NULL;
}

size_t pmap_copyin_other(pmap_t pmap, uintptr_t uva, void *dst, size_t len) {
    (void)pmap;
    copy_other_calls++;
    memset(dst, 0, len);
    memcpy(dst, &uva, len < sizeof(uva) ? len : sizeof(uva));
    return len;
}

size_t pmap_copyout_other(pmap_t pmap, uintptr_t uva, const void *src, size_t len) {
    (void)pmap;
    (void)uva;
    (void)src;
    copy_other_calls++;
    return len;
}

int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot) {
    (void)map;
    (void)va;
    (void)prot;
    vm_fault_calls++;
    return 0;
}

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void mutex_lock(mutex_t *m) {
    (void)m;
}

void mutex_unlock(mutex_t *m) {
    (void)m;
}

void psignal(struct process *p, int sig) {
    (void)p;
    if (sig == SIGKILL) {
        killed = 1;
    }
}

void signal_resume_process_threads(process_t *p) {
    (void)p;
}

void *ptrace_user_frame(struct process *p) {
    (void)p;
    return NULL;
}

thread_t *ptrace_user_thread(struct process *p) {
    (void)p;
    return NULL;
}

int fpu_thread_get_regs(struct thread *t, int layout, void *kbuf, size_t len) {
    (void)t;
    (void)layout;
    (void)kbuf;
    (void)len;
    return -EIO;
}

int fpu_thread_set_regs(struct thread *t, int layout, const void *kbuf, size_t len) {
    (void)t;
    (void)layout;
    (void)kbuf;
    (void)len;
    return -EIO;
}

int fpu_xstate_info(uint64_t *xcr0, uint32_t *len) {
    (void)xcr0;
    (void)len;
    return -EIO;
}

#include "../../sys/kern/ptrace.c"

static void reset(int stopped) {
    memset(&tracer_proc, 0, sizeof(tracer_proc));
    memset(&tracee_proc, 0, sizeof(tracee_proc));
    tracer_proc.pid = 100;
    tracee_proc.pid = 200;
    tracee_proc.p_tracer = &tracer_proc;
    tracee_proc.p_flag = P_TRACED;
    tracee_proc.state = stopped ? SSTOP : SRUN;
    current_process = &tracer_proc;
    copy_other_calls = 0;
    vm_fault_calls = 0;
    killed = 0;
}

int main(void) {
    /* PEEK with data 0 is the classic form: the word is the return value,
     * here (pmap_copyin_other's stub) the address read */

    /* a running tracee: nothing read or written, but it can be killed */
    reset(0);
    assert(sys_ptrace(PTRACE_PEEKDATA, 200, 0x1000, 0) == -ESRCH);
    assert(sys_ptrace(PTRACE_POKEDATA, 200, 0x1000, 7) == -ESRCH);
    assert(sys_ptrace(PTRACE_CONT, 200, 0, 0) == -ESRCH);
    assert(copy_other_calls == 0 && vm_fault_calls == 0);
    assert(sys_ptrace(PTRACE_KILL, 200, 0, 0) == 0);
    assert(killed == 1);

    /* a stopped tracee: user memory yes, the kernel half no */
    reset(1);
    assert(sys_ptrace(PTRACE_PEEKDATA, 200, 0x1000, 0) == 0x1000);
    assert(copy_other_calls == 1);
    reset(1);
    assert(sys_ptrace(PTRACE_PEEKDATA, 200, (int)0xC0001000U, 0) == -EIO);
    assert(sys_ptrace(PTRACE_POKEDATA, 200, (int)0xC0001000U, 7) == -EIO);
    assert(sys_ptrace(PTRACE_PEEKDATA, 200, (int)0xBFFFFFFEU, 0) == -EIO);
    assert(copy_other_calls == 0 && vm_fault_calls == 0);

    printf("host_test_ptrace: PASS\n");
    return 0;
}
