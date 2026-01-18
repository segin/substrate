/*
 * ntsync.c - Windows NT Synchronization Primitive Driver
 *
 * Provides kernel-level emulation of Windows NT synchronization primitives
 * (semaphores, mutexes, events) for Wine/Proton compatibility.
 *
 * Based on Linux kernel 6.14 ntsync interface.
 *
 * Architecture:
 *   - Each open() of /dev/ntsync creates an ntsync_instance
 *   - Each instance maintains its own object namespace
 *   - Objects are created via ioctls and returned as FDs
 *   - Objects are automatically destroyed when all FDs are closed
 */

#include "../../vfs/vfs.h"
#include "../../kern/console.h"
#include "../../kern/sched.h"
#include <sys/ntsync.h>
#include <sys/proc.h>
#include <string.h>
#include <errno.h>

/* Forward declarations */
static int ntsync_ioctl(fs_node_t *node, uint32_t request, void *arg);
static int ntsync_obj_ioctl(fs_node_t *node, uint32_t request, void *arg);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/*
 * ============================================================
 * Data Structures
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

/*
 * ============================================================
 * Spinlock Helpers
 * ============================================================
 */

static inline void spinlock_acquire(volatile int *lock) {
    while (__sync_lock_test_and_set(lock, 1)) {
        while (*lock) {
            __asm__ volatile("pause");
        }
    }
}

static inline void spinlock_release(volatile int *lock) {
    __sync_lock_release(lock);
}

/*
 * ============================================================
 * Object Reference Counting
 * ============================================================
 */

static void ntsync_object_ref(ntsync_object_t *obj) {
    __sync_fetch_and_add(&obj->refcount, 1);
}

static void ntsync_object_unref(ntsync_object_t *obj) {
    if (__sync_sub_and_fetch(&obj->refcount, 1) == 0) {
        /* Free the object */
        kfree(obj);
    }
}

/*
 * ============================================================
 * Wait Queue Management
 * ============================================================
 */

static void waiter_enqueue(ntsync_object_t *obj, ntsync_waiter_t *w) {
    /* Insert in priority order (higher priority first) */
    ntsync_waiter_t **pp = &obj->waiters;
    while (*pp && (*pp)->priority >= w->priority) {
        pp = &(*pp)->next;
    }
    w->next = *pp;
    *pp = w;
    obj->waiter_count++;
}

static void waiter_dequeue(ntsync_object_t *obj, ntsync_waiter_t *w) {
    ntsync_waiter_t **pp = &obj->waiters;
    while (*pp) {
        if (*pp == w) {
            *pp = w->next;
            obj->waiter_count--;
            return;
        }
        pp = &(*pp)->next;
    }
}

/*
 * ============================================================
 * Signaling Helpers
 * ============================================================
 */

/*
 * Check if object is signaled (can be acquired)
 */
static int ntsync_is_signaled(ntsync_object_t *obj, uint32_t owner) {
    switch (obj->type) {
    case NTSYNC_OBJ_SEM:
        return obj->sem.count > 0;
        
    case NTSYNC_OBJ_MUTEX:
        /* Signaled if unowned or owned by us */
        return (obj->mutex.owner == 0) || 
               (obj->mutex.owner == owner) ||
               (obj->mutex.abandoned);
        
    case NTSYNC_OBJ_EVENT:
        return obj->event.signaled != 0;
        
    default:
        return 0;
    }
}

/*
 * Acquire an object (modify state on acquisition)
 * Returns 0 on success, -EOWNERDEAD if abandoned mutex
 */
static int ntsync_acquire(ntsync_object_t *obj, uint32_t owner) {
    int ret = 0;
    
    switch (obj->type) {
    case NTSYNC_OBJ_SEM:
        if (obj->sem.count > 0) {
            obj->sem.count--;
        }
        break;
        
    case NTSYNC_OBJ_MUTEX:
        if (obj->mutex.abandoned) {
            obj->mutex.abandoned = 0;
            obj->mutex.owner = owner;
            obj->mutex.count = 1;
            ret = -EOWNERDEAD;
        } else if (obj->mutex.owner == 0) {
            obj->mutex.owner = owner;
            obj->mutex.count = 1;
        } else if (obj->mutex.owner == owner) {
            obj->mutex.count++;
        }
        break;
        
    case NTSYNC_OBJ_EVENT:
        /* Auto-reset events get designaled on acquisition */
        if (!obj->event.manual) {
            obj->event.signaled = 0;
        }
        break;
    }
    
    return ret;
}

