/*
 * host_test_lockmgr.c - lockmgr shared/exclusive/upgrade/downgrade/nowait tests
 *
 * REQ-04-0195: Unit: lockmgr — shared/exclusive/upgrade/downgrade/nowait.
 *
 * Standalone host build: includes lockmgr.c directly with minimal stubs.
 * Single-threaded so all sched_yield() calls return immediately.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include <kern/sched.h>
#include <sys/errno.h>

/* ---- Kernel stubs -------------------------------------------------- */

thread_t  g_thread;
thread_t *current_thread  = &g_thread;
process_t *current_process = NULL;

void sched_yield(void) {}
int  sched_sleep_until(void *chan, uint64_t deadline)
    { (void)chan; (void)deadline; return 0; }
uint32_t get_hz(void)    { return 100; }
uint64_t get_ticks(void) { return 0; }

/* spinlock stubs */
void spinlock_init(spinlock_t *lock, const char *name)
    { lock->locked = 0; lock->cpu_id = 0; lock->name = name; }
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; }
void spinlock_release(spinlock_t *lock) { lock->locked = 0; }
bool spinlock_try_acquire(spinlock_t *lock)
    { if (lock->locked) return false; lock->locked = 1; return true; }
bool spinlock_is_held(spinlock_t *lock) { return lock->locked != 0; }

/* sleepq stubs */
void sleepq_add(void *chan, thread_t *t)  { (void)chan; (void)t; }
int  sleepq_wake_all(void *chan)          { (void)chan; return 0; }

/* turnstile stubs — no-op PI in single-threaded tests */
void turnstile_block(void *lockobj, struct thread *owner)
    { (void)lockobj; (void)owner; }
void turnstile_release(void *lockobj)
    { (void)lockobj; }

/* memory */
void *kmalloc(size_t size)          { return calloc(1, size); }
void  kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void panic(const char *msg)
    { fprintf(stderr, "PANIC: %s\n", msg); abort(); }

/* ---- lockmgr inclusion --------------------------------------------- */
/*
 * lockmgr only wakes sleepers when the queue says there are some; this test
 * drives the uncontended paths.  Signature per sys/kern/sleepq.h.
 */
int sleepq_has_waiters(void *chan) { (void)chan; return 0; }

#include "../../sys/kern/lockmgr.c"

/* ---- Helpers ------------------------------------------------------- */

#define PASS(name) printf("PASS: %s\n", name)

static struct lock g_lock;

static void setup(void)
{
    lockinit(&g_lock, 0, "test", 0);
    /* Ensure current_thread is properly initialized */
    g_thread.tid = 1;
    g_thread.state = THREAD_RUNNING;
    current_thread = &g_thread;
}

/* ---- Tests --------------------------------------------------------- */

static void test_shared_acquire_release(void)
{
    setup();
    int r = lockmgr(&g_lock, LK_SHARED, NULL);
    assert(r == 0);
    assert(g_lock.lk_sharecount == 1);
    assert(!(g_lock.lk_flags & LK_HAVE_EXCL));

    r = lockmgr(&g_lock, LK_RELEASE, NULL);
    assert(r == 0);
    assert(g_lock.lk_sharecount == 0);
    PASS("test_shared_acquire_release");
}

static void test_multiple_shared_locks(void)
{
    setup();
    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(g_lock.lk_sharecount == 3);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(g_lock.lk_sharecount == 2);
    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(g_lock.lk_sharecount == 0);
    PASS("test_multiple_shared_locks");
}

static void test_exclusive_acquire_release(void)
{
    setup();
    int r = lockmgr(&g_lock, LK_EXCLUSIVE, NULL);
    assert(r == 0);
    assert(g_lock.lk_flags & LK_HAVE_EXCL);
    assert(g_lock.lk_lockholder == current_thread);
    assert(g_lock.lk_exclusivecount == 1);

    r = lockmgr(&g_lock, LK_RELEASE, NULL);
    assert(r == 0);
    assert(!(g_lock.lk_flags & LK_HAVE_EXCL));
    assert(g_lock.lk_lockholder == NULL);
    PASS("test_exclusive_acquire_release");
}

static void test_recursive_exclusive(void)
{
    setup();
    assert(lockmgr(&g_lock, LK_EXCLUSIVE, NULL) == 0);
    assert(g_lock.lk_exclusivecount == 1);

    /* Second exclusive from same thread is recursive */
    assert(lockmgr(&g_lock, LK_EXCLUSIVE, NULL) == 0);
    assert(g_lock.lk_exclusivecount == 2);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(g_lock.lk_exclusivecount == 1);
    assert(g_lock.lk_flags & LK_HAVE_EXCL);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(g_lock.lk_exclusivecount == 0);
    assert(!(g_lock.lk_flags & LK_HAVE_EXCL));
    PASS("test_recursive_exclusive");
}

