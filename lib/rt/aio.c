/*
 * aio.c — POSIX asynchronous I/O (librt).
 *
 * Substrate has no kernel AIO; this implements the POSIX aio_* surface as a
 * userspace worker-thread pool over libpthread — the same model glibc's librt
 * uses.  Submitted requests are queued and a small pool of worker threads runs
 * the blocking read/write/fsync, then records the result on a per-request
 * object hung off the aiocb (aiocb.__aio_impl).  aio_error / aio_return /
 * aio_suspend / aio_cancel read that state; completion notification
 * (SIGEV_SIGNAL / SIGEV_THREAD) fires from the worker (or, for a request that
 * is cancelled while still queued, from aio_cancel).
 *
 * Per-fd serialization: at most one worker runs I/O for a given file
 * descriptor at a time and same-fd requests run in submission (FIFO) order —
 * matching glibc.  This is what makes aio_cancel's "some done, one blocked,
 * the rest cancelable" model deterministic, keeps O_APPEND writes ordered, and
 * avoids a swarm of workers hammering one inode concurrently.
 *
 * Prioritized I/O (_POSIX_PRIORITIZED_IO): the work queue is kept in
 * aio_reqprio order (lower value = higher priority, serviced first; FIFO
 * among equals), so a higher-priority request overtakes lower-priority ones
 * waiting for the same or a different fd.
 *
 * Bound (AIO_MAX / _SC_AIO_MAX): the number of outstanding requests
 * (submitted but not yet reaped by aio_return) is capped; aio_read /
 * aio_write / lio_listio fail with EAGAIN once the cap is reached.
 *
 * All shared state is protected by a single mutex g_lock.  g_work_cv wakes a
 * worker when a request becomes runnable; g_done_cv wakes aio_suspend when any
 * request completes.
 */
#include <aio.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>     /* AIO_MAX */

/* Per-request state. */
enum { REQ_INPROGRESS, REQ_DONE, REQ_CANCELED };
enum { OP_READ, OP_WRITE, OP_FSYNC };

/* lio_listio group: fire sig once the last member completes (LIO_NOWAIT). */
struct aio_group {
    int             remaining;
    int             have_sev;
    struct sigevent sev;
};

struct aio_req {
    struct aiocb    *cb;
    int              op;
    int              state;
    int              queued;      /* still on the work queue (cancelable) */
    int              err;         /* errno result (0 = success) */
    ssize_t          ret;         /* bytes transferred, or -1 */
    struct sigevent  sev;         /* completion notification, copied at submit
                                   * time so we never deref cb once the request
                                   * is done/cancelled (the app may have freed
                                   * or reused the aiocb by then). */
    struct aio_group *group;
    struct aio_req  *q_next;      /* work-queue FIFO link */
    struct aio_req  *all_next;    /* g_all list link */
};

#define AIO_NWORKERS 4

static pthread_mutex_t g_lock  = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_work_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  g_done_cv = PTHREAD_COND_INITIALIZER;
static pthread_once_t  g_once  = PTHREAD_ONCE_INIT;

static struct aio_req *g_q_head, *g_q_tail;   /* pending work FIFO */
static struct aio_req *g_all;                 /* every live request */

/* Number of outstanding aio requests — submitted (aio_read/aio_write/
 * lio_listio) but not yet reaped by aio_return.  Bounded by AIO_MAX:
 * aio_new_req refuses (EAGAIN) once the bound is reached, which is what
 * sysconf(_SC_AIO_MAX) advertises.  All accesses hold g_lock. */
static int g_outstanding;

/* Set of fds currently being serviced by a worker.  At most AIO_NWORKERS can
 * be active at once (one per busy worker), and a given fd appears at most once
 * (a fd is not re-dispatched while active), so a fixed array suffices.  All
 * accesses hold g_lock. */
static int g_active_fd[AIO_NWORKERS];
static int g_nactive;

static int fd_is_active(int fd)
{
    for (int i = 0; i < g_nactive; i++)
        if (g_active_fd[i] == fd)
            return 1;
    return 0;
}

static void fd_mark_active(int fd)
{
    if (g_nactive < AIO_NWORKERS)
        g_active_fd[g_nactive++] = fd;
}