/*
 * Wake one waiter (for single-object wake)
 */
static void ntsync_wake_one(ntsync_object_t *obj) {
    if (obj->waiters) {
        ntsync_waiter_t *w = obj->waiters;
        w->signaled = 1;
        sched_wakeup(w->thread);
        /* Don't dequeue - waiter will dequeue itself */
    }
}

/*
 * Wake all waiters (for broadcast events)
 */
static void ntsync_wake_all(ntsync_object_t *obj) {
    for (ntsync_waiter_t *w = obj->waiters; w; w = w->next) {
        if (!w->all_wait) {
            w->signaled = 1;
            sched_wakeup(w->thread);
        }
    }
}

/*
 * ============================================================
 * Object ioctl handlers
 * ============================================================
 */

/*
 * NTSYNC_IOC_SEM_POST - Post to semaphore
 */
static int ntsync_sem_post(ntsync_object_t *obj, uint32_t *arg) {
    if (obj->type != NTSYNC_OBJ_SEM) return -EINVAL;
    
    uint32_t add_count = *arg;
    uint32_t prev_count;
    
    spinlock_acquire(&obj->lock);
    
    prev_count = obj->sem.count;
    
    /* Check for overflow - return ERANGE as fallback */
    if (add_count > obj->sem.max - obj->sem.count) {
        spinlock_release(&obj->lock);
        return -ERANGE;  /* EOVERFLOW not available, use ERANGE */
    }
    
    obj->sem.count += add_count;
    
    /* Wake waiters */
    while (obj->sem.count > 0 && obj->waiters) {
        ntsync_waiter_t *w = obj->waiters;
        if (!w->all_wait) {
            w->signaled = 1;
            obj->sem.count--;
            sched_wakeup(w->thread);
        }
        obj->waiters = w->next;
        obj->waiter_count--;
    }
    
    spinlock_release(&obj->lock);
    
    *arg = prev_count;
    return 0;
}

/*
 * NTSYNC_IOC_READ_SEM - Read semaphore state
 */
static int ntsync_read_sem(ntsync_object_t *obj, struct ntsync_sem_args *arg) {
    if (obj->type != NTSYNC_OBJ_SEM) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    arg->count = obj->sem.count;
    arg->max = obj->sem.max;
    spinlock_release(&obj->lock);
    
    return 0;
}

/*
 * NTSYNC_IOC_MUTEX_UNLOCK - Unlock mutex
 */
static int ntsync_mutex_unlock(ntsync_object_t *obj, struct ntsync_mutex_args *arg) {
    if (obj->type != NTSYNC_OBJ_MUTEX) return -EINVAL;
    if (arg->owner == 0) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    
    if (obj->mutex.owner != arg->owner) {
        spinlock_release(&obj->lock);
        return -EPERM;
    }
    
    uint32_t prev_count = obj->mutex.count;
    
    obj->mutex.count--;
    if (obj->mutex.count == 0) {
        obj->mutex.owner = 0;
        /* Wake one waiter */
        ntsync_wake_one(obj);
    }
    
    spinlock_release(&obj->lock);
    
    arg->count = prev_count;
    return 0;
}

/*
 * NTSYNC_IOC_READ_MUTEX - Read mutex state
 */
static int ntsync_read_mutex(ntsync_object_t *obj, struct ntsync_mutex_args *arg) {
    if (obj->type != NTSYNC_OBJ_MUTEX) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    
    if (obj->mutex.abandoned) {
        arg->owner = 0;
        arg->count = 0;
        spinlock_release(&obj->lock);
        return -EOWNERDEAD;
    }
    
    arg->owner = obj->mutex.owner;
    arg->count = obj->mutex.count;
    spinlock_release(&obj->lock);
    
    return 0;
}

/*
 * NTSYNC_IOC_KILL_OWNER - Mark mutex as abandoned
 */
static int ntsync_kill_owner(ntsync_object_t *obj, uint32_t owner) {
    if (obj->type != NTSYNC_OBJ_MUTEX) return -EINVAL;
    if (owner == 0) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    
    if (obj->mutex.owner != owner) {
        spinlock_release(&obj->lock);
        return -EPERM;
    }
    
    obj->mutex.abandoned = 1;
    obj->mutex.owner = 0;
    obj->mutex.count = 0;
    
    /* Wake waiters (they'll get EOWNERDEAD) */
    ntsync_wake_one(obj);
    
    spinlock_release(&obj->lock);
    
    return 0;
}

