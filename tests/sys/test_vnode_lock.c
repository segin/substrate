#include <vfs/vnode.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <sys/kthread.h>
#include <kern/sleepq.h>

static struct vnode *test_vp;
static volatile int helper_started = 0;
static volatile int helper_done = 0;

static void helper_lock_thread(void *arg) {
    (void)arg;
    int error;

    kprint("Helper: Trying to lock vnode...\n");
    error = vn_lock(test_vp, LK_EXCLUSIVE);
    if (error) {
        kprintf("Helper: vn_lock failed: %d\n", error);
        return;
    }
    kprint("Helper: Got lock! Sleeping...\n");

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

void run_vnode_lock_tests(void) {
    kprint("\n=== TEST: VNode Locking Semantics ===\n");

    /* Create a dummy vnode */
    /* We pass NULL for mount and ops as we only test locking */
    int error = getnewvnode("test_lock", NULL, NULL, &test_vp);
    if (error) {
        kprintf("FAIL: getnewvnode failed: %d\n", error);
        return;
    }
    kprint("Created test vnode.\n");

    /* Spawn helper thread */
    thread_t *t;
    kthread_create(helper_lock_thread, NULL, &t, "lock_helper");

    /* Wait for helper to acquire lock */
    while (!helper_started) {
        sched_yield();
    }

    kprint("Main: Helper has lock. Attempting to acquire lock (should block)...\n");

    /* This should block until helper releases lock */
    error = vn_lock(test_vp, LK_EXCLUSIVE);

    if (error) {
        kprintf("FAIL: Main vn_lock failed: %d\n", error);
    } else {
        kprint("Main: Acquired lock!\n");

        if (!helper_done) {
            /* If we got here before helper is done, locking is broken (non-blocking) */
            kprint("FAIL: Acquired lock before helper finished! (Locking is broken/non-blocking)\n");
        } else {
            kprint("PASS: Acquired lock after helper finished (Blocking worked)\n");
        }

        vn_unlock(test_vp);
    }

    /* Cleanup - though in kernel test we might just leak the vnode or rely on reclaim later */
    /* We can't easily free vnode here without proper VFS teardown, but it's a test */

    kprint("=== TEST COMPLETE ===\n");
}
