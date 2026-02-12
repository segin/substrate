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

/* Mock UMA implementation */
uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor, uma_init init, uma_fini fini, int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return (uma_zone_t*)malloc(sizeof(uma_zone_t)); // Dummy zone
}

void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)zone; (void)flags;
    return calloc(1, 1024); // Sufficient size for vnode
}

void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone;
    free(item);
}

/* Mock sched_wakeup */
static void *wakeup_chan = NULL;
static int wakeup_called = 0;

void sched_wakeup(void *chan) {
    wakeup_called++;
    wakeup_chan = chan;
    printf("sched_wakeup called for %p\n", chan);
}

/* We need struct mount/thread definitions for vnode.h */
struct mount {};
struct thread {};

/* Include source file */
/* We include sys/vfs/vnode.c.
   We need to make sure include paths are correct.
*/
#include "../../sys/vfs/vnode.c"

int main() {
    printf("Running vn_unlock wakeup test...\n");

    struct vnode vp;
    memset(&vp, 0, sizeof(vp));
    spinlock_init(&vp.v_interlock, "test_lock");

    /* Test 1: Normal unlock */
    printf("Test 1: Normal unlock (no waiters)\n");
    vp.v_lockstate = 2; // Locked exclusive
    vp.v_flag = VXLOCK;
    wakeup_called = 0;
    wakeup_chan = NULL;

    vn_unlock(&vp);

    if (vp.v_lockstate != 0) {
        printf("FAIL: lockstate not cleared\n");
        return 1;
    }
    if (vp.v_flag & VXLOCK) {
        printf("FAIL: VXLOCK not cleared\n");
        return 1;
    }
    if (wakeup_called != 0) {
        printf("FAIL: wakeup called unexpectedly\n");
        return 1;
    }
    printf("PASS\n");

    /* Test 2: Unlock with waiters */
    printf("Test 2: Unlock with waiters (VXWANT)\n");
    vp.v_lockstate = 2;
    vp.v_flag = VXLOCK | VXWANT;
    wakeup_called = 0;
    wakeup_chan = NULL;

    vn_unlock(&vp);

    if (vp.v_lockstate != 0) {
        printf("FAIL: lockstate not cleared\n");
        return 1;
    }
    if (vp.v_flag & VXLOCK) {
        printf("FAIL: VXLOCK not cleared\n");
        return 1;
    }
    if (vp.v_flag & VXWANT) {
        printf("FAIL: VXWANT not cleared\n");
        return 1;
    }
    if (wakeup_called != 1) {
        printf("FAIL: wakeup NOT called (called %d times)\n", wakeup_called);
        return 2; // Expected fail code
    } else {
        if (wakeup_chan != &vp) {
            printf("FAIL: wakeup called with wrong channel %p (expected %p)\n", wakeup_chan, &vp);
            return 1;
        }
        printf("PASS\n");
    }

    return 0;
}