/*
 * NTSYNC_IOC_SET_EVENT - Signal event
 */
static int ntsync_set_event(ntsync_object_t *obj, uint32_t *arg) {
    if (obj->type != NTSYNC_OBJ_EVENT) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    
    uint32_t prev = obj->event.signaled;
    obj->event.signaled = 1;
    
    if (obj->event.manual) {
        ntsync_wake_all(obj);
    } else {
        ntsync_wake_one(obj);
        obj->event.signaled = 0;  /* Auto-reset */
    }
    
    spinlock_release(&obj->lock);
    
    *arg = prev;
    return 0;
}

/*
 * NTSYNC_IOC_RESET_EVENT - Designal event
 */
static int ntsync_reset_event(ntsync_object_t *obj, uint32_t *arg) {
    if (obj->type != NTSYNC_OBJ_EVENT) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    uint32_t prev = obj->event.signaled;
    obj->event.signaled = 0;
    spinlock_release(&obj->lock);
    
    *arg = prev;
    return 0;
}

/*
 * NTSYNC_IOC_PULSE_EVENT - Signal then reset atomically
 */
static int ntsync_pulse_event(ntsync_object_t *obj, uint32_t *arg) {
    if (obj->type != NTSYNC_OBJ_EVENT) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    
    uint32_t prev = obj->event.signaled;
    
    /* Wake all/one based on type, then reset */
    if (obj->event.manual) {
        ntsync_wake_all(obj);
    } else {
        ntsync_wake_one(obj);
    }
    obj->event.signaled = 0;
    
    spinlock_release(&obj->lock);
    
    *arg = prev;
    return 0;
}

/*
 * NTSYNC_IOC_READ_EVENT - Read event state
 */
static int ntsync_read_event(ntsync_object_t *obj, struct ntsync_event_args *arg) {
    if (obj->type != NTSYNC_OBJ_EVENT) return -EINVAL;
    
    spinlock_acquire(&obj->lock);
    arg->signaled = obj->event.signaled;
    arg->manual = obj->event.manual;
    spinlock_release(&obj->lock);
    
    return 0;
}

/*
 * ============================================================
 * Object ioctl dispatcher
 * ============================================================
 */

static int ntsync_obj_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    ntsync_object_t *obj = (ntsync_object_t *)(uintptr_t)node->impl;
    if (!obj) return -EINVAL;
    
    switch (request) {
    case NTSYNC_IOC_SEM_POST:
        return ntsync_sem_post(obj, (uint32_t *)arg);
        
    case NTSYNC_IOC_READ_SEM:
        return ntsync_read_sem(obj, (struct ntsync_sem_args *)arg);
        
    case NTSYNC_IOC_MUTEX_UNLOCK:
        return ntsync_mutex_unlock(obj, (struct ntsync_mutex_args *)arg);
        
    case NTSYNC_IOC_READ_MUTEX:
        return ntsync_read_mutex(obj, (struct ntsync_mutex_args *)arg);
        
    case NTSYNC_IOC_KILL_OWNER:
        return ntsync_kill_owner(obj, *(uint32_t *)arg);
        
    case NTSYNC_IOC_SET_EVENT:
        return ntsync_set_event(obj, (uint32_t *)arg);
        
    case NTSYNC_IOC_RESET_EVENT:
        return ntsync_reset_event(obj, (uint32_t *)arg);
        
    case NTSYNC_IOC_PULSE_EVENT:
        return ntsync_pulse_event(obj, (uint32_t *)arg);
        
    case NTSYNC_IOC_READ_EVENT:
        return ntsync_read_event(obj, (struct ntsync_event_args *)arg);
        
    default:
        return -ENOSYS;
    }
}

/*
 * ============================================================
 * Object Creation
 * ============================================================
 */

/*
 * Allocate and initialize an object, returning its FD
 */
