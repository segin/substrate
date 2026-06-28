#ifndef _SYS_LOCK_H
#define _SYS_LOCK_H

#include <stdint.h>

typedef int spinlock_t;

/*
 * Minimal stand-in for the kernel's lockmgr lock.  vnode.h embeds a
 * `struct lock v_lock` by value, so this must be a complete type (the
 * host tests never exercise the lock itself).
 */
struct lock {
    spinlock_t   lk_interlock;
    uint32_t     lk_flags;
    uint32_t     lk_sharecount;
    uint32_t     lk_waitcount;
    uint32_t     lk_exclusivecount;
    void        *lk_lockholder;
    int          lk_prio;
    const char  *lk_name;
};

#endif