static void fd_clear_active(int fd)
{
    for (int i = 0; i < g_nactive; i++) {
        if (g_active_fd[i] == fd) {
            g_active_fd[i] = g_active_fd[--g_nactive];
            return;
        }
    }
}

/* ---- notification ---- */

struct thread_notify_arg {
    void (*fn)(union sigval);
    union sigval val;
};

static void *thread_notify_trampoline(void *p)
{
    struct thread_notify_arg *a = (struct thread_notify_arg *)p;
    void (*fn)(union sigval) = a->fn;
    union sigval val = a->val;
    free(a);
    fn(val);
    return NULL;
}

/* Deliver one completion notification (called with g_lock NOT held). */
static void aio_do_notify(const struct sigevent *sev)
{
    if (!sev)
        return;
    if (sev->sigev_notify == SIGEV_SIGNAL) {
        if (sev->sigev_signo > 0)
            sigqueue(getpid(), sev->sigev_signo, sev->sigev_value);
    } else if (sev->sigev_notify == SIGEV_THREAD) {
        if (sev->sigev_notify_function) {
            struct thread_notify_arg *a = malloc(sizeof(*a));
            if (a) {
                a->fn  = sev->sigev_notify_function;
                a->val = sev->sigev_value;
                pthread_t t;
                pthread_attr_t attr;
                pthread_attr_t *ap = NULL;
                if (sev->sigev_notify_attributes)
                    ap = (pthread_attr_t *)sev->sigev_notify_attributes;
                else if (pthread_attr_init(&attr) == 0) {
                    pthread_attr_setdetachstate(&attr,
                                                PTHREAD_CREATE_DETACHED);
                    ap = &attr;
                }
                if (pthread_create(&t, ap, thread_notify_trampoline, a) != 0) {
                    free(a);
                } else if (ap == &attr) {
                    pthread_detach(t);
                }
                if (ap == &attr)
                    pthread_attr_destroy(&attr);
            }
        }
    }
    /* SIGEV_NONE: nothing to do. */
}

/* Complete a request's group accounting (caller holds g_lock).  Returns a
 * heap sigevent to deliver after unlock, or NULL. */
static struct sigevent *aio_group_complete(struct aio_req *r)
{
    struct aio_group *g = r->group;
    if (!g)
        return NULL;
    r->group = NULL;
    if (--g->remaining > 0)
        return NULL;
    struct sigevent *out = NULL;
    if (g->have_sev) {
        out = malloc(sizeof(*out));
        if (out)
            *out = g->sev;
    }
    free(g);
    return out;
}

/* ---- the blocking I/O itself ---- */

/*
 * Run the request's I/O synchronously and return the byte count (errno set on
 * failure, as for read/write/fsync).
 *
 * substrate has no pread/pwrite syscall — libc emulates them with
 * lseek+read/write, which fails ESPIPE on a non-seekable descriptor (socket,
 * pipe, FIFO).  For those we fall back to plain read/write, where the offset
 * is meaningless anyway.  O_APPEND writes ignore aio_offset and append at EOF
 * (per-fd serialization keeps the seek+write pair race-free); a write to a
 * descriptor not open for writing reports EBADF.
 */
static ssize_t aio_run_io(int op, struct aiocb *cb)
{
    int      fd     = cb->aio_fildes;
    void    *buf    = (void *)cb->aio_buf;
    size_t   nbytes = cb->aio_nbytes;
    off_t    offset = cb->aio_offset;
    ssize_t  res;

    if (op == OP_FSYNC)
        return fsync(fd);

    if (op == OP_READ) {
        res = pread(fd, buf, nbytes, offset);
        if (res < 0 && errno == ESPIPE)      /* non-seekable: socket/pipe */
            res = read(fd, buf, nbytes);
        return res;
    }

    /* OP_WRITE */
    int fl = fcntl(fd, F_GETFL);
    if (fl >= 0 && (fl & O_ACCMODE) == O_RDONLY) {
        errno = EBADF;                       /* descriptor not open for writing */
        return -1;
    }
    if (fl >= 0 && (fl & O_APPEND)) {
        lseek(fd, 0, SEEK_END);              /* append; aio_offset ignored */
        return write(fd, buf, nbytes);
    }
    res = pwrite(fd, buf, nbytes, offset);
    if (res < 0 && errno == ESPIPE)          /* non-seekable: socket/pipe */
        res = write(fd, buf, nbytes);
    return res;
}

