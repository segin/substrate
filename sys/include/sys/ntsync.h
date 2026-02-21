/*
 * ntsync.h - Windows NT Synchronization Primitive Driver
 *
 * Provides kernel-level emulation of Windows NT synchronization primitives
 * for use by Windows compatibility layers (Wine/Proton).
 *
 * Based on Linux kernel 6.14 ntsync interface.
 *
 * Objects:
 *   - Semaphores: count/max with atomic post
 *   - Mutexes: recursive with owner death detection
 *   - Events: manual-reset and auto-reset
 *
 * Usage:
 *   1. Open /dev/ntsync to get an instance FD
 *   2. Use NTSYNC_IOC_CREATE_* on instance FD to create objects (returns object FD)
 *   3. Use object-specific ioctls on object FDs
 *   4. Use NTSYNC_IOC_WAIT_* on instance FD to wait on multiple objects
 */

#ifndef _SYS_NTSYNC_H
#define _SYS_NTSYNC_H

#include <stdint.h>

/*
 * ============================================================
 * Ioctl Argument Structures
 * ============================================================
 */

/*
 * Semaphore creation/query arguments
 *
 * count: Current/initial count (must be <= max)
 * max:   Maximum count (>= 1)
 */
struct ntsync_sem_args {
    uint32_t count;
    uint32_t max;
};

/*
 * Mutex creation/query/unlock arguments
 *
 * owner: Thread identifier owning the mutex (0 = unowned)
 * count: Recursion count (0 = not held, must be 0 if owner is 0)
 */
struct ntsync_mutex_args {
    uint32_t owner;
    uint32_t count;
};

/*
 * Event creation/query arguments
 *
 * signaled: Non-zero if event is signaled
 * manual:   Non-zero for manual-reset, zero for auto-reset
 */
struct ntsync_event_args {
    uint32_t signaled;
    uint32_t manual;
};

/*
 * Wait operation arguments
 *
 * timeout: Absolute timeout in nanoseconds (U64_MAX = infinite)
 * objs:    Pointer to array of object FDs
 * count:   Number of objects in array
 * owner:   Owner ID for mutex acquisition
 * index:   [out] Index of signaled object (count if alert triggered)
 * alert:   Optional alert event FD (0 = none)
 * flags:   NTSYNC_WAIT_* flags
 * pad:     Reserved, must be 0
 */
struct ntsync_wait_args {
    uint64_t timeout;
    uint64_t objs;      /* Pointer to int[] - sized as u64 for 32/64 compat */
    uint32_t count;
    uint32_t owner;
    uint32_t index;
    uint32_t alert;
    uint32_t flags;
    uint32_t pad;
};

/*
 * ============================================================
 * Wait Flags
 * ============================================================
 */

#define NTSYNC_WAIT_REALTIME    (1 << 0)  /* Use REALTIME clock instead of MONOTONIC */

/*
 * ============================================================
 * Limits
 * ============================================================
 */

#define NTSYNC_MAX_WAIT_COUNT   64  /* Maximum objects in single wait */

/*
 * ============================================================
 * Ioctl Numbers
 *
 * Magic 'N' (0x4E) for NTSYNC
 * ============================================================
 */

/* Instance (device fd) ioctls - create objects, perform waits */
#define NTSYNC_IOC_CREATE_SEM     _IOWR('N', 0x00, struct ntsync_sem_args)
#define NTSYNC_IOC_CREATE_MUTEX   _IOWR('N', 0x01, struct ntsync_mutex_args)
#define NTSYNC_IOC_CREATE_EVENT   _IOWR('N', 0x02, struct ntsync_event_args)
#define NTSYNC_IOC_WAIT_ANY       _IOWR('N', 0x03, struct ntsync_wait_args)
#define NTSYNC_IOC_WAIT_ALL       _IOWR('N', 0x04, struct ntsync_wait_args)

/* Object (semaphore fd) ioctls */
#define NTSYNC_IOC_SEM_POST       _IOWR('N', 0x10, uint32_t)
#define NTSYNC_IOC_READ_SEM       _IOR('N', 0x11, struct ntsync_sem_args)

