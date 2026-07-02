/*
 * semaphore.c — POSIX.1-2017 counting semaphores (named + unnamed).
 *
 * Two implementations sit behind one sem_t, selected by sem->__sem_kind:
 *
 *   __SEM_LOCAL  — a process-local unnamed semaphore (sem_init, pshared == 0).
 *                  The count lives in __sem_id and waiters block in the kernel
 *                  via SYS_FUTEX (FUTEX_WAIT); sem_post wakes them (FUTEX_WAKE).
 *                  This is the fast path and stays entirely in userspace until
 *                  it must block, exactly as before.
 *
 *   __SEM_KANON  — a process-SHARED unnamed semaphore (sem_init, pshared != 0),
 *   __SEM_KNAMED — a named semaphore (sem_open).  Both are backed by a real
 *                  kernel object (a "ksem", sys/kern/posix_sem.c) and route
 *                  every operation through the SYS_KSEM_* syscalls.  Substrate's
 *                  futex is keyed by a per-process virtual address and never
 *                  crosses an address space (see sys/kern/futex.c), so a
 *                  genuinely shared semaphore CANNOT be a futex word in shared
 *                  memory — it has to live in the kernel.  __SEM_KNAMED sem_t's
 *                  are heap-allocated by sem_open and freed by sem_close;
 *                  __SEM_KANON sem_t's are supplied by the caller (sem_init).
 */
#include <semaphore.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/syscall.h>

long syscall(long number, ...);

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

#ifndef SEM_VALUE_MAX
#define SEM_VALUE_MAX 2147483647
#endif

/* Map a negative-errno kernel return into errno + -1; return 0 otherwise. */
static int ksem_ret(long r)
{
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    if (sem == NULL || value > SEM_VALUE_MAX) {
        errno = EINVAL;
        return -1;
    }

    if (pshared) {
        /* Process-shared: back it with an anonymous kernel ksem so it is
         * correct across fork/exec, not a per-process futex word. */
        long id = syscall(SYS_KSEM_OPEN, (long)0 /* no name */, 0,
                          (long)0666, (long)value);
        if (id < 0) {
            errno = (int)-id;
            return -1;
        }
        sem->__sem_kind = __SEM_KANON;
        sem->__sem_id   = (int)id;
        sem->__sem_pad[0] = sem->__sem_pad[1] = 0;
        return 0;
    }

    sem->__sem_kind = __SEM_LOCAL;
    sem->__sem_id   = (int)value;
    sem->__sem_pad[0] = sem->__sem_pad[1] = 0;
    return 0;
}

int sem_destroy(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__sem_kind == __SEM_KANON)
        return ksem_ret(syscall(SYS_KSEM_CLOSE, (long)sem->__sem_id));
    if (sem->__sem_kind == __SEM_KNAMED) {
        /* sem_destroy is for unnamed sems only; a named sem uses sem_close. */
        errno = EINVAL;
        return -1;
    }
    return 0;   /* __SEM_LOCAL: nothing to release */
}

/*
 * Per-process table of open named semaphores, keyed by the KERNEL ksem id.
 *
 * POSIX requires that repeated sem_open() calls for the same open object
 * return the SAME sem_t address (OPTS sem_open/15-1), so we hand back one heap
 * sem_t per kernel id, reference-counted by the number of outstanding
 * sem_open() calls that map to it.  We still issue one SYS_KSEM_OPEN per
 * sem_open() and one SYS_KSEM_CLOSE per sem_close() (identical kernel
 * open/close accounting to the un-deduped path) — the table only decides which
 * sem_t POINTER to return and when to free it.
 *
 * Keying by id (not by name) is essential: after sem_unlink() the name is
 * gone, so a later sem_open() without O_CREAT must fail at the kernel, and a
 * sem_open() with O_CREAT gets a NEW kernel id and hence a NEW, distinct sem_t
 * (OPTS sem_unlink/6-1).  A name-keyed cache would wrongly resurrect the stale
 * mapping.
 */
