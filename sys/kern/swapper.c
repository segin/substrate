/*
 * swapper.c - PID 0 idle/fallback helpers
 *
 * The live scheduler now seeds PID 0 and per-CPU idle threads directly.
 * This file provides the remaining idle helpers used by boot-time reclaim
 * fallback and host-side validation.
 */

#include <kern/sched.h>
#include <sys/proc.h>
#include <arch/i386/intr.h>
#include <stdint.h>
#include <string.h>

static process_t swapper_fallback_proc;
static thread_t swapper_fallback_thread;
static int swapper_fallback_ready = 0;
static volatile int idle_work_pending = 0;

extern void vm_pageout(void);

static void swapper_seed_fallback_context(void) {
    if (swapper_fallback_ready) {
        return;
    }

    memset(&swapper_fallback_proc, 0, sizeof(swapper_fallback_proc));
    swapper_fallback_proc.pid = 0;
    swapper_fallback_proc.ppid = 0;
    swapper_fallback_proc.is_kernel_task = 1;
    strncpy(swapper_fallback_proc.comm, "swapper", AC_COMM_LEN);
    swapper_fallback_proc.comm[AC_COMM_LEN - 1] = '\0';

    memset(&swapper_fallback_thread, 0, sizeof(swapper_fallback_thread));
    swapper_fallback_thread.tid = 0;
    swapper_fallback_thread.proc = &swapper_fallback_proc;
    swapper_fallback_thread.state = THREAD_RUNNING;
    swapper_fallback_thread.sched_class = SCHED_IDLE;
    swapper_fallback_thread.priority = 0;
    swapper_fallback_thread.base_priority = 0;

    swapper_fallback_ready = 1;
}

static int swapper_consume_work_request(void) {
    return __sync_lock_test_and_set(&idle_work_pending, 0);
}

void swapper_init(void) {
    swapper_seed_fallback_context();
    if (!current_thread) {
        current_thread = &swapper_fallback_thread;
    }
    if (!current_process) {
        current_process = current_thread->proc ? current_thread->proc : &swapper_fallback_proc;
    }
}

process_t *swapper_get_proc(void) {
    if (processes[0].pid == 0) {
        return &processes[0];
    }

    swapper_seed_fallback_context();
    return &swapper_fallback_proc;
}

thread_t *swapper_get_idle_thread(void) {
    if (current_thread && current_thread->sched_class == SCHED_IDLE) {
        return current_thread;
    }

    swapper_seed_fallback_context();
    return &swapper_fallback_thread;
}

void swapper_request_work(void) {
    __sync_lock_test_and_set(&idle_work_pending, 1);
}

void swapper_idle_loop(void) {
    for (;;) {
        uint32_t flags = intr_disable();

        if (swapper_consume_work_request()) {
            intr_restore(flags);
            vm_pageout();
            continue;
        }

        extern thread_t *sched_idle_balance(void);
        thread_t *next = sched_idle_balance();
        if (next) {
            intr_restore(flags);
            sched_switch(next);
            continue;
        }

        if (current_thread && current_thread->needs_resched) {
            intr_restore(flags);
            continue;
        }

        intr_restore(flags);
        wait_for_interrupt();
    }
}

thread_t *sched_ensure_context(void) {
    if (current_thread) {
        return current_thread;
    }

    swapper_init();
    return current_thread;
}

void sched_enter_critical(void) {
    (void)sched_ensure_context();
}

int sched_is_idle(void) {
    if (!current_thread) {
        return 1;
    }
    return current_thread->sched_class == SCHED_IDLE;
}