static int ntsync_create_object(ntsync_instance_t *inst, ntsync_obj_type_t type,
                                void *args) {
    ntsync_object_t *obj = kmalloc(sizeof(ntsync_object_t));
    if (!obj) return -ENOMEM;
    
    memset(obj, 0, sizeof(*obj));
    obj->type = type;
    obj->refcount = 1;
    obj->instance = inst;
    
    /* Initialize type-specific fields */
    switch (type) {
    case NTSYNC_OBJ_SEM: {
        struct ntsync_sem_args *sargs = args;
        if (sargs->count > sargs->max) {
            kfree(obj);
            return -EINVAL;
        }
        obj->sem.count = sargs->count;
        obj->sem.max = sargs->max;
        break;
    }
    
    case NTSYNC_OBJ_MUTEX: {
        struct ntsync_mutex_args *margs = args;
        /* owner==0 && count>0 or owner!=0 && count==0 is invalid */
        if ((margs->owner == 0 && margs->count != 0) ||
            (margs->owner != 0 && margs->count == 0)) {
            kfree(obj);
            return -EINVAL;
        }
        obj->mutex.owner = margs->owner;
        obj->mutex.count = margs->count;
        obj->mutex.abandoned = 0;
        break;
    }
    
    case NTSYNC_OBJ_EVENT: {
        struct ntsync_event_args *eargs = args;
        obj->event.signaled = eargs->signaled ? 1 : 0;
        obj->event.manual = eargs->manual ? 1 : 0;
        break;
    }
    
    default:
        kfree(obj);
        return -EINVAL;
    }
    
    /* Setup fs_node for this object */
    memset(&obj->node, 0, sizeof(fs_node_t));
    obj->node.flags = FS_CHARDEVICE;
    obj->node.impl = (uint32_t)(uintptr_t)obj;
    obj->node.ioctl = ntsync_obj_ioctl;
    
    /* Add to instance's object list */
    spinlock_acquire(&inst->lock);
    
    if (inst->object_count >= inst->object_capacity) {
        int new_cap = inst->object_capacity ? inst->object_capacity * 2 : 16;
        ntsync_object_t **new_objs = kmalloc(new_cap * sizeof(ntsync_object_t *));
        if (!new_objs) {
            spinlock_release(&inst->lock);
            kfree(obj);
            return -ENOMEM;
        }
        if (inst->objects) {
            memcpy(new_objs, inst->objects, 
                   inst->object_count * sizeof(ntsync_object_t *));
            kfree(inst->objects);
        }
        inst->objects = new_objs;
        inst->object_capacity = new_cap;
    }
    
    inst->objects[inst->object_count++] = obj;
    ntsync_object_ref(obj);  /* Instance holds a reference */
    
    spinlock_release(&inst->lock);
    
    /* Return an FD for this object
     * TODO: Proper FD allocation - for now return object index as pseudo-FD
     * In real implementation, this would allocate a real file descriptor
     */
    return inst->object_count - 1;
}

/*
 * ============================================================
 * Wait Operations
 * ============================================================
 */

/*
 * NTSYNC_IOC_WAIT_ANY - Wait for any of N objects
 */
