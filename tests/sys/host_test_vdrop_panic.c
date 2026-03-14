#include <assert.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int32_t register_t;

#include <sys/lock.h>
#include <kern/sched.h>
#include <vfs/vnode.h>
#include <vm/uma.h>

thread_t *current_thread;

static jmp_buf panic_jmp;
static const char *last_panic;

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFF;
    lock->name = name;
}
void spinlock_acquire(spinlock_t *lock) { lock->locked = 1; }
bool spinlock_try_acquire(spinlock_t *lock) {
    if (lock->locked) return false;
    lock->locked = 1;
    return true;
}
void spinlock_release(spinlock_t *lock) { lock->locked = 0; }
bool spinlock_is_held(spinlock_t *lock) { return lock->locked != 0; }

void sched_yield(void) {}
void sleepq_add(void *chan, thread_t *t) { (void)chan; t->state = THREAD_BLOCKED; }
thread_t *sleepq_wake_one(void *chan) { (void)chan; return NULL; }
int sleepq_wake_all(void *chan) { (void)chan; return 0; }
int sleepq_has_waiters(void *chan) { (void)chan; return 0; }

void panic(const char *msg) {
    last_panic = msg;
    longjmp(panic_jmp, 1);
}

void kprint(const char *str) {
    (void)str;
}

uma_zone_t *uma_zcreate(const char *name, size_t size,
                        int (*ctor)(void *, int, void *, int),
                        void (*dtor)(void *, int, void *),
                        int (*init)(void *, int, int),
                        void (*fini)(void *, int),
                        int align, uint32_t flags) {
    (void)name; (void)size; (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    return (uma_zone_t *)1;
}
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)zone; (void)flags;
    static struct vnode mock_vnode;
    return &mock_vnode;
}
void uma_zfree(uma_zone_t *zone, void *item) {
    (void)zone; (void)item;
}

#include "../../sys/vfs/vnode.c"

int main(void) {
    vnode_init();

    struct vnode *vp;
    int error = getnewvnode("test", NULL, NULL, &vp);
    assert(error == 0);
    assert(vp != NULL);

    vp->v_holdcount = 1;
    vdrop(vp); // Should not panic
    assert(vp->v_holdcount == 0);

    if (setjmp(panic_jmp) == 0) {
        vdrop(vp); // Should panic
        assert(!"expected panic");
    }

    assert(last_panic != NULL);
    assert(strcmp(last_panic, "vdrop: holdcount already zero") == 0);

    printf("PASS\n");
    return 0;
}
