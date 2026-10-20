#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <sys/copy.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <sys/vt.h>
#include <vfs/vfs.h>

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

// Mocks
int devfs_register_called = 0;
void devfs_register_device(fs_node_t *node) {
    (void)node;
    devfs_register_called = 1;
}

void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }

/*
 * input_subsys.c serializes its event ring under a spinlock, consults the
 * active VT (graphics/kbd mode) before delivering events, copies events out
 * to userspace, and reads the caller's open-file offset via current_thread.
 * None of those paths run in the input_init() test, so minimal
 * stubs/globals satisfy the link.
 */
thread_t *current_thread = NULL;
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
int vt_get_active(void) { return 0; }
vt_state_t *vt_get_state(int n) { (void)n; return NULL; }

int sys_gettimeofday(struct timeval *tv, struct timezone *tz) { (void)tv; (void)tz; return 0; }
void kprint(const char *fmt) { (void)fmt; }

#include "../../sys/drivers/input/input_subsys.c"

static void test_input_init(void) {
    printf("Testing input_init...\n");

    // Reset state
    input_devices = (input_dev_t *)0xDEADBEEF; // Set to garbage
    devfs_register_called = 0;

    // Call function
    input_init();

    // Verify
    assert(input_devices == NULL);
    assert(devfs_register_called == 1);

    printf("PASS\n");
}

int main(void) {
    printf("Running input_subsys tests...\n");
    test_input_init();
    printf("All tests passed.\n");
    return 0;
}