static int ntsync_wait_any(ntsync_instance_t *inst, struct ntsync_wait_args *args) {
    if (args->count == 0) return -EINVAL;
    if (args->count > NTSYNC_MAX_WAIT_COUNT) return -EINVAL;
    if (args->owner == 0) return -EINVAL;
    
    int *obj_fds = (int *)(uintptr_t)args->objs;
    
    /* First pass: check if any object is already signaled */
    for (uint32_t i = 0; i < args->count; i++) {
        int fd = obj_fds[i];
        if (fd < 0 || fd >= inst->object_count) return -EINVAL;
        
        ntsync_object_t *obj = inst->objects[fd];
        spinlock_acquire(&obj->lock);
        
        if (ntsync_is_signaled(obj, args->owner)) {
            int ret = ntsync_acquire(obj, args->owner);
            args->index = i;
            spinlock_release(&obj->lock);
            return ret ? ret : 0;
        }
        
        spinlock_release(&obj->lock);
    }
    
    /* Check alert event if specified */
    if (args->alert != 0) {
        int alert_fd = args->alert;
        if (alert_fd >= 0 && alert_fd < inst->object_count) {
            ntsync_object_t *alert_obj = inst->objects[alert_fd];
            spinlock_acquire(&alert_obj->lock);
            if (alert_obj->type == NTSYNC_OBJ_EVENT && alert_obj->event.signaled) {
                args->index = args->count;  /* Alert signaled */
                if (!alert_obj->event.manual) {
                    alert_obj->event.signaled = 0;
                }
                spinlock_release(&alert_obj->lock);
                return 0;
            }
            spinlock_release(&alert_obj->lock);
        }
    }
    
    /* None signaled - need to sleep */
    ntsync_waiter_t waiter;
    waiter.thread = current_thread;
    waiter.signaled = 0;
    waiter.all_wait = 0;
    waiter.priority = current_thread ? current_thread->priority : 0;
    waiter.next = NULL;
    
    /* Add waiter to all objects */
    for (uint32_t i = 0; i < args->count; i++) {
        ntsync_object_t *obj = inst->objects[obj_fds[i]];
        spinlock_acquire(&obj->lock);
        waiter_enqueue(obj, &waiter);
        spinlock_release(&obj->lock);
    }
    
    /* Also add to alert if specified */
    if (args->alert != 0 && args->alert < (uint32_t)inst->object_count) {
        ntsync_object_t *alert_obj = inst->objects[args->alert];
        spinlock_acquire(&alert_obj->lock);
        waiter_enqueue(alert_obj, &waiter);
        spinlock_release(&alert_obj->lock);
    }
    
    /* Sleep until signaled or timeout */
    /* TODO: Implement proper timeout handling */
    while (!waiter.signaled) {
        sched_yield();  /* Basic sleep - could be improved with sleep queues */
    }
    
    /* Remove waiter from all objects */
    for (uint32_t i = 0; i < args->count; i++) {
        ntsync_object_t *obj = inst->objects[obj_fds[i]];
        spinlock_acquire(&obj->lock);
        waiter_dequeue(obj, &waiter);
        spinlock_release(&obj->lock);
    }
    
    if (args->alert != 0 && args->alert < (uint32_t)inst->object_count) {
        ntsync_object_t *alert_obj = inst->objects[args->alert];
        spinlock_acquire(&alert_obj->lock);
        waiter_dequeue(alert_obj, &waiter);
        spinlock_release(&alert_obj->lock);
    }
    
    /* Find which object signaled us */
    for (uint32_t i = 0; i < args->count; i++) {
        ntsync_object_t *obj = inst->objects[obj_fds[i]];
        spinlock_acquire(&obj->lock);
        
        if (ntsync_is_signaled(obj, args->owner)) {
            int ret = ntsync_acquire(obj, args->owner);
            args->index = i;
            spinlock_release(&obj->lock);
            return ret ? ret : 0;
        }
        
        spinlock_release(&obj->lock);
    }
    
    /* Check if alert triggered */
    if (args->alert != 0 && args->alert < (uint32_t)inst->object_count) {
        args->index = args->count;
        return 0;
    }
    
    return -EINTR;  /* Interrupted */
}

/*
 * NTSYNC_IOC_WAIT_ALL - Wait for all of N objects simultaneously
 */
static int ntsync_wait_all(ntsync_instance_t *inst, struct ntsync_wait_args *args) {
    if (args->count == 0) return -EINVAL;
    if (args->count > NTSYNC_MAX_WAIT_COUNT) return -EINVAL;
    if (args->owner == 0) return -EINVAL;
    
    int *obj_fds = (int *)(uintptr_t)args->objs;
    
    /* Check for duplicates (not allowed in wait_all) */
    for (uint32_t i = 0; i < args->count; i++) {
        for (uint32_t j = i + 1; j < args->count; j++) {
            if (obj_fds[i] == obj_fds[j]) return -EINVAL;
        }
        if (args->alert != 0 && obj_fds[i] == (int)args->alert) {
            return -EINVAL;
        }
    }
    
    /* Try to acquire all objects atomically */
    while (1) {
        int all_signaled = 1;
        
        /* Lock all objects */
        for (uint32_t i = 0; i < args->count; i++) {
            int fd = obj_fds[i];
            if (fd < 0 || fd >= inst->object_count) return -EINVAL;
            spinlock_acquire(&inst->objects[fd]->lock);
        }
        
        /* Check if all are signaled */
        for (uint32_t i = 0; i < args->count; i++) {
            ntsync_object_t *obj = inst->objects[obj_fds[i]];
            if (!ntsync_is_signaled(obj, args->owner)) {
                all_signaled = 0;
                break;
            }
        }
        
        if (all_signaled) {
            /* Acquire all */
            int ret = 0;
            for (uint32_t i = 0; i < args->count; i++) {
                ntsync_object_t *obj = inst->objects[obj_fds[i]];
                int r = ntsync_acquire(obj, args->owner);
                if (r == -EOWNERDEAD) ret = r;  /* Report if any mutex was abandoned */
            }
            
            /* Unlock all */
            for (uint32_t i = 0; i < args->count; i++) {
                spinlock_release(&inst->objects[obj_fds[i]]->lock);
            }
            
            args->index = 0;
            return ret;
        }
        
        /* Unlock all */
        for (uint32_t i = 0; i < args->count; i++) {
            spinlock_release(&inst->objects[obj_fds[i]]->lock);
        }
        
        /* Check alert */
        if (args->alert != 0 && args->alert < (uint32_t)inst->object_count) {
            ntsync_object_t *alert_obj = inst->objects[args->alert];
            spinlock_acquire(&alert_obj->lock);
            if (alert_obj->type == NTSYNC_OBJ_EVENT && alert_obj->event.signaled) {
                if (!alert_obj->event.manual) {
                    alert_obj->event.signaled = 0;
                }
                spinlock_release(&alert_obj->lock);
                args->index = args->count;
                return 0;
            }
            spinlock_release(&alert_obj->lock);
        }
        
        /* Sleep briefly and retry */
        /* TODO: Proper wait queue implementation */
        sched_yield();
    }
    
    return -ETIMEDOUT;
}

