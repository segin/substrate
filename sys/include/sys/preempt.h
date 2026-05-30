#ifndef _SYS_PREEMPT_H
#define _SYS_PREEMPT_H

#include <stdint.h>

/*
 * Kernel preemption control.
 *
 * Every spinlock acquire raises the current thread's preempt_count and
 * every release lowers it.  A timer interrupt that lands in kernel mode
 * only performs an involuntary context switch when preempt_count == 0 --
 * i.e. when the interrupted context holds no spinlock and is therefore
 * safe to switch away from.  preempt_disable() must be paired with
 * preempt_enable_noresched(), and disable must precede the spinlock's
 * atomic acquire so there is never a lock-held-but-count-0 window.
 *
 * These operate on `current_thread`; they are no-ops before the
 * scheduler is up (current_thread == NULL).
 */
void     preempt_disable(void);
void     preempt_enable_noresched(void);
uint32_t preempt_count_get(void);

#endif /* _SYS_PREEMPT_H */
