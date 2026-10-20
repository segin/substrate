/*
 * posix_sem.c — POSIX named / kernel semaphores (the "ksem" objects).
 *
 * A fixed table of KSEM_MAX kernel semaphore objects backs POSIX named
 * semaphores (sem_open/sem_close/sem_unlink) and process-shared unnamed
 * semaphores (sem_init with pshared != 0).  This mirrors FreeBSD's ksem model
 * and is structurally the SysV-semaphore code's sibling (sys/kern/ipc_sem.c):
 * a small table scanned linearly, an id that encodes a slot index and a
 * per-slot sequence number (so a stale id from a removed-and-recreated slot is
 * rejected), interruptible sleepq blocking, and an id-keyed lookup.
 *
 * Why a kernel object rather than a futex-in-shared-memory: substrate's futex
 * is keyed by a per-process VIRTUAL address and is never cross-process (see
 * sys/kern/futex.c).  A shared semaphore therefore has to live somewhere both
 * processes can name — the kernel.  The unnamed-but-process-shared case is an
 * anonymous ksem (no name in the table); the named case is a ksem with a
 * leading-'/' name.
 *
 * Lifetime (POSIX persistence): each sem_open bumps an open-descriptor
 * refcount and records a (pid, slot) so a process that exits without closing is
 * cleaned up (ksem_proc_cleanup, from proc_exit); fork() copies the parent's
 * records to the child (ksem_proc_fork).  Wait, post and getvalue need an
 * open record or the matching permission bits.  sem_unlink removes the name
 * so future opens miss it, but the object survives — existing descriptors keep
 * working — until the last descriptor is closed, at which point the slot is
 * freed.  An anonymous ksem has no name, so it is freed as soon as its refcount
 * reaches zero.
 *
 * The core (kern_ksem_*) operates on kernel memory only; the native sys_ksem_*
 * wrappers do the copyin/copyout.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <machine/intr.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <sys/compat32.h>
#include <sys/copy.h>
#include <sys/fcntl.h>
#include <sys/ipc.h>
#include <sys/kern_syscalls.h>
#include <sys/lock.h>
#include <sys/posix_sem.h>
#include <sys/proc.h>
#include <sys/time.h>
#include <sys/types.h>
#include <vm/vm_kmem.h>

struct kposix_sem {
    int             in_use;                 /* slot occupied */
    int             unlinked;               /* name removed; free on last close */
    char            name[KSEM_NAME_MAX];    /* leading '/', NUL-terminated ("" = anon) */
    struct ipc_perm perm;
    unsigned int    value;                  /* current count */
    unsigned int    seq;                    /* bumped each (re)allocation of this slot */
    int             refs;                   /* number of open descriptors */
    int             ncnt;                   /* threads blocked in wait */
    struct vm_shared_key key;               /* the sem_t's memory (sem_init) */
};

/* One record per successful open, so a process that dies with the semaphore
 * still open has its reference reclaimed at exit. */
struct ksem_open {
    struct ksem_open *next;
    int    pid;
    int    index;
    unsigned int seq;
};

static struct kposix_sem posix_sems[KSEM_MAX];
static struct ksem_open *ksem_open_list;
static mutex_t  ksem_lock;
static int      ksem_ready;

void ksem_init(void)
{
    if (ksem_ready)
        return;
    mutex_init(&ksem_lock, "posix_sem");
    ksem_ready = 1;
}

static void ksem_lazy_init(void)
{
    if (!ksem_ready)
        ksem_init();
}

/* id <-> slot encoding: id = seq * KSEM_MAX + index. */
static int ksem_make_id(int index, unsigned int seq)
{
    return (int)(seq * KSEM_MAX + (unsigned int)index);
}

/* Resolve a ksem id to its in-use slot, or NULL with *err set.  An id remains
 * valid after unlink (the descriptor keeps working); only a freed/recreated
 * slot (seq mismatch) is rejected. */
static struct kposix_sem *ksem_lookup(int id, int *err)
{
    if (id < 0) { *err = EINVAL; return NULL; }
    int index = id % KSEM_MAX;
    unsigned int seq = (unsigned int)id / KSEM_MAX;
    struct kposix_sem *s = &posix_sems[index];
    if (!s->in_use) { *err = EINVAL; return NULL; }
    if (s->seq != seq) { *err = EINVAL; return NULL; }
    return s;
}