/*
 * ============================================================
 * Instance ioctl dispatcher
 * ============================================================
 */

static int ntsync_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    ntsync_instance_t *inst = (ntsync_instance_t *)(uintptr_t)node->impl;
    if (!inst) return -EINVAL;
    
    switch (request) {
    case NTSYNC_IOC_CREATE_SEM:
        return ntsync_create_object(inst, NTSYNC_OBJ_SEM, arg);
        
    case NTSYNC_IOC_CREATE_MUTEX:
        return ntsync_create_object(inst, NTSYNC_OBJ_MUTEX, arg);
        
    case NTSYNC_IOC_CREATE_EVENT:
        return ntsync_create_object(inst, NTSYNC_OBJ_EVENT, arg);
        
    case NTSYNC_IOC_WAIT_ANY:
        return ntsync_wait_any(inst, (struct ntsync_wait_args *)arg);
        
    case NTSYNC_IOC_WAIT_ALL:
        return ntsync_wait_all(inst, (struct ntsync_wait_args *)arg);
        
    default:
        return -ENOSYS;
    }
}

/*
 * ============================================================
 * Device Open/Close
 * ============================================================
 */

static fs_node_t ntsync_device;

/* Note: VFS open callback takes only node ptr, no flags */
static void ntsync_open_callback(fs_node_t *node) {
    /* Allocate new instance */
    ntsync_instance_t *inst = kmalloc(sizeof(ntsync_instance_t));
    if (!inst) {
        node->impl = 0;
        return;
    }
    
    memset(inst, 0, sizeof(*inst));
    
    /* Setup fs_node for this instance */
    memset(&inst->node, 0, sizeof(fs_node_t));
    strcpy(inst->node.name, "ntsync_inst");
    inst->node.flags = FS_CHARDEVICE;
    inst->node.impl = (uint32_t)(uintptr_t)inst;
    inst->node.ioctl = ntsync_ioctl;
    
    /* Store in the node's impl for later access */
    node->impl = (uint32_t)(uintptr_t)inst;
}

static void ntsync_close(fs_node_t *node) {
    ntsync_instance_t *inst = (ntsync_instance_t *)(uintptr_t)node->impl;
    if (!inst) return;
    
    /* Clean up all objects */
    spinlock_acquire(&inst->lock);
    for (int i = 0; i < inst->object_count; i++) {
        ntsync_object_unref(inst->objects[i]);
    }
    if (inst->objects) {
        kfree(inst->objects);
    }
    spinlock_release(&inst->lock);
    
    kfree(inst);
    node->impl = 0;
}

/*
 * ============================================================
 * Driver Initialization
 * ============================================================
 */

void ntsync_init(void) {
    memset(&ntsync_device, 0, sizeof(fs_node_t));
    strcpy(ntsync_device.name, "ntsync");
    ntsync_device.flags = FS_CHARDEVICE;
    ntsync_device.open = ntsync_open_callback;
    ntsync_device.close = ntsync_close;
    ntsync_device.ioctl = ntsync_ioctl;
    
    devfs_register_device(&ntsync_device);
    
    kprint("  ntsync: Windows NT sync primitive driver initialized\n");
}
