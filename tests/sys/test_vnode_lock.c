#include <vfs/vnode.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <sys/kthread.h>
#include <kern/sleepq.h>
#include <sys/lock.h>

static struct vnode *test_vp;
static volatile int helper_started = 0;
static volatile int helper_done = 0;
static volatile int shared_count = 0;

/* --- Exclusive Blocking Test --- */

static void helper_lock_thread(void *arg) {
    (void)arg;
    int error;

    kprint("Helper: Trying to lock vnode (Exclusive)...\n");
    error = vn_lock(test_vp, LK_EXCLUSIVE);
    if (error) {
        kprintf("Helper: vn_lock failed: %d\n", error);
        return;
    }
    kprint("Helper: Got Exclusive lock! Sleeping...\n");

    helper_started = 1;

    /* Simulate work - yield loop for delay */
    for (volatile int i = 0; i < 10000000; i++) {
        __asm__ volatile("nop");
    }

    kprint("Helper: Unlocking vnode...\n");
    vn_unlock(test_vp);
    helper_done = 1;
    kprint("Helper: Done.\n");

    kthread_exit();
}

static void test_exclusive_blocking(void) {
    kprint("\n--- Test: Exclusive Blocking ---\n");

    helper_started = 0;
    helper_done = 0;

    /* Reset vnode state if reused, or assume clean */
    /* Ensure unlocked */
    if (vn_islocked(test_vp)) vn_unlock(test_vp);

    /* Spawn helper thread */
    thread_t *t;
    kthread_create(helper_lock_thread, NULL, &t, "lock_helper");

    /* Wait for helper to acquire lock */
    while (!helper_started) {
        sched_yield();
    }

    kprint("Main: Helper has lock. Attempting to acquire Exclusive lock (should block)...\n");

    /* This should block until helper releases lock */
    int error = vn_lock(test_vp, LK_EXCLUSIVE);

    if (error) {
        kprintf("FAIL: Main vn_lock failed: %d\n", error);
    } else {
        kprint("Main: Acquired lock!\n");

        if (!helper_done) {
            kprint("FAIL: Acquired lock before helper finished! (Locking is broken/non-blocking)\n");
        } else {
            kprint("PASS: Acquired lock after helper finished (Blocking worked)\n");
        }
        vn_unlock(test_vp);
    }
}

/* --- Concurrent Shared Locking Test --- */

static void helper_shared_thread(void *arg) {
    (void)arg;
    int error;

    kprint("Helper: Trying to lock vnode (Shared)...\n");
    error = vn_lock(test_vp, LK_SHARED);
    if (error) {
        kprintf("Helper: vn_lock shared failed: %d\n", error);
        return;
    }

    /* Increment shared count to signal we hold it */
    __atomic_fetch_add(&shared_count, 1, __ATOMIC_SEQ_CST);
    kprint("Helper: Got Shared lock! Holding...\n");

    /* Wait until main thread signals to release (by setting helper_done=1) */
    while (!helper_done) {
        sched_yield();
    }

    kprint("Helper: Unlocking Shared...\n");
    vn_unlock(test_vp);
    __atomic_fetch_sub(&shared_count, 1, __ATOMIC_SEQ_CST);

    kthread_exit();
}

static void test_concurrent_shared(void) {
    kprint("\n--- Test: Concurrent Shared Locking ---\n");

    helper_started = 0;
    helper_done = 0;
    shared_count = 0;

    if (vn_islocked(test_vp)) vn_unlock(test_vp);

    /* Spawn helper thread */
    thread_t *t;
    kthread_create(helper_shared_thread, NULL, &t, "shared_helper");

    /* Wait for helper to acquire shared lock */
    while (shared_count == 0) {
        sched_yield();
    }

    kprint("Main: Helper has shared lock. Attempting to acquire Shared lock (should succeed immediately)...\n");

    int error = vn_lock(test_vp, LK_SHARED);
    if (error) {
        kprintf("FAIL: Main vn_lock shared failed: %d\n", error);
    } else {
        kprint("Main: Acquired Shared lock!\n");

        /* Check that shared_count is still >= 1 (helper still holds it) */
        if (shared_count < 1) {
             kprint("FAIL: Helper lost lock? shared_count < 1\n");
        } else {
             kprint("PASS: Concurrent Shared locks held.\n");
        }

        /* Signal helper to release */
        helper_done = 1;

        vn_unlock(test_vp);
    }

    /* Wait for helper to finish */
    while (shared_count > 0) {
        sched_yield();
    }
}

/* --- Shared Blocks Exclusive Test --- */

static void helper_shared_block_excl_thread(void *arg) {
    (void)arg;
    int error;

    kprint("Helper: Taking Shared lock...\n");
    error = vn_lock(test_vp, LK_SHARED);
    if (error) return;

    helper_started = 1;

    /* Hold for a bit */
    for (volatile int i = 0; i < 10000000; i++) {
        __asm__ volatile("nop");
    }

    kprint("Helper: Releasing Shared lock...\n");
    vn_unlock(test_vp);
    helper_done = 1;
    kthread_exit();
}

static void test_shared_blocks_exclusive(void) {
    kprint("\n--- Test: Shared Blocks Exclusive ---\n");

    helper_started = 0;
    helper_done = 0;
    if (vn_islocked(test_vp)) vn_unlock(test_vp);

    thread_t *t;
    kthread_create(helper_shared_block_excl_thread, NULL, &t, "shared_blocker");

    while (!helper_started) sched_yield();

    kprint("Main: Helper has Shared. Attempting Exclusive (should block)...\n");

    int error = vn_lock(test_vp, LK_EXCLUSIVE);
    if (error) {
        kprintf("FAIL: vn_lock excl failed: %d\n", error);
    } else {
        if (!helper_done) {
             kprint("FAIL: Acquired Exclusive while Shared held! (Broken semantics)\n");
        } else {
             kprint("PASS: Exclusive blocked by Shared.\n");
        }
        vn_unlock(test_vp);
    }
}


static void test_vnode_init(void) {
    kprint("\n--- Test: vnode_init ---\n");
    /* We expect vnode_init to complete and not panic. */
    vnode_init();
    kprint("PASS: vnode_init completed without crashing.\n");
}

void run_vnode_lock_tests(void) {
    kprint("\n=== TEST: VNode Locking Semantics ===\n");

    test_vnode_init();

    /* Create a dummy vnode */
    int error = getnewvnode("test_lock", NULL, NULL, &test_vp);
    if (error) {
        kprintf("FAIL: getnewvnode failed: %d\n", error);
        return;
    }
    kprint("Created test vnode.\n");

    test_exclusive_blocking();

    /* Allow some time for cleanup/thread exit */
    for(int i=0; i<100000; i++) __asm__ volatile("nop");

    test_concurrent_shared();

    for(int i=0; i<100000; i++) __asm__ volatile("nop");

    test_shared_blocks_exclusive();

    kprint("=== TEST COMPLETE ===\n");
}