/* Permission check.  want = read flag 4 / alter flag 2 (octal-style, matching
 * the low mode bits).  Root bypasses. */
static int ksem_perm_ok(struct kposix_sem *s, int want)
{
    process_t *p = current_process;
    if (!p || p->euid == 0)
        return 1;
    mode_t mode = s->perm.mode;
    int granted;
    if (p->euid == s->perm.uid || p->euid == s->perm.cuid)
        granted = (mode >> 6) & 7;
    else if (p->egid == s->perm.gid || p->egid == s->perm.cgid)
        granted = (mode >> 3) & 7;
    else
        granted = mode & 7;
    return (granted & want) == want;
}

/* Does pid hold an open descriptor for the object in slot index? */
static int ksem_has_open(int pid, int index, unsigned int seq)
{
    for (struct ksem_open *o = ksem_open_list; o; o = o->next)
        if (o->pid == pid && o->index == index && o->seq == seq)
            return 1;
    return 0;
}

/*
 * Resolve an id for a wait/post/getvalue by the calling process: the caller
 * must hold the semaphore open (sem_open, sem_init, or inherited across
 * fork), map the memory a sem_init semaphore lives in (`key`, the caller's
 * sem_t address resolved by vm_map_shared_key, or NULL), or have the
 * permission the operation needs (want: 4 read, 2 alter).  Ids are small and
 * sequential, so without this any process could post, drain or read any
 * other process's semaphore.  A semaphore the caller may not use is
 * reported like a nonexistent one, EINVAL.
 */
static struct kposix_sem *ksem_lookup_use(int id, int want,
                                          const struct vm_shared_key *key,
                                          int *err)
{
    struct kposix_sem *s = ksem_lookup(id, err);
    if (!s)
        return NULL;
    int pid = current_process ? current_process->pid : 0;
    if (!ksem_has_open(pid, (int)(s - posix_sems), s->seq) &&
        !vm_shared_key_equal(&s->key, key) &&
        !ksem_perm_ok(s, want)) {
        *err = EINVAL;
        return NULL;
    }
    return s;
}

/* Release a slot and invalidate outstanding ids (caller holds ksem_lock).
 * Only call once refs has reached 0. */
static void ksem_free_slot(struct kposix_sem *s)
{
    s->in_use   = 0;
    s->unlinked = 0;
    s->name[0]  = '\0';
    s->value    = 0;
    s->refs     = 0;
    s->ncnt     = 0;
    vm_shared_key_release(&s->key);
    s->seq++;               /* invalidate outstanding ids */
}

/* Record an open (caller holds ksem_lock).  Bumps refs on success. */
static int ksem_add_open(int index, unsigned int seq)
{
    struct ksem_open *o = (struct ksem_open *)kmalloc(sizeof(*o));
    if (!o)
        return -ENOMEM;
    o->pid   = current_process ? current_process->pid : 0;
    o->index = index;
    o->seq   = seq;
    o->next  = ksem_open_list;
    ksem_open_list = o;
    posix_sems[index].refs++;
    return 0;
}

/* Drop one open record for (pid, index) and decrement refs; free the slot if it
 * was the last descriptor and the object is anonymous or unlinked.  Returns 0
 * if a record was dropped, -EINVAL if the caller held no descriptor. */
static int ksem_drop_open(int pid, int index)
{
    struct ksem_open **pp = &ksem_open_list;
    while (*pp) {
        struct ksem_open *o = *pp;
        if (o->pid == pid && o->index == index) {
            *pp = o->next;
            kfree(o, sizeof(*o));
            struct kposix_sem *s = &posix_sems[index];
            if (s->in_use && s->refs > 0)
                s->refs--;
            if (s->in_use && s->refs == 0 &&
                (s->unlinked || s->name[0] == '\0'))
                ksem_free_slot(s);
            return 0;
        }
        pp = &o->next;
    }
    return -EINVAL;
}

/* ---- core ---- */