/* ---- worker pool ---- */

static void *aio_worker(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_lock);

        /* Pick the first queued request whose fd is not already being
         * serviced by another worker (per-fd serialization, FIFO order). */
        struct aio_req *r = NULL;
        for (;;) {
            struct aio_req **pp = &g_q_head;
            struct aio_req  *prev = NULL;
            while (*pp) {
                if (!fd_is_active((*pp)->cb->aio_fildes))
                    break;
                prev = *pp;
                pp = &(*pp)->q_next;
            }
            if (*pp) {
                r = *pp;
                *pp = r->q_next;
                if (g_q_tail == r)
                    g_q_tail = prev;
                break;
            }
            pthread_cond_wait(&g_work_cv, &g_lock);
        }

        r->queued = 0;
        r->q_next = NULL;
        struct aiocb *cb = r->cb;
        int op = r->op;
        fd_mark_active(cb->aio_fildes);
        pthread_mutex_unlock(&g_lock);

        /* Run the blocking I/O. */
        errno = 0;
        ssize_t res = aio_run_io(op, cb);
        int e = (res < 0) ? errno : 0;

        pthread_mutex_lock(&g_lock);
        r->ret   = res;
        r->err   = (res < 0) ? e : 0;
        r->state = REQ_DONE;
        fd_clear_active(cb->aio_fildes);
        /* Snapshot the notification while we still hold the lock: once we
         * release it and REQ_DONE becomes observable, aio_return() may free
         * r and the app may free/reuse cb, so neither may be touched after. */
        struct sigevent notify_sev = r->sev;
        struct sigevent *gsev = aio_group_complete(r);
        pthread_cond_broadcast(&g_done_cv);
        /* Freeing this fd may make a queued same-fd request runnable. */
        pthread_cond_broadcast(&g_work_cv);
        pthread_mutex_unlock(&g_lock);

        /* Per-request notification, then any group notification. */
        aio_do_notify(&notify_sev);
        if (gsev) {
            aio_do_notify(gsev);
            free(gsev);
        }
    }
    return NULL;
}

static void aio_pool_init(void)
{
    for (int i = 0; i < AIO_NWORKERS; i++) {
        pthread_t t;
        pthread_attr_t attr;
        if (pthread_attr_init(&attr) == 0) {
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            pthread_create(&t, &attr, aio_worker, NULL);
            pthread_attr_destroy(&attr);
        }
    }
}

/* ---- submission ---- */

/* Enqueue a request (caller holds g_lock).
 *
 * Prioritized I/O (_POSIX_PRIORITIZED_IO): the request is inserted in
 * priority order — a lower aio_reqprio is a higher priority and is serviced
 * first — with FIFO (submission) order preserved among equal priorities.
 * The worker scans the queue from the head for the first request whose fd is
 * not already active, so this ordering directly determines dispatch order.
 * When every request shares a priority (the common case, e.g. the default
 * aio_reqprio == 0) the insert walks to the tail and the queue stays a plain
 * FIFO, identical to the unprioritized behaviour. */
static void aio_enqueue(struct aio_req *r)
{
    r->queued = 1;
    int prio = r->cb->aio_reqprio;
    struct aio_req **pp = &g_q_head;
    while (*pp && (*pp)->cb->aio_reqprio <= prio)
        pp = &(*pp)->q_next;
    r->q_next = *pp;
    *pp = r;
    if (r->q_next == NULL)          /* inserted at the end -> new tail */
        g_q_tail = r;
    r->all_next = g_all;
    g_all = r;
}

/* Allocate + initialise a request for cb and enqueue it (caller holds g_lock).
 * Returns the request, or NULL on OOM (errno=EAGAIN). */
