/*
 * aio.c — POSIX asynchronous I/O (librt).
 *
 * Substrate has no kernel AIO; this implements the POSIX aio_* surface as a
 * userspace worker-thread pool over libpthread — the same model glibc's librt
 * uses.  Submitted requests are queued and a small pool of worker threads runs
 * the blocking pread/pwrite/fsync, then records the result on a per-request
 * object hung off the aiocb (aiocb.__aio_impl).  aio_error / aio_return /
 * aio_suspend / aio_cancel read that state; completion notification
 * (SIGEV_SIGNAL / SIGEV_THREAD) fires from the worker.
 *
 * All shared state is protected by a single mutex g_lock.  g_work_cv wakes a
 * worker when a request is queued; g_done_cv wakes aio_suspend when any
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

/* ---- worker pool ---- */

static void *aio_worker(void *arg)
{
    (void)arg;
    for (;;) {
        pthread_mutex_lock(&g_lock);
        while (!g_q_head)
            pthread_cond_wait(&g_work_cv, &g_lock);
        struct aio_req *r = g_q_head;
        g_q_head = r->q_next;
        if (!g_q_head)
            g_q_tail = NULL;
        r->queued = 0;
        r->q_next = NULL;
        struct aiocb *cb = r->cb;
        int op = r->op;
        pthread_mutex_unlock(&g_lock);

        /* Run the blocking I/O. */
        ssize_t res = 0;
        int e = 0;
        if (op == OP_READ) {
            res = pread(cb->aio_fildes, (void *)cb->aio_buf,
                        cb->aio_nbytes, cb->aio_offset);
            if (res < 0) e = errno;
        } else if (op == OP_WRITE) {
            res = pwrite(cb->aio_fildes, (const void *)cb->aio_buf,
                         cb->aio_nbytes, cb->aio_offset);
            if (res < 0) e = errno;
        } else { /* OP_FSYNC */
            res = fsync(cb->aio_fildes);
            if (res < 0) e = errno;
        }

        pthread_mutex_lock(&g_lock);
        r->ret   = res;
        r->err   = (res < 0) ? e : 0;
        r->state = REQ_DONE;
        struct sigevent *gsev = aio_group_complete(r);
        pthread_cond_broadcast(&g_done_cv);
        pthread_mutex_unlock(&g_lock);

        /* Per-request notification, then any group notification. */
        aio_do_notify(&cb->aio_sigevent);
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

/* Enqueue a request (caller holds g_lock). */
static void aio_enqueue(struct aio_req *r)
{
    r->q_next = NULL;
    r->queued = 1;
    if (g_q_tail)
        g_q_tail->q_next = r;
    else
        g_q_head = r;
    g_q_tail = r;
    r->all_next = g_all;
    g_all = r;
}

static int aio_submit(struct aiocb *cb, int op, struct aio_group *group)
{
    pthread_once(&g_once, aio_pool_init);

    struct aio_req *r = calloc(1, sizeof(*r));
    if (!r) {
        errno = EAGAIN;
        return -1;
    }
    r->cb    = cb;
    r->op    = op;
    r->state = REQ_INPROGRESS;
    r->ret   = -1;
    r->err   = EINPROGRESS;
    r->group = group;
    cb->__aio_impl = r;

    pthread_mutex_lock(&g_lock);
    aio_enqueue(r);
    pthread_cond_signal(&g_work_cv);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

int aio_read(struct aiocb *aiocbp)
{
    if (!aiocbp) { errno = EINVAL; return -1; }
    aiocbp->aio_lio_opcode = LIO_READ;
    return aio_submit(aiocbp, OP_READ, NULL);
}

int aio_write(struct aiocb *aiocbp)
{
    if (!aiocbp) { errno = EINVAL; return -1; }
    aiocbp->aio_lio_opcode = LIO_WRITE;
    return aio_submit(aiocbp, OP_WRITE, NULL);
}

int aio_fsync(int op, struct aiocb *aiocbp)
{
    if (!aiocbp) { errno = EINVAL; return -1; }
    if (op != O_SYNC && op != O_DSYNC) { errno = EINVAL; return -1; }
    return aio_submit(aiocbp, OP_FSYNC, NULL);
}

/* ---- status ---- */

int aio_error(const struct aiocb *aiocbp)
{
    if (!aiocbp || !aiocbp->__aio_impl) { errno = EINVAL; return -1; }
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

int aio_cancel(int fildes, struct aiocb *aiocbp)
{
    int canceled = 0, notcanceled = 0, alldone = 0;
    /* Group notifications are deferred to after the scan so we never drop the
     * lock mid-iteration (which could invalidate the g_all cursor). */
    struct sigevent *defer[16];
    int ndefer = 0;

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
            struct sigevent *gsev = aio_group_complete(r);
            if (gsev && ndefer < (int)(sizeof(defer)/sizeof(defer[0])))
                defer[ndefer++] = gsev;
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

    for (int i = 0; i < ndefer; i++) {
        aio_do_notify(defer[i]);
        free(defer[i]);
    }

    if (notcanceled > 0)
        return AIO_NOTCANCELED;
    if (canceled > 0)
        return REQ_CANCELED;
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

    /* Count the real operations. */
    int nops = 0;
    for (int i = 0; i < nent; i++) {
        if (list[i] && list[i]->aio_lio_opcode != LIO_NOP)
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

    int err = 0;
    for (int i = 0; i < nent; i++) {
        struct aiocb *cb = list[i];
        if (!cb || cb->aio_lio_opcode == LIO_NOP)
            continue;
        int op;
        if (cb->aio_lio_opcode == LIO_READ)  op = OP_READ;
        else if (cb->aio_lio_opcode == LIO_WRITE) op = OP_WRITE;
        else { err = EINVAL; continue; }
        if (aio_submit(cb, op, group) != 0)
            err = errno;
    }

    if (mode == LIO_WAIT) {
        /* Block until every submitted op completes. */
        for (int i = 0; i < nent; i++) {
            struct aiocb *cb = list[i];
            if (!cb || cb->aio_lio_opcode == LIO_NOP || !cb->__aio_impl)
                continue;
            const struct aiocb *one[1] = { cb };
            aio_suspend(one, 1, NULL);
        }
        if (err) { errno = err; return -1; }
        return 0;
    }

    /* LIO_NOWAIT. */
    if (err) { errno = err; return -1; }
    return 0;
}