int kern_ksem_open(const char *kname, int oflag, mode_t mode, unsigned int value)
{
    ksem_lazy_init();

    if (value > KSEM_VALUE_MAX)
        return -EINVAL;
    if (kname) {
        /* A named semaphore's name must be "/something". */
        if (kname[0] != '/' || kname[1] == '\0')
            return -EINVAL;
        if (strlen(kname) >= KSEM_NAME_MAX)
            return -ENAMETOOLONG;
    }

    mutex_lock(&ksem_lock);

    struct kposix_sem *s = NULL;
    int free_index = -1;

    if (kname) {
        for (int i = 0; i < KSEM_MAX; i++) {
            if (posix_sems[i].in_use && !posix_sems[i].unlinked &&
                posix_sems[i].name[0] &&
                strcmp(posix_sems[i].name, kname) == 0) {
                s = &posix_sems[i];
                break;
            }
            if (!posix_sems[i].in_use && free_index < 0)
                free_index = i;
        }
        if (s) {
            /* Attach to the existing object. */
            if ((oflag & O_CREAT) && (oflag & O_EXCL)) {
                mutex_unlock(&ksem_lock);
                return -EEXIST;
            }
            if (!ksem_perm_ok(s, 4 | 2)) {
                mutex_unlock(&ksem_lock);
                return -EACCES;
            }
            int index = (int)(s - posix_sems);
            unsigned int seq = s->seq;
            int r = ksem_add_open(index, seq);
            mutex_unlock(&ksem_lock);
            return r ? r : ksem_make_id(index, seq);
        }
        if (!(oflag & O_CREAT)) {
            mutex_unlock(&ksem_lock);
            return -ENOENT;
        }
        /* fall through: create with this name */
    } else {
        for (int i = 0; i < KSEM_MAX; i++)
            if (!posix_sems[i].in_use) { free_index = i; break; }
    }

    if (free_index < 0) {
        mutex_unlock(&ksem_lock);
        return -ENOSPC;
    }

    /* Create a new object. */
    s = &posix_sems[free_index];
    process_t *p = current_process;
    s->in_use   = 1;
    s->unlinked = 0;
    if (kname) {
        strlcpy(s->name, kname, sizeof(s->name));
    } else {
        s->name[0] = '\0';
    }
    s->perm.__key = 0;
    s->perm.uid   = s->perm.cuid = p ? p->euid : 0;
    s->perm.gid   = s->perm.cgid = p ? p->egid : 0;
    s->perm.mode  = mode & 0777;
    s->perm.__seq = s->seq;
    s->value      = value;
    s->refs       = 0;
    s->ncnt       = 0;

    int index = free_index;
    unsigned int seq = s->seq;
    int r = ksem_add_open(index, seq);
    if (r) {
        ksem_free_slot(s);          /* creation failed: roll back the slot */
        mutex_unlock(&ksem_lock);
        return r;
    }
    mutex_unlock(&ksem_lock);
    return ksem_make_id(index, seq);
}

/*
 * sem_init(pshared): an anonymous semaphore, mode 0, bound to `key` -- the
 * memory its sem_t lives in.  Any process that maps that memory may use it
 * (POSIX), with no permission bits that would let anyone else in.  Takes
 * over the caller's reference in *key whatever happens.
 */
int kern_ksem_init(struct vm_shared_key *key, unsigned int value)
{
    int id = kern_ksem_open(NULL, 0, 0, value);
    if (id < 0) {
        vm_shared_key_release(key);
        return id;
    }
    mutex_lock(&ksem_lock);
    struct kposix_sem *s = &posix_sems[id % KSEM_MAX];
    s->key = *key;
    key->obj = NULL;
    mutex_unlock(&ksem_lock);
    return id;
}

/*
 * The physical memory [pa, pa + len) is being released (a SysV segment or a
 * shared-memory file going away).  A sem_init semaphore keyed by an address
 * in it loses that binding -- the frames may next back someone else's
 * memory, whose owner would otherwise match the old key.  Its open and
 * inherited references keep working.
 */
void ksem_unbind_memory(uintptr_t pa, size_t len)
{
    if (!ksem_ready || len == 0)
        return;
    mutex_lock(&ksem_lock);
    for (int i = 0; i < KSEM_MAX; i++) {
        struct kposix_sem *s = &posix_sems[i];
        if (s->in_use && vm_shared_key_is_phys(&s->key) &&
            s->key.off >= pa && s->key.off - pa < len)
            vm_shared_key_release(&s->key);
    }
    mutex_unlock(&ksem_lock);
}

int kern_ksem_close(int id)
{
    ksem_lazy_init();
    mutex_lock(&ksem_lock);
    int err = 0;
    struct kposix_sem *s = ksem_lookup(id, &err);
    if (!s) { mutex_unlock(&ksem_lock); return -err; }
    int pid = current_process ? current_process->pid : 0;
    int r = ksem_drop_open(pid, (int)(s - posix_sems));
    mutex_unlock(&ksem_lock);
    return r;
}