static struct aio_req *aio_new_req(struct aiocb *cb, int op,
                                   struct aio_group *group)
{
    /* POSIX: fail with EAGAIN when the AIO_MAX limit on the number of
     * outstanding operations would be exceeded. */
    if (g_outstanding >= AIO_MAX) {
        errno = EAGAIN;
        return NULL;
    }
    struct aio_req *r = calloc(1, sizeof(*r));
    if (!r) {
        errno = EAGAIN;
        return NULL;
    }
    r->cb    = cb;
    r->op    = op;
    r->state = REQ_INPROGRESS;
    r->ret   = -1;
    r->err   = EINPROGRESS;
    /* Snapshot the completion event now, while cb is guaranteed live.  The
     * worker (and aio_cancel) notify from this copy after the request is
     * observably done — at which point a conforming app may already have
     * freed/reused the aiocb, so cb must not be dereferenced. */
    r->sev   = cb->aio_sigevent;
    r->group = group;
    cb->__aio_impl = r;
    aio_enqueue(r);
    g_outstanding++;
    return r;
}

static int aio_submit(struct aiocb *cb, int op, struct aio_group *group)
{
    pthread_once(&g_once, aio_pool_init);

    pthread_mutex_lock(&g_lock);
    struct aio_req *r = aio_new_req(cb, op, group);
    if (r)
        pthread_cond_broadcast(&g_work_cv);
    pthread_mutex_unlock(&g_lock);
    return r ? 0 : -1;
}

int aio_read(struct aiocb *aiocbp)
{
    if (!aiocbp) { errno = EINVAL; return -1; }
    if (aiocbp->aio_reqprio < 0 || aiocbp->aio_offset < 0) {
        errno = EINVAL;
        return -1;
    }
    aiocbp->aio_lio_opcode = LIO_READ;
    return aio_submit(aiocbp, OP_READ, NULL);
}

int aio_write(struct aiocb *aiocbp)
{
    if (!aiocbp) { errno = EINVAL; return -1; }
    if (aiocbp->aio_reqprio < 0 || aiocbp->aio_offset < 0) {
        errno = EINVAL;
        return -1;
    }
    aiocbp->aio_lio_opcode = LIO_WRITE;
    return aio_submit(aiocbp, OP_WRITE, NULL);
}

int aio_fsync(int op, struct aiocb *aiocbp)
{
    if (!aiocbp) { errno = EINVAL; return -1; }
    if (op != O_SYNC && op != O_DSYNC) { errno = EINVAL; return -1; }
    /* POSIX: fail with EBADF if aio_fildes is not a valid descriptor. */
    if (fcntl(aiocbp->aio_fildes, F_GETFL) == -1) { errno = EBADF; return -1; }
    return aio_submit(aiocbp, OP_FSYNC, NULL);
}

/* ---- status ---- */

int aio_error(const struct aiocb *aiocbp)
{
    /* POSIX ERRORS: aio_error() may fail with [EINVAL] if aiocbp does not
     * refer to an asynchronous operation whose return status has not yet
     * been retrieved.  Such an aiocb carries no request object (never
     * submitted, or already consumed by aio_return); return the error
     * status EINVAL directly, as aio_error() reports status by return
     * value rather than via errno. */
    if (!aiocbp || !aiocbp->__aio_impl)
        return EINVAL;
    struct aio_req *r = (struct aio_req *)aiocbp->__aio_impl;
    pthread_mutex_lock(&g_lock);
    int s = r->state;
    int e = r->err;
    pthread_mutex_unlock(&g_lock);
    if (s == REQ_INPROGRESS) return EINPROGRESS;
    if (s == REQ_CANCELED)   return ECANCELED;
    return e;
}

ssize_t aio_return(struct aiocb *aiocbp)
{
    if (!aiocbp || !aiocbp->__aio_impl) { errno = EINVAL; return -1; }
    struct aio_req *r = (struct aio_req *)aiocbp->__aio_impl;
    pthread_mutex_lock(&g_lock);
    if (r->state == REQ_INPROGRESS) {
        pthread_mutex_unlock(&g_lock);
        errno = EINPROGRESS;
        return -1;
    }
    ssize_t ret = r->ret;
    int e = r->err;
    /* The operation's status is now retrieved: it is no longer outstanding. */
    if (g_outstanding > 0)
        g_outstanding--;
    /* Unlink from g_all and free — aio_return consumes the request. */
    struct aio_req **pp = &g_all;
    while (*pp) {
        if (*pp == r) { *pp = r->all_next; break; }
        pp = &(*pp)->all_next;
    }
    aiocbp->__aio_impl = NULL;
    pthread_mutex_unlock(&g_lock);
    free(r);
    if (ret < 0)
        errno = e ? e : ECANCELED;
    return ret;
}

