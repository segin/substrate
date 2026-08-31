/*
 * Shared kernel stubs for the standalone signal host tests.
 *
 * host_test_signal_restart.c and host_test_signal_integration.c each build
 * sys/kern/signal.c natively and link it alone -- no mocks.o, no kernel.
 * signal.c walks the process and thread registries, takes spinlocks, checks
 * the command line and clears POSIX timers, none of which those tests
 * exercise or supply.
 *
 * Signatures are copied from the declaring headers, named in the trailing
 * comment on each.  A stub that disagrees with its header is a conflicting
 * definition of the thing it stands in for -- keep them matched.
 *
 * proc_first/proc_next and thread_first/thread_next are weak: FOREACH_THREAD() is built on them, so a
 * test that actually needs the registry walked -- rather than merely linked --
 * overrides them with a walk over its own mock table.
 */

#include <stddef.h>
#include <stdint.h>

#include "../../sys/include/sys/lock.h"
#include "../../sys/include/sys/proc.h"
#include "../../sys/kern/sched.h"
#include "../../sys/pm/pm.h"

int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }   /* sys/kern/cmdline.h */
int cmdline_has(const char *key) { (void)key; return 0; }   /* sys/kern/cmdline.h */
__attribute__((weak)) process_t *proc_first(void) { return NULL; }   /* sys/pm/pm.h */
__attribute__((weak)) process_t *proc_next(process_t *p) { (void)p; return NULL; }   /* sys/pm/pm.h */
void proc_ptimers_clear(struct process *p) { (void)p; }   /* sys/kern/time.h */
void proc_registry_lock(void) { }   /* sys/pm/pm.h */
void proc_registry_unlock(void) { }   /* sys/pm/pm.h */
void ptimer_signal_delivered(struct process *p, int sig) { (void)p; (void)sig; }   /* sys/kern/time.h */
void sched_park_if_reboot_frozen(void) { }   /* sys/kern/sched.h */
void spinlock_acquire(spinlock_t *lock) { (void)lock; }   /* sys/include/sys/lock.h */
void spinlock_release(spinlock_t *lock) { (void)lock; }   /* sys/include/sys/lock.h */
__attribute__((weak)) thread_t *thread_first(void) { return NULL; }   /* sys/kern/sched.h */
__attribute__((weak)) thread_t *thread_next(thread_t *t) { (void)t; return NULL; }   /* sys/kern/sched.h */
unsigned long thread_registry_lock(void) { return 0; }   /* sys/kern/sched.h */
void thread_registry_unlock(unsigned long flags) { (void)flags; }   /* sys/kern/sched.h */