static void test_upgrade_shared_to_exclusive(void)
{
    setup();
    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(g_lock.lk_sharecount == 1);

    /* Upgrade: share count drops, exclusive is acquired */
    int r = lockmgr(&g_lock, LK_UPGRADE, NULL);
    assert(r == 0);
    assert(g_lock.lk_flags & LK_HAVE_EXCL);
    assert(g_lock.lk_sharecount == 0);
    assert(g_lock.lk_lockholder == current_thread);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    PASS("test_upgrade_shared_to_exclusive");
}

static void test_downgrade_exclusive_to_shared(void)
{
    setup();
    assert(lockmgr(&g_lock, LK_EXCLUSIVE, NULL) == 0);
    assert(g_lock.lk_flags & LK_HAVE_EXCL);

    int r = lockmgr(&g_lock, LK_DOWNGRADE, NULL);
    assert(r == 0);
    assert(!(g_lock.lk_flags & LK_HAVE_EXCL));
    assert(g_lock.lk_sharecount == 1);
    assert(g_lock.lk_lockholder == NULL);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(g_lock.lk_sharecount == 0);
    PASS("test_downgrade_exclusive_to_shared");
}

static void test_nowait_exclusive_fails_when_held(void)
{
    setup();
    /* Acquire exclusive first */
    assert(lockmgr(&g_lock, LK_EXCLUSIVE, NULL) == 0);

    /*
     * Simulate a second "thread" trying to get exclusive with NOWAIT.
     * We fake it by temporarily changing current_thread to a different one.
     */
    thread_t t2 = { .tid = 2, .state = THREAD_RUNNING };
    thread_t *saved = current_thread;
    current_thread = &t2;

    int r = lockmgr(&g_lock, LK_EXCLUSIVE | LK_NOWAIT, NULL);
    assert(r == EBUSY);

    current_thread = saved;
    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    PASS("test_nowait_exclusive_fails_when_held");
}

static void test_nowait_shared_fails_when_excl_held(void)
{
    setup();
    assert(lockmgr(&g_lock, LK_EXCLUSIVE, NULL) == 0);

    thread_t t2 = { .tid = 2, .state = THREAD_RUNNING };
    thread_t *saved = current_thread;
    current_thread = &t2;

    int r = lockmgr(&g_lock, LK_SHARED | LK_NOWAIT, NULL);
    assert(r == EBUSY);

    current_thread = saved;
    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    PASS("test_nowait_shared_fails_when_excl_held");
}

static void test_nowait_upgrade_fails_when_other_shared(void)
{
    setup();
    /* Two readers (same thread, simulating two references) */
    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(g_lock.lk_sharecount == 2);

    /*
     * Upgrade NOWAIT: share count would drop by one, but the remaining
     * reader blocks upgrade completion → EBUSY.
     */
    int r = lockmgr(&g_lock, LK_UPGRADE | LK_NOWAIT, NULL);
    assert(r == EBUSY);
    /* share count must be restored */
    assert(g_lock.lk_sharecount == 2);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    PASS("test_nowait_upgrade_fails_when_other_shared");
}

static void test_lockstatus(void)
{
    setup();
    /* Unlocked */
    assert(lockstatus(&g_lock) == 0);

    assert(lockmgr(&g_lock, LK_SHARED, NULL) == 0);
    assert(lockstatus(&g_lock) == LK_SHARED);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);

    assert(lockmgr(&g_lock, LK_EXCLUSIVE, NULL) == 0);
    assert(lockstatus(&g_lock) == LK_EXCLUSIVE);

    assert(lockmgr(&g_lock, LK_RELEASE, NULL) == 0);
    PASS("test_lockstatus");
}

/* ---- main ---------------------------------------------------------- */

int main(void)
{
    g_thread.tid = 1;
    g_thread.state = THREAD_RUNNING;
    current_thread = &g_thread;

    test_shared_acquire_release();
    test_multiple_shared_locks();
    test_exclusive_acquire_release();
    test_recursive_exclusive();
    test_upgrade_shared_to_exclusive();
    test_downgrade_exclusive_to_shared();
    test_nowait_exclusive_fails_when_held();
    test_nowait_shared_fails_when_excl_held();
    test_nowait_upgrade_fails_when_other_shared();
    test_lockstatus();

    printf("All lockmgr tests PASSED\n");
    return 0;
}