/* ---- suspend ---- */

int aio_suspend(const struct aiocb *const list[], int nent,
                const struct timespec *timeout)
{
    struct timespec abs;
    int have_abs = 0;
    if (timeout) {
        clock_gettime(CLOCK_REALTIME, &abs);
        abs.tv_sec  += timeout->tv_sec;
        abs.tv_nsec += timeout->tv_nsec;
        if (abs.tv_nsec >= 1000000000L) {
            abs.tv_sec += abs.tv_nsec / 1000000000L;
            abs.tv_nsec %= 1000000000L;
        }
        have_abs = 1;
    }

    pthread_mutex_lock(&g_lock);
    for (;;) {
        int any_pending = 0;
        for (int i = 0; i < nent; i++) {
            const struct aiocb *cb = list[i];
            if (!cb || !cb->__aio_impl)
                continue;
            struct aio_req *r = (struct aio_req *)cb->__aio_impl;
            if (r->state != REQ_INPROGRESS) {
                pthread_mutex_unlock(&g_lock);
                return 0;       /* at least one has completed */
            }
            any_pending = 1;
        }
        if (!any_pending) {
            pthread_mutex_unlock(&g_lock);
            return 0;           /* nothing to wait for */
        }
        int rc;
        if (have_abs)
            rc = pthread_cond_timedwait(&g_done_cv, &g_lock, &abs);
        else
            rc = pthread_cond_wait(&g_done_cv, &g_lock);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&g_lock);
            errno = EAGAIN;
            return -1;
        }
    }
}

/* ---- cancel ---- */

/*
 * A small growable vector of sigevents.  aio_cancel defers completion
 * notifications past its g_lock-held scan (so it never drops the lock mid-
 * iteration and invalidates the g_all cursor).  A fixed array would silently
 * drop notifications — and leak the malloc'd group sigevent — once an app
 * cancels more matching requests than the array holds; this grows instead.
 */
struct sev_vec {
    struct sigevent *v;
    unsigned         n, cap;
};

/* Append a copy of *s.  On OOM the notification is dropped (best-effort, as
 * aio_do_notify already is when pthread_create/malloc fail) — never leaked. */
static void sev_vec_push(struct sev_vec *sv, const struct sigevent *s)
{
    if (sv->n == sv->cap) {
        unsigned ncap = sv->cap ? sv->cap * 2u : 8u;
        struct sigevent *nv = realloc(sv->v, (size_t)ncap * sizeof(*nv));
        if (!nv)
            return;
        sv->v = nv;
        sv->cap = ncap;
    }
    sv->v[sv->n++] = *s;
}

int aio_cancel(int fildes, struct aiocb *aiocbp)
{
    /* EBADF if fildes is not a valid descriptor. */
    if (fcntl(fildes, F_GETFL) == -1) {
        errno = EBADF;
        return -1;
    }

    int canceled = 0, notcanceled = 0, alldone = 0;
    /* Notifications are deferred to after the scan so we never drop the lock
     * mid-iteration (which could invalidate the g_all cursor).  A cancelled
     * request must still deliver its completion notification (POSIX).  Only
     * sigevent *values* are collected: once the cancel is observable the app
     * may free/reuse the aiocb (and aio_return may free r), so neither cb nor
     * r may be dereferenced after the lock is dropped. */
    struct sev_vec reqsev = {0};   /* per-request completion events */
    struct sev_vec grpsev = {0};   /* lio_listio group events */

    pthread_mutex_lock(&g_lock);
    for (struct aio_req *r = g_all; r; r = r->all_next) {
        if (r->cb->aio_fildes != fildes)
            continue;
        if (aiocbp && r->cb != aiocbp)
            continue;

        if (r->queued) {
            /* Not yet started: remove from the work queue and cancel it. */
            struct aio_req **pp = &g_q_head;
            struct aio_req *prev = NULL;
            while (*pp && *pp != r) { prev = *pp; pp = &(*pp)->q_next; }
            if (*pp == r) {
                *pp = r->q_next;
                if (g_q_tail == r)
                    g_q_tail = prev;
            }
            r->queued = 0;
            r->q_next = NULL;
            r->state  = REQ_CANCELED;
            r->err    = ECANCELED;
            r->ret    = -1;
            sev_vec_push(&reqsev, &r->sev);
            struct sigevent *gsev = aio_group_complete(r);
            if (gsev) {
                sev_vec_push(&grpsev, gsev);
                free(gsev);          /* copied by value; free now (no leak) */
            }
            canceled++;
        } else if (r->state == REQ_INPROGRESS) {
            notcanceled++;      /* running — cannot interrupt blocking I/O */
        } else {
            alldone++;
        }
    }
    if (canceled)
        pthread_cond_broadcast(&g_done_cv);
    pthread_mutex_unlock(&g_lock);

    /* Per-request notifications for successfully cancelled requests, then any
     * lio_listio group notifications that the cancel completed. */
    for (unsigned i = 0; i < reqsev.n; i++)
        aio_do_notify(&reqsev.v[i]);
    for (unsigned i = 0; i < grpsev.n; i++)
        aio_do_notify(&grpsev.v[i]);
    free(reqsev.v);
    free(grpsev.v);

    if (notcanceled > 0)
        return AIO_NOTCANCELED;
    if (canceled > 0)
        return AIO_CANCELED;
    (void)alldone;
    return AIO_ALLDONE;
}