/* Object (mutex fd) ioctls */
#define NTSYNC_IOC_MUTEX_UNLOCK   _IOWR('N', 0x20, struct ntsync_mutex_args)
#define NTSYNC_IOC_READ_MUTEX     _IOR('N', 0x21, struct ntsync_mutex_args)
#define NTSYNC_IOC_KILL_OWNER     _IOW('N', 0x22, uint32_t)

/* Object (event fd) ioctls */
#define NTSYNC_IOC_SET_EVENT      _IOR('N', 0x30, uint32_t)
#define NTSYNC_IOC_RESET_EVENT    _IOR('N', 0x31, uint32_t)
#define NTSYNC_IOC_PULSE_EVENT    _IOR('N', 0x32, uint32_t)
#define NTSYNC_IOC_READ_EVENT     _IOR('N', 0x33, struct ntsync_event_args)

/*
 * ============================================================
 * Ioctl Helper Macros (if not defined elsewhere)
 * ============================================================
 */

#ifndef _IOC
#define _IOC_NRBITS     8
#define _IOC_TYPEBITS   8
#define _IOC_SIZEBITS   14
#define _IOC_DIRBITS    2

#define _IOC_NRSHIFT    0
#define _IOC_TYPESHIFT  (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT  (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT   (_IOC_SIZESHIFT + _IOC_SIZEBITS)

#define _IOC_NONE       0U
#define _IOC_WRITE      1U
#define _IOC_READ       2U

#define _IOC(dir, type, nr, size) \
    (((dir)  << _IOC_DIRSHIFT) | \
     ((type) << _IOC_TYPESHIFT) | \
     ((nr)   << _IOC_NRSHIFT) | \
     ((size) << _IOC_SIZESHIFT))

#define _IO(type, nr)           _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, argtype) _IOC(_IOC_READ, (type), (nr), sizeof(argtype))
#define _IOW(type, nr, argtype) _IOC(_IOC_WRITE, (type), (nr), sizeof(argtype))
#define _IOWR(type, nr, argtype) _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(argtype))
#endif /* _IOC */

#ifdef _KERNEL
#include <vfs/vfs.h>
#include <kern/sched.h>

/*
 * ============================================================
 * Internal Data Structures
 * ============================================================
 */

/*
 * Object types
 */
typedef enum {
    NTSYNC_OBJ_SEM = 1,
    NTSYNC_OBJ_MUTEX,
    NTSYNC_OBJ_EVENT
} ntsync_obj_type_t;

/*
 * Wait queue entry
 */
typedef struct ntsync_waiter {
    struct ntsync_waiter *next;
    thread_t *thread;
    int signaled;       /* Set when object signals this waiter */
    int all_wait;       /* Part of wait-all operation */
    int priority;       /* For priority ordering */
} ntsync_waiter_t;

struct ntsync_instance;

/*
 * Base object structure (common to all object types)
 */
typedef struct ntsync_object {
    ntsync_obj_type_t type;
    uint32_t refcount;
    
    /* Wait queue for threads waiting on this object */
    ntsync_waiter_t *waiters;
    int waiter_count;
    
    /* Spinlock for thread safety */
    volatile int lock;
    
    /* Type-specific data follows */
    union {
        /* Semaphore */
        struct {
            uint32_t count;
            uint32_t max;
        } sem;
        
        /* Mutex */
        struct {
            uint32_t owner;
            uint32_t count;
            int abandoned;
        } mutex;
        
        /* Event */
        struct {
            uint32_t signaled;
            uint32_t manual;    /* 1 = manual-reset, 0 = auto-reset */
        } event;
    };
    
    /* Back-pointer to owning instance (for validation) */
    struct ntsync_instance *instance;
    
    /* fs_node for this object */
    fs_node_t node;
} ntsync_object_t;

/*
 * Instance structure (one per open of /dev/ntsync)
 */
typedef struct ntsync_instance {
    /* Object list for cleanup on close */
    ntsync_object_t **objects;
    int object_count;
    int object_capacity;
    
    /* Lock for instance state */
    volatile int lock;
    
    /* fs_node for this instance */
    fs_node_t node;
} ntsync_instance_t;

#endif /* _KERNEL */

#endif /* _SYS_NTSYNC_H */