struct named_sem {
    int               id;
    sem_t            *sem;
    int               refcount;
    struct named_sem *next;
};
static struct named_sem *named_sems;
static volatile int      named_sems_lock;

static void nsem_lock(void)   { while (__sync_lock_test_and_set(&named_sems_lock, 1)) { } }
static void nsem_unlock(void) { __sync_lock_release(&named_sems_lock); }

sem_t *sem_open(const char *name, int oflag, ...)
{
    mode_t       mode = 0;
    unsigned int value = 0;

    if (name == NULL) {
        errno = EINVAL;
        return SEM_FAILED;
    }
    if (oflag & O_CREAT) {
        va_list ap;
        va_start(ap, oflag);
        mode  = (mode_t)va_arg(ap, int);          /* mode_t promotes to int */
        value = va_arg(ap, unsigned int);
        va_end(ap);
    }

    /* Always open the kernel object: this honours O_CREAT/O_EXCL/ENOENT/perms
     * exactly and yields the stable per-object id we dedup on. */
    long id = syscall(SYS_KSEM_OPEN, (long)name, (long)oflag,
                      (long)mode, (long)value);
    if (id < 0) {
        errno = (int)-id;
        return SEM_FAILED;
    }

    /* Already have a sem_t for this id in this process?  Return it (bump the
     * userspace refcount); the KSEM_OPEN above accounts for the kernel ref. */
    nsem_lock();
    for (struct named_sem *e = named_sems; e; e = e->next) {
        if (e->id == (int)id) {
            e->refcount++;
            sem_t *r = e->sem;
            nsem_unlock();
            return r;
        }
    }
    nsem_unlock();

    sem_t *sem = (sem_t *)malloc(sizeof(*sem));
    if (sem == NULL) {
        syscall(SYS_KSEM_CLOSE, id);              /* don't leak the descriptor */
        errno = ENOMEM;
        return SEM_FAILED;
    }
    sem->__sem_kind = __SEM_KNAMED;
    sem->__sem_id   = (int)id;
    sem->__sem_pad[0] = sem->__sem_pad[1] = 0;

    struct named_sem *e = (struct named_sem *)malloc(sizeof(*e));
    if (e == NULL) {
        /* Out of memory for the dedup entry: still return a working sem_t
         * (the same-address guarantee just won't hold for this id). */
        return sem;
    }
    e->id = (int)id;
    e->sem = sem;
    e->refcount = 1;

    nsem_lock();
    /* Re-check: another thread may have added this id concurrently.  If so,
     * adopt its sem_t and keep our extra kernel ref (counted by refcount++);
     * do NOT KSEM_CLOSE here — the shared sem_t's closes will balance it. */
    for (struct named_sem *cur = named_sems; cur; cur = cur->next) {
        if (cur->id == (int)id) {
            cur->refcount++;
            sem_t *r = cur->sem;
            nsem_unlock();
            free(e);
            free(sem);
            return r;
        }
    }
    e->next = named_sems;
    named_sems = e;
    nsem_unlock();
    return sem;
}

int sem_close(sem_t *sem)
{
    if (sem == NULL || sem->__sem_kind != __SEM_KNAMED) {
        errno = EINVAL;
        return -1;
    }

    /* One KSEM_CLOSE per sem_close() balances the KSEM_OPEN each sem_open()
     * issued.  The sem_t itself is freed only when the last outstanding
     * sem_open() for this id is closed. */
    nsem_lock();
    struct named_sem **pp = &named_sems, *found = NULL;
    while (*pp) {
        if ((*pp)->sem == sem) { found = *pp; break; }
        pp = &(*pp)->next;
    }
    int last = 1;
    if (found) {
        if (--found->refcount > 0) last = 0;
        else *pp = found->next;        /* last reference: unlink */
    }
    nsem_unlock();

    int r = ksem_ret(syscall(SYS_KSEM_CLOSE, (long)sem->__sem_id));
    if (found && last) free(found);
    if (last) free(sem);               /* sem_open malloc'd it */
    return r;
}

