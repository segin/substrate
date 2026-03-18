#define HOST_TEST

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>

/* Mock helper for kprint/panic */
void kprint(const char *msg) {
    printf("%s", msg);
}

void panic(const char *msg) {
    printf("PANIC: %s\n", msg);
    exit(1);
}

/* Include kernel headers */
#include <sys/types.h>
#include <sys/lock.h>
#include <sys/queue.h>
#include <vm/uma.h>

/* Mock spinlock implementation */
void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    lock->locked = 1;
}

void spinlock_release(spinlock_t *lock) {
    lock->locked = 0;
}

bool spinlock_try_acquire(spinlock_t *lock) {
    lock->locked = 1;
    return true;
}

bool spinlock_is_held(spinlock_t *lock) {
    return lock->locked != 0;
}

/* Mock UMA implementation */
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return (uma_zone_t*)malloc(sizeof(uma_zone_t));
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)zone; (void)flags;
    return calloc(1, 1024);
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

/* Track lockmgr calls */
static uint32_t last_lockmgr_flags = 0;
static int lockmgr_call_count = 0;
static int lockmgr_return_value = 0;

/* Mock lockmgr, lockinit, lockstatus, lockdestroy */
void lockinit(struct lock *lkp, int prio, const char *name, int flags) {
    (void)prio; (void)flags;
    memset(lkp, 0, sizeof(*lkp));
    spinlock_init(&lkp->lk_interlock, name ? name : "lock");
    lkp->lk_name = name;
}

int lockmgr(struct lock *lkp, uint32_t flags, spinlock_t *interlock) {
    (void)lkp; (void)interlock;
    last_lockmgr_flags = flags;
    lockmgr_call_count++;
    return lockmgr_return_value;
}

int lockstatus(struct lock *lkp) {
    return (int)lkp->lk_flags;
}

int lockcount(struct lock *lkp) {
    return (int)(lkp->lk_sharecount + lkp->lk_exclusivecount);
}

void lockdestroy(struct lock *lkp) {
    (void)lkp;
}

/* Mock sleepq, sched, turnstile */
struct thread { int dummy; };
struct thread _mock_thread;
struct thread *current_thread = &_mock_thread;

void sleepq_add(void *chan, struct thread *td) { (void)chan; (void)td; }
void sleepq_wake_one(void *chan) { (void)chan; }
void sleepq_wake_all(void *chan) { (void)chan; }
bool sleepq_has_waiters(void *chan) { (void)chan; return false; }
void sched_yield(void) {}
void sched_wakeup(void *chan) { (void)chan; }
void turnstile_block(void *lockobj, struct thread *owner) { (void)lockobj; (void)owner; }
void turnstile_release(void *lockobj) { (void)lockobj; }

/* We need struct mount definition for vnode.h */
struct mount {};

/* Include vnode source to test vn_lock/vn_unlock/vn_islocked */
#include "../../sys/vfs/vnode.c"

int main(void) {
    printf("Running vn_lock/vn_unlock lockmgr integration test...\n");

    struct vnode vp;
    memset(&vp, 0, sizeof(vp));
    spinlock_init(&vp.v_interlock, "test_lock");
    lockinit(&vp.v_lock, 0, "test_vnode", 0);

    /* Test 1: vn_lock with LK_EXCLUSIVE */
    printf("Test 1: vn_lock LK_EXCLUSIVE\n");
    lockmgr_call_count = 0;
    last_lockmgr_flags = 0;
    lockmgr_return_value = 0;

    int err = vn_lock(&vp, LK_EXCLUSIVE);
    if (err != 0) {
        printf("FAIL: vn_lock returned %d\n", err);
        return 1;
    }
    if (!(last_lockmgr_flags & LK_EXCLUSIVE)) {
        printf("FAIL: lockmgr not called with LK_EXCLUSIVE (flags=0x%x)\n", last_lockmgr_flags);
        return 1;
    }
    if (lockmgr_call_count != 1) {
        printf("FAIL: lockmgr called %d times\n", lockmgr_call_count);
        return 1;
    }
    printf("PASS\n");

    /* Test 2: vn_lock with LK_SHARED */
    printf("Test 2: vn_lock LK_SHARED\n");
    lockmgr_call_count = 0;
    last_lockmgr_flags = 0;

    err = vn_lock(&vp, LK_SHARED);
    if (err != 0) {
        printf("FAIL: vn_lock returned %d\n", err);
        return 1;
    }
    if (!(last_lockmgr_flags & LK_SHARED)) {
        printf("FAIL: lockmgr not called with LK_SHARED (flags=0x%x)\n", last_lockmgr_flags);
        return 1;
    }
    printf("PASS\n");

    /* Test 3: vn_lock with LK_NOWAIT (failure path) */
    printf("Test 3: vn_lock LK_EXCLUSIVE|LK_NOWAIT (EBUSY)\n");
    lockmgr_call_count = 0;
    last_lockmgr_flags = 0;
    lockmgr_return_value = 16; /* EBUSY */

    err = vn_lock(&vp, LK_EXCLUSIVE | LK_NOWAIT);
    if (err != -11) { /* -EAGAIN */
        printf("FAIL: expected -EAGAIN (-11), got %d\n", err);
        return 1;
    }
    if (!(last_lockmgr_flags & LK_NOWAIT)) {
        printf("FAIL: lockmgr not called with LK_NOWAIT (flags=0x%x)\n", last_lockmgr_flags);
        return 1;
    }
    printf("PASS\n");
    lockmgr_return_value = 0;

    /* Test 4: vn_unlock calls lockmgr with LK_RELEASE */
    printf("Test 4: vn_unlock calls LK_RELEASE\n");
    lockmgr_call_count = 0;
    last_lockmgr_flags = 0;

    vn_unlock(&vp);
    if (!(last_lockmgr_flags & LK_RELEASE)) {
        printf("FAIL: lockmgr not called with LK_RELEASE (flags=0x%x)\n", last_lockmgr_flags);
        return 1;
    }
    if (lockmgr_call_count != 1) {
        printf("FAIL: lockmgr called %d times\n", lockmgr_call_count);
        return 1;
    }
    printf("PASS\n");

    /* Test 5: vn_islocked returns correct status */
    printf("Test 5: vn_islocked status mapping\n");

    vp.v_lock.lk_flags = 0;
    if (vn_islocked(&vp) != 0) {
        printf("FAIL: expected 0 (unlocked)\n");
        return 1;
    }

    vp.v_lock.lk_flags = LK_SHARED;
    if (vn_islocked(&vp) != 1) {
        printf("FAIL: expected 1 (shared)\n");
        return 1;
    }

    vp.v_lock.lk_flags = LK_EXCLUSIVE;
    if (vn_islocked(&vp) != 2) {
        printf("FAIL: expected 2 (exclusive)\n");
        return 1;
    }
    printf("PASS\n");

    printf("All tests passed.\n");
    return 0;
}
