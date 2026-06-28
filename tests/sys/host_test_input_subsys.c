#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>

/*
 * This test builds with -idirafter ../../sys/include, so bare <sys/...>
 * includes resolve to the HOST's glibc headers first.  input_subsys.c grew
 * dependencies on the SUBSTRATE <sys/vt.h> (vt_state_t / vt_get_state) and
 * <sys/file.h> (struct file with f_offset/f_flag).  The host <sys/vt.h>
 * pulls <linux/vt.h>, which defines its own `struct vt_mode` that then
 * collides with substrate's <sys/vtio.h>.
 *
 * Pull substrate's vt.h / vtio.h / file.h in explicitly by path so the
 * substrate types win, and pre-claim <linux/vt.h>'s include guard so the
 * host <sys/vt.h> that input_subsys.c reaches for becomes a harmless no-op
 * (no duplicate struct vt_mode).
 */
// AC_COMM_LEN is needed by <sys/proc.h>'s acct struct on the host.
#define AC_COMM_LEN 16

#define _LINUX_VT_H
#include "../../sys/include/sys/vt.h"
#include "../../sys/include/sys/vtio.h"
#include "../../sys/include/sys/file.h"
#include "../../sys/include/sys/proc.h"
#include "../../sys/include/sys/lock.h"

/*
 * input_subsys.c carries its own `extern int copyout(const void *, void *,
 * unsigned int)`.  On the 32-bit target that matches <sys/copy.h>'s size_t
 * prototype; on this 64-bit host size_t is wider, so the two collide.
 * Suppress copy.h (claim its guard) and provide host stubs with the
 * host-matching signatures.
 */
#define _SYS_COPY_H
#include <stddef.h>
int validate_user_addr(const void *addr, size_t size);
int copyin(const void *src, void *dst, size_t size);
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len);
int copyout(const void *src, void *dst, unsigned int size) {
    memcpy(dst, src, size);
    return 0;
}

// Forward declarations
struct fs_node;

// Mocks
int devfs_register_called = 0;
void devfs_register_device(struct fs_node *node) {
    (void)node;
    devfs_register_called = 1;
}

void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }

/*
 * input_subsys.c now serializes its event ring under a spinlock, consults
 * the active VT (graphics/kbd mode) before delivering events, copies events
 * out to userspace, and reads the caller's open-file offset via
 * current_thread.  None of those paths run in the input_init() test, so
 * minimal stubs/globals satisfy the link.
 */
thread_t *current_thread = NULL;
void spinlock_init(spinlock_t *lock, const char *name) { (void)lock; (void)name; }
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }
int vt_get_active(void) { return 0; }
vt_state_t *vt_get_state(int n) { (void)n; return NULL; }

// Use correct types as defined in kernel headers
// We just need to define struct timezone if host doesn't
struct timezone;
int sys_gettimeofday(struct timeval *tv, struct timezone *tz) { (void)tv; (void)tz; return 0; }
void kprint(const char *fmt) { (void)fmt; }

/*
 * input_subsys.c includes <arch/i386/intr.h>, whose inlines use 32-bit
 * privileged asm (pushfl/popfl/cli) the host assembler rejects.  Claim its
 * guard and supply no-op host equivalents.
 */
#define _ARCH_I386_INTR_H
static inline uint32_t intr_disable(void) { return 0; }
static inline void intr_restore(uint32_t flags) { (void)flags; }
static inline void intr_enable(void) {}
static inline void wait_for_interrupt(void) {}

// Include the source file directly for testing.
//
// input_read() is typed size_t/uint32_t — identical on the 32-bit target,
// but on this 64-bit host size_t is wider than uint32_t, so installing it
// into the read_type_t (size_t-based) devfs slot trips
// -Wincompatible-pointer-types (an error by default on modern GCC).  This is
// a pure host/target width artefact, not a real type error; suppress it for
// the include.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#include "../../sys/drivers/input/input_subsys.c"
#pragma GCC diagnostic pop

void test_input_init() {
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

int main() {
    printf("Running input_subsys tests...\n");
    test_input_init();
    printf("All tests passed.\n");
    return 0;
}