int kern_ksem_unlink(const char *kname)
{
    ksem_lazy_init();
    if (!kname)
        return -EINVAL;
    /*
     * A name that isn't a well-formed "/something" cannot name any existing
     * semaphore, so the semaphore "does not exist": POSIX lists ENOENT (not
     * EINVAL) as the sem_unlink error for a missing name (OPTS
     * sem_unlink/4-1 passes an uninitialized/garbage name and expects
     * ENOENT).
     */
    if (kname[0] != '/' || kname[1] == '\0')
        return -ENOENT;

    mutex_lock(&ksem_lock);
    struct kposix_sem *s = NULL;
    for (int i = 0; i < KSEM_MAX; i++) {
        if (posix_sems[i].in_use && !posix_sems[i].unlinked &&
            posix_sems[i].name[0] &&
            strcmp(posix_sems[i].name, kname) == 0) {
            s = &posix_sems[i];
            break;
        }
    }
    if (!s) { mutex_unlock(&ksem_lock); return -ENOENT; }

    /* POSIX: unlink requires write permission on the semaphore. */
    if (!ksem_perm_ok(s, 2)) { mutex_unlock(&ksem_lock); return -EACCES; }

    s->unlinked = 1;
    s->name[0]  = '\0';         /* future name lookups miss it */
    if (s->refs == 0)
        ksem_free_slot(s);      /* no open descriptors: free now */
    mutex_unlock(&ksem_lock);
    return 0;
}

int kern_ksem_trywait(int id, const struct vm_shared_key *key)
{
    ksem_lazy_init();
    mutex_lock(&ksem_lock);
    int err = 0;
    struct kposix_sem *s = ksem_lookup_use(id, 2, key, &err);
    if (!s) { mutex_unlock(&ksem_lock); return -err; }
    if (s->value > 0) {
        s->value--;
        mutex_unlock(&ksem_lock);
        return 0;
    }
    mutex_unlock(&ksem_lock);
    return -EAGAIN;
}

/*
 * Shared wait engine.  Decrement the count, blocking interruptibly while it is
 * zero.  `deadline` is an absolute tick count (0 = wait forever).  The object
 * cannot be freed under a blocked waiter because the waiter holds an open
 * descriptor (refs >= 1), but we re-resolve the id on every loop for safety.
 */
static int ksem_wait_common(int id, uint64_t deadline,
                            const struct vm_shared_key *key)
{
    mutex_lock(&ksem_lock);
    for (;;) {
        int err = 0;
        struct kposix_sem *s = ksem_lookup_use(id, 2, key, &err);
        if (!s) { mutex_unlock(&ksem_lock); return -err; }

        if (s->value > 0) {
            s->value--;
            mutex_unlock(&ksem_lock);
            return 0;
        }

        /* Deliver a pending unblocked signal as EINTR rather than sleeping. */
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            mutex_unlock(&ksem_lock);
            return -EINTR;
        }

        if (deadline && get_ticks() >= deadline) {
            mutex_unlock(&ksem_lock);
            return -ETIMEDOUT;
        }

        int index = (int)(s - posix_sems);
        s->ncnt++;
        current_thread->sleep_status = 0;
        current_thread->flags |= THREAD_F_INTERRUPTIBLE;
        /* Enqueue-and-release under intr_disable: a timer preempt landing
         * between sleepq_add (which marks us THREAD_BLOCKED) and mutex_unlock
         * would park us BLOCKED while still holding ksem_lock, and every
         * sem_post needs ksem_lock to wake us -> permanent deadlock.  Mirrors
         * mq_block() in posix_mqueue.c. */
        uint32_t pf = intr_disable();
        sleepq_add(&posix_sems[index], current_thread);
        if (deadline) {
            current_thread->sleep_expiry = deadline;
        } else if (current_thread->sleep_expiry == 0) {
            /* Untimed wait: arm a ~100 ms lost-wakeup net so a post that raced
             * the enqueue still recovers via sched_tick instead of hanging. */
            uint32_t hz = get_hz();
            uint64_t span = hz ? (hz / 10u) : 8u;
            if (span == 0) span = 1;
            current_thread->sleep_expiry = get_ticks() + span;
        }
        mutex_unlock(&ksem_lock);
        intr_restore(pf);

        if (current_thread->wait_chan == &posix_sems[index])
            sched_yield();

        if (current_thread)
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
        /* sched_tick timeouts leave the stale sleepq entry linked; self-unlink
         * (idempotent if a post already dequeued us). */
        current_thread->sleep_expiry = 0;
        sleepq_remove_thread(current_thread);

        mutex_lock(&ksem_lock);
        {
            int e2 = 0;
            struct kposix_sem *s2 = ksem_lookup(id, &e2);
            if (s2 && s2->ncnt > 0)
                s2->ncnt--;
        }

        if (deadline) {
            current_thread->sleep_expiry = 0;
            if (current_thread->sleep_status == -ETIMEDOUT) {
                mutex_unlock(&ksem_lock);
                return -ETIMEDOUT;
            }
        }
        if (current_thread &&
            (current_thread->sleep_status == -EINTR ||
             (current_thread->sig_pending & ~current_thread->sig_mask))) {
            mutex_unlock(&ksem_lock);
            return -EINTR;
        }
        /* loop: re-check the value */
    }
}