int sem_unlink(const char *name)
{
    if (name == NULL) {
        errno = EINVAL;
        return -1;
    }
    return ksem_ret(syscall(SYS_KSEM_UNLINK, (long)name));
}

int sem_post(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__sem_kind != __SEM_LOCAL)
        return ksem_ret(syscall(SYS_KSEM_POST, (long)sem->__sem_id));

    __sync_fetch_and_add(&sem->__sem_id, 1);
    syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAKE, 1, 0, 0, 0);
    return 0;
}

int sem_trywait(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__sem_kind != __SEM_LOCAL)
        return ksem_ret(syscall(SYS_KSEM_TRYWAIT, (long)sem->__sem_id));

    for (;;) {
        int v = sem->__sem_id;
        if (v <= 0) {
            errno = EAGAIN;
            return -1;
        }
        if (__sync_bool_compare_and_swap(&sem->__sem_id, v, v - 1)) {
            return 0;
        }
    }
}

int sem_wait(sem_t *sem)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__sem_kind != __SEM_LOCAL)
        return ksem_ret(syscall(SYS_KSEM_WAIT, (long)sem->__sem_id));

    for (;;) {
        int v = sem->__sem_id;
        if (v > 0) {
            if (__sync_bool_compare_and_swap(&sem->__sem_id, v, v - 1)) {
                return 0;
            }
            continue;
        }
        /* count is 0: block until a post raises it (re-checked on wake) */
        syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAIT, 0, 0, 0, 0);
    }
}

int sem_timedwait(sem_t *restrict sem, const struct timespec *restrict abs_timeout)
{
    if (sem == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__sem_kind != __SEM_LOCAL) {
        /* The kernel ksem takes the absolute CLOCK_REALTIME deadline directly
         * and returns ETIMEDOUT when it lapses. */
        return ksem_ret(syscall(SYS_KSEM_TIMEDWAIT, (long)sem->__sem_id,
                                (long)abs_timeout));
    }

    for (;;) {
        int v = sem->__sem_id;
        if (v > 0) {
            if (__sync_bool_compare_and_swap(&sem->__sem_id, v, v - 1)) {
                return 0;
            }
            continue;
        }
        if (abs_timeout != NULL) {
            /* About to block on a malformed timeout — POSIX requires EINVAL
             * (sem_timedwait/6-1,6-2,9-1).  Checked only here, on the
             * blocking path; a nonblocking decrement never consults it. */
            if (abs_timeout->tv_nsec < 0 ||
                abs_timeout->tv_nsec >= 1000000000L) {
                errno = EINVAL;
                return -1;
            }
            struct timespec now, rel;
            if (clock_gettime(CLOCK_REALTIME, &now) == 0) {
                rel.tv_sec = abs_timeout->tv_sec - now.tv_sec;
                rel.tv_nsec = abs_timeout->tv_nsec - now.tv_nsec;
                if (rel.tv_nsec < 0) { rel.tv_sec--; rel.tv_nsec += 1000000000L; }
                if (rel.tv_sec < 0) { errno = ETIMEDOUT; return -1; }
                syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAIT, 0,
                        (long)&rel, 0, 0);
                continue;
            }
        }
        syscall(SYS_FUTEX, (long)&sem->__sem_id, FUTEX_WAIT, 0, 0, 0, 0);
    }
}

int sem_getvalue(sem_t *restrict sem, int *restrict sval)
{
    if (sem == NULL || sval == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (sem->__sem_kind != __SEM_LOCAL)
        return ksem_ret(syscall(SYS_KSEM_GETVALUE, (long)sem->__sem_id,
                                (long)sval));

    int v = sem->__sem_id;
    *sval = v < 0 ? 0 : v;
    return 0;
}