/* ---- lio_listio ---- */

int lio_listio(int mode, struct aiocb *const restrict list[restrict],
               int nent, struct sigevent *restrict sig)
{
    if (mode != LIO_WAIT && mode != LIO_NOWAIT) {
        errno = EINVAL;
        return -1;
    }
    /* POSIX: EINVAL if nent is negative or greater than {AIO_LISTIO_MAX}. */
    if (nent < 0 || nent > AIO_LISTIO_MAX) {
        errno = EINVAL;
        return -1;
    }
    pthread_once(&g_once, aio_pool_init);

    int had_err = 0;      /* an entry could not be queued -> EIO */

    /* Count the real operations and flag invalid opcodes up front. */
    int nops = 0;
    for (int i = 0; i < nent; i++) {
        struct aiocb *cb = list[i];
        if (!cb)
            continue;
        int oc = cb->aio_lio_opcode;
        if (oc == LIO_NOP)
            continue;
        if (oc != LIO_READ && oc != LIO_WRITE) {
            had_err = 1;        /* invalid opcode */
            continue;
        }
        nops++;
    }

    struct aio_group *group = NULL;
    if (mode == LIO_NOWAIT && nops > 0) {
        group = calloc(1, sizeof(*group));
        if (group) {
            group->remaining = nops;
            if (sig) { group->sev = *sig; group->have_sev = 1; }
        }
    }

    /* Submit every valid op under one lock hold so per-fd ordering across the
     * batch is FIFO and no worker can decrement the group counter mid-submit. */
    pthread_mutex_lock(&g_lock);
    int submitted = 0;
    for (int i = 0; i < nent; i++) {
        struct aiocb *cb = list[i];
        if (!cb)
            continue;
        int oc = cb->aio_lio_opcode;
        if (oc != LIO_READ && oc != LIO_WRITE)   /* LIO_NOP or invalid */
            continue;
        int op = (oc == LIO_READ) ? OP_READ : OP_WRITE;
        if (aio_new_req(cb, op, group))
            submitted++;
        else
            had_err = 1;        /* OOM: this op could not be queued */
    }
    if (group) {
        group->remaining = submitted;   /* correct for any failed submits */
        if (submitted == 0) { free(group); group = NULL; }
    }
    if (submitted)
        pthread_cond_broadcast(&g_work_cv);
    pthread_mutex_unlock(&g_lock);

    if (mode == LIO_WAIT) {
        /* Block until every submitted op completes. */
        for (int i = 0; i < nent; i++) {
            struct aiocb *cb = list[i];
            if (!cb || !cb->__aio_impl)
                continue;
            const struct aiocb *one[1] = { cb };
            aio_suspend(one, 1, NULL);
        }
        if (had_err) { errno = EIO; return -1; }
        return 0;
    }

    /* LIO_NOWAIT. */
    if (had_err) { errno = EIO; return -1; }
    return 0;
}