int kern_ksem_wait(int id, const struct vm_shared_key *key)
{
    ksem_lazy_init();
    return ksem_wait_common(id, 0, key);
}

int kern_ksem_timedwait(int id, const struct timespec *kabstime,
                        const struct vm_shared_key *key)
{
    ksem_lazy_init();
    if (!kabstime)
        return ksem_wait_common(id, 0, key);
    if (kabstime->tv_nsec < 0 || kabstime->tv_nsec >= 1000000000L)
        return -EINVAL;

    /* Convert the absolute CLOCK_REALTIME deadline into a tick count. */
    struct timespec now;
    if (kern_clock_gettime(CLOCK_REALTIME, &now) != 0)
        return -EINVAL;
    /* Already past: poll once, then ETIMEDOUT. */
    uint64_t deadline = get_ticks() +
                        abstime_ticks(kabstime->tv_sec, kabstime->tv_nsec,
                                      now.tv_sec, now.tv_nsec);
    return ksem_wait_common(id, deadline, key);
}

int kern_ksem_post(int id, const struct vm_shared_key *key)
{
    ksem_lazy_init();
    mutex_lock(&ksem_lock);
    int err = 0;
    struct kposix_sem *s = ksem_lookup_use(id, 2, key, &err);
    if (!s) { mutex_unlock(&ksem_lock); return -err; }
    if (s->value >= KSEM_VALUE_MAX) { mutex_unlock(&ksem_lock); return -ERANGE; }
    s->value++;
    sleepq_wake_one(&posix_sems[(int)(s - posix_sems)]);
    mutex_unlock(&ksem_lock);
    return 0;
}

int kern_ksem_getvalue(int id, int *ksval, const struct vm_shared_key *key)
{
    ksem_lazy_init();
    mutex_lock(&ksem_lock);
    int err = 0;
    struct kposix_sem *s = ksem_lookup_use(id, 4, key, &err);
    if (!s) { mutex_unlock(&ksem_lock); return -err; }
    *ksval = (int)s->value;
    mutex_unlock(&ksem_lock);
    return 0;
}

/* ---- descriptor reclamation at process exit ---- */

void ksem_proc_cleanup(int pid)
{
    if (!ksem_ready) return;
    mutex_lock(&ksem_lock);
    struct ksem_open **pp = &ksem_open_list;
    while (*pp) {
        struct ksem_open *o = *pp;
        if (o->pid != pid) { pp = &o->next; continue; }
        int index = o->index;
        *pp = o->next;
        kfree(o, sizeof(*o));
        struct kposix_sem *s = &posix_sems[index];
        if (s->in_use && s->refs > 0)
            s->refs--;
        if (s->in_use && s->refs == 0 &&
            (s->unlinked || s->name[0] == '\0'))
            ksem_free_slot(s);
    }
    mutex_unlock(&ksem_lock);
}

/*
 * fork(): semaphores open in the parent are open in the child (POSIX), so
 * the child gets a copy of each of the parent's open records and the
 * reference that goes with it.  On allocation failure the child is given
 * nothing and fork fails with -ENOMEM.
 */
int ksem_proc_fork(int parent_pid, int child_pid)
{
    if (!ksem_ready) return 0;
    mutex_lock(&ksem_lock);
    struct ksem_open *added = NULL;
    for (struct ksem_open *o = ksem_open_list; o; o = o->next) {
        if (o->pid != parent_pid)
            continue;
        struct ksem_open *c = (struct ksem_open *)kmalloc(sizeof(*c));
        if (!c) {
            while (added) {
                struct ksem_open *n = added->next;
                kfree(added, sizeof(*added));
                added = n;
            }
            mutex_unlock(&ksem_lock);
            return -ENOMEM;
        }
        c->pid   = child_pid;
        c->index = o->index;
        c->seq   = o->seq;
        c->next  = added;
        added = c;
    }
    /* All allocated: link them in and take the references. */
    while (added) {
        struct ksem_open *n = added->next;
        added->next = ksem_open_list;
        ksem_open_list = added;
        posix_sems[added->index].refs++;
        added = n;
    }
    mutex_unlock(&ksem_lock);
    return 0;
}

/* ---- native-ABI syscall wrappers ---- */

int sys_ksem_open(const char *uname, int oflag, mode_t mode, unsigned int value)
{
    char kname[KSEM_NAME_MAX];
    const char *namep = NULL;
    if (uname) {
        size_t len = 0;
        int r = copyinstr(uname, kname, sizeof(kname), &len);
        if (r != 0)
            return -r;                 /* copyinstr returns positive errno */
        namep = kname;
    }
    return kern_ksem_open(namep, oflag, mode, value);
}

int sys_ksem_close(int id)
{
    return kern_ksem_close(id);
}

int sys_ksem_unlink(const char *uname)
{
    char kname[KSEM_NAME_MAX];
    size_t len = 0;
    int r = copyinstr(uname, kname, sizeof(kname), &len);
    if (r != 0)
        return -r;
    return kern_ksem_unlink(kname);
}

/*
 * Resolve the caller's sem_t address to the memory it lives in.  Touching
 * it first faults in a device page (a SysV segment) the key is taken from.
 * -1, leaving *key empty, for a NULL or unmapped address: the caller then
 * has only its open references and the permission bits to go on.
 */
static int ksem_user_key(const void *usem, struct vm_shared_key *key)
{
    unsigned char probe;
    key->id = NULL;
    key->obj = NULL;
    if (!usem || !current_process || !current_process->vm_map)
        return -1;
    if (copyin(usem, &probe, sizeof(probe)) != 0)
        return -1;
    return vm_map_shared_key(current_process->vm_map, (uintptr_t)usem, key);
}

int sys_ksem_init(const void *usem, unsigned int value)
{
    struct vm_shared_key key;
    if (ksem_user_key(usem, &key) != 0)
        return -EFAULT;
    return kern_ksem_init(&key, value);
}

int sys_ksem_wait(int id, const void *usem)
{
    struct vm_shared_key key;
    ksem_user_key(usem, &key);
    int r = kern_ksem_wait(id, &key);
    vm_shared_key_release(&key);
    return r;
}

int sys_ksem_trywait(int id, const void *usem)
{
    struct vm_shared_key key;
    ksem_user_key(usem, &key);
    int r = kern_ksem_trywait(id, &key);
    vm_shared_key_release(&key);
    return r;
}

int sys_ksem_timedwait(int id, const struct timespec *uabstime,
                       const void *usem)
{
    struct timespec kts;
    if (uabstime && timespec_copyin(uabstime, &kts) != 0)
        return -EFAULT;
    struct vm_shared_key key;
    ksem_user_key(usem, &key);
    int r = kern_ksem_timedwait(id, uabstime ? &kts : NULL, &key);
    vm_shared_key_release(&key);
    return r;
}

int sys_ksem_post(int id, const void *usem)
{
    struct vm_shared_key key;
    ksem_user_key(usem, &key);
    int r = kern_ksem_post(id, &key);
    vm_shared_key_release(&key);
    return r;
}

int sys_ksem_getvalue(int id, int *usval, const void *usem)
{
    int v = 0;
    struct vm_shared_key key;
    ksem_user_key(usem, &key);
    int r = kern_ksem_getvalue(id, &v, &key);
    vm_shared_key_release(&key);
    if (r < 0)
        return r;
    if (copyout(&v, usval, sizeof(v)) != 0)
        return -EFAULT;
    return 0;
}
