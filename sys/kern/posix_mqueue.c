/*
 * posix_mqueue.c — POSIX message queues (mq_open/mq_unlink/mq_timedsend/
 * mq_timedreceive/mq_notify/mq_getsetattr).
 *
 * A POSIX message queue is a NAMED, cross-process object, so — like the
 * System V IPC objects (ipc_sem.c, ipc_shm.c) — it lives in the kernel as a
 * fixed table.  This file keeps two tables under one mutex:
 *
 *   mqueues[MQ_OPEN_MAX]  — the queue objects, keyed by a leading-'/' POSIX
 *                           name.  Each carries an ipc_perm, a per-slot
 *                           sequence number, an mq_attr (maxmsg/msgsize/
 *                           curmsgs), a PRIORITY-ORDERED message list, and a
 *                           single mq_notify registration.  A queue is
 *                           reference counted by open descriptors and freed
 *                           once the last descriptor closes and the name has
 *                           been unlinked.
 *
 *   mqdescs[MQ_DESC_MAX]  — the open descriptors.  mq_open() returns an
 *                           opaque mqd_t = seq*MQ_DESC_MAX + index (so a
 *                           stale, closed-and-reused descriptor is rejected
 *                           with EBADF).  The per-open O_NONBLOCK flag and
 *                           access mode live here, matching POSIX's
 *                           per-description mq_flags.
 *
 * Blocking send/receive uses the same interruptible-sleepq idiom as pipe.c:
 * register on the queue's wait channel, arm a fallback deadline (plus the
 * caller's absolute timeout when given), drop the mutex, yield, then re-check.
 * Messages are ordered highest-priority-first, FIFO within a priority.
 * mq_notify registers ONE process for SIGEV_SIGNAL delivery on an
 * empty->non-empty transition when no receiver is blocked; the registration
 * is one-shot (cleared on delivery, on the registrant closing, or on unlink).
 *
 * The core (kern_mq_*) works on kernel memory only; the native sys_mq_*
 * wrappers do the copyin/copyout.
 */

#include <sys/mqueue.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/copy.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/kern_syscalls.h>
#include <vm/vm_kmem.h>
#include <kern/sleepq.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <pm/pm.h>
#include <arch/i386/intr.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

/* One queued message; the payload is stored inline after the header. */
struct mq_msg {
    struct mq_msg *next;
    unsigned       prio;
    size_t         len;
    /* len bytes of payload follow */
};
#define MQ_MSG_DATA(m) ((char *)((m) + 1))

struct mq_notify_reg {
    int          active;
    int          pid;
    int          notify;   /* SIGEV_SIGNAL / SIGEV_NONE */
    int          signo;
    union sigval value;
};

struct mqueue {
    int             in_use;
    int             unlinked;   /* name removed; free on last close */
    unsigned short  seq;        /* bumped each (re)allocation of this slot */
    char            name[MQ_NAME_MAX + 1];
    struct ipc_perm perm;
    long            maxmsg;
    long            msgsize;
    long            curmsgs;
    struct mq_msg  *msgs;        /* priority-ordered, head = next to receive */
    unsigned        refs;        /* open descriptors referencing this queue */
    int             send_waiters;
    int             recv_waiters;
    struct mq_notify_reg notify;
};

struct mqdesc {
    int            in_use;
    unsigned short seq;          /* bumped each (re)allocation of this slot */
    int            qindex;       /* queue slot this descriptor refers to */
    unsigned short qseq;         /* queue's seq at open time */
    int            oflag;        /* O_RDONLY/O_WRONLY/O_RDWR | O_NONBLOCK */
    int            pid;          /* owning pid (for proc-exit cleanup) */
};

static struct mqueue mqueues[MQ_OPEN_MAX];
static struct mqdesc mqdescs[MQ_DESC_MAX];
static mutex_t mq_lock;
static int     mq_ready;

void mq_init(void)
{
    if (mq_ready)
        return;
    mutex_init(&mq_lock, "posix_mq");
    mq_ready = 1;
}

static void mq_lazy_init(void)
{
    if (!mq_ready)
        mq_init();
}

/* ---- descriptor id encoding (mirrors semid/shmid) ---- */

static int mqd_make(int index, unsigned short seq)
{
    return (int)seq * MQ_DESC_MAX + index;
}

static struct mqdesc *mqd_lookup(int mqd, int *err)
{
    if (mqd < 0) { *err = EBADF; return NULL; }
    int index = mqd % MQ_DESC_MAX;
    unsigned short seq = (unsigned short)(mqd / MQ_DESC_MAX);
    struct mqdesc *d = &mqdescs[index];
    if (!d->in_use)   { *err = EBADF; return NULL; }
    if (d->seq != seq) { *err = EBADF; return NULL; }
    return d;
}

/* Resolve the queue a descriptor refers to.  NULL if the queue is gone. */
static struct mqueue *mq_from_desc(struct mqdesc *d)
{
    struct mqueue *q = &mqueues[d->qindex];
    if (!q->in_use || q->seq != d->qseq)
        return NULL;
    return q;
}

/* Permission check.  want = read flag 4 / write flag 2 (octal-style, matching
 * the low mode bits).  Root bypasses. */
static int mq_perm_ok(struct mqueue *q, int want)
{
    process_t *p = current_process;
    if (!p || p->euid == 0)
        return 1;
    mode_t mode = q->perm.mode;
    int granted;
    if (p->euid == q->perm.uid || p->euid == q->perm.cuid)
        granted = (mode >> 6) & 7;
    else if (p->egid == q->perm.gid || p->egid == q->perm.cgid)
        granted = (mode >> 3) & 7;
    else
        granted = mode & 7;
    return (granted & want) == want;
}

/* Free a queue slot and all its messages (caller holds mq_lock). */
static void mq_free_queue(struct mqueue *q)
{
    struct mq_msg *m = q->msgs;
    while (m) {
        struct mq_msg *n = m->next;
        kfree(m, sizeof(struct mq_msg) + m->len);
        m = n;
    }
    q->msgs = NULL;
    q->in_use = 0;
    q->unlinked = 0;
    q->refs = 0;
    q->curmsgs = 0;
    q->send_waiters = 0;
    q->recv_waiters = 0;
    q->name[0] = '\0';
    memset(&q->notify, 0, sizeof(q->notify));
    q->seq++;                    /* invalidate outstanding qseq references */
}

/* Drop a descriptor's reference; free the queue when the last close meets an
 * unlinked name (caller holds mq_lock). */
static void mq_queue_unref(struct mqueue *q)
{
    if (q->refs > 0)
        q->refs--;
    if (q->refs == 0 && q->unlinked)
        mq_free_queue(q);
}

/* Distinct wait channels per queue and direction, so a blocked sender and a
 * blocked receiver on the same queue don't wake each other spuriously. */
static void *mq_send_chan(struct mqueue *q) { return (char *)q + 1; }
static void *mq_recv_chan(struct mqueue *q) { return (char *)q + 2; }

static void mq_wake(void *chan)
{
    /* Wake via both mechanisms (see pipe.c): sleepq for our blockers, and
     * sched_wakeup so a select/poll multiplexer parked on the channel also
     * re-checks. */
    sleepq_wake_all(chan);
    sched_wakeup(chan);
}

/*
 * Block the current thread on `chan`, dropping mq_lock while asleep and
 * reacquiring it before return.  Returns 0 on a normal wake and -EINTR if a
 * signal is pending.  The caller re-evaluates its own condition and the
 * absolute deadline on return; `deadline` (in ticks, 0 = none) only bounds
 * the sleep so a real timeout self-heals.  Modeled on pipe_wait().
 */
static int mq_block(void *chan, uint64_t deadline)
{
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;
    uint32_t pf = intr_disable();
    sleepq_add(chan, current_thread);
    if (current_thread->sleep_expiry == 0) {
        uint32_t hz = get_hz();
        uint64_t span = hz ? (hz / 10u) : 8u;   /* ~100 ms lost-wakeup net */
        if (span == 0) span = 1;
        uint64_t exp = get_ticks() + span;
        if (deadline && deadline < exp)
            exp = deadline;
        current_thread->sleep_expiry = exp;
    }
    mutex_unlock(&mq_lock);
    intr_restore(pf);
    if (current_thread->wait_chan == chan)
        sched_yield();
    current_thread->sleep_expiry = 0;
    sleepq_remove_thread(current_thread);
    mutex_lock(&mq_lock);
    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    if (current_thread->sig_pending & ~current_thread->sig_mask)
        return -EINTR;
    return 0;
}

/* Convert an absolute CLOCK_REALTIME timespec to a monotonic tick deadline.
 * Returns 0 (no deadline) if abstime is NULL.  *expired is set if the
 * deadline is already in the past. */
static uint64_t mq_abs_to_deadline(const struct timespec *abstime, int *expired)
{
    *expired = 0;
    if (!abstime)
        return 0;

    struct timespec now;
    kern_clock_gettime(CLOCK_REALTIME, &now);

    int64_t dsec  = (int64_t)abstime->tv_sec - (int64_t)now.tv_sec;
    int64_t dnsec = (int64_t)abstime->tv_nsec - (int64_t)now.tv_nsec;
    int64_t total_ns = dsec * 1000000000LL + dnsec;
    if (total_ns <= 0) {
        *expired = 1;
        return get_ticks();       /* already due */
    }

    uint32_t hz = get_hz();
    uint64_t ticks = (uint64_t)((total_ns * (int64_t)hz + 999999999LL) / 1000000000LL);
    if (ticks == 0)
        ticks = 1;
    return get_ticks() + ticks;
}

/* ---- core: open / unlink / close ---- */

int kern_mq_open(const char *name, int oflag, mode_t mode,
                 const struct mq_attr *attr)
{
    mq_lazy_init();

    /* POSIX names are "/somename": a leading slash and no other slash. */
    if (!name || name[0] != '/')
        return -EINVAL;
    size_t nlen = strlen(name);
    if (nlen < 2 || nlen > MQ_NAME_MAX)
        return -EINVAL;
    for (size_t i = 1; i < nlen; i++)
        if (name[i] == '/')
            return -EINVAL;

    int accmode = oflag & O_ACCMODE;
    if (accmode != O_RDONLY && accmode != O_WRONLY && accmode != O_RDWR)
        return -EINVAL;

    mutex_lock(&mq_lock);

    struct mqueue *q = NULL;
    int free_q = -1;
    int created = 0;
    for (int i = 0; i < MQ_OPEN_MAX; i++) {
        if (mqueues[i].in_use && !mqueues[i].unlinked &&
            strcmp(mqueues[i].name, name) == 0) {
            q = &mqueues[i];
            break;
        }
        if (!mqueues[i].in_use && free_q < 0)
            free_q = i;
    }

    if (q) {
        /* Existing queue. */
        if ((oflag & O_CREAT) && (oflag & O_EXCL)) {
            mutex_unlock(&mq_lock);
            return -EEXIST;
        }
        int want = 0;
        if (accmode == O_RDONLY || accmode == O_RDWR) want |= 4;
        if (accmode == O_WRONLY || accmode == O_RDWR) want |= 2;
        if (!mq_perm_ok(q, want)) {
            mutex_unlock(&mq_lock);
            return -EACCES;
        }
    } else {
        if (!(oflag & O_CREAT)) {
            mutex_unlock(&mq_lock);
            return -ENOENT;
        }
        if (free_q < 0) {
            mutex_unlock(&mq_lock);
            return -ENOSPC;
        }
        /* Validate a caller-supplied attr. */
        long maxmsg  = MQ_DFL_MAXMSG;
        long msgsize = MQ_DFL_MSGSIZE;
        if (attr) {
            maxmsg  = attr->mq_maxmsg;
            msgsize = attr->mq_msgsize;
            if (maxmsg <= 0 || msgsize <= 0 ||
                maxmsg > MQ_MAXMSG_LIMIT || msgsize > MQ_MSGSIZE_LIMIT) {
                mutex_unlock(&mq_lock);
                return -EINVAL;
            }
        }
        q = &mqueues[free_q];
        process_t *p = current_process;
        /* mq_free_queue bumped seq on the previous free; preserve it across
         * the zero-init so stale-descriptor detection stays monotonic. */
        unsigned short saved_seq = q->seq;
        memset(q, 0, sizeof(*q));
        q->seq = saved_seq;
        q->in_use   = 1;
        q->unlinked = 0;
        strlcpy(q->name, name, sizeof(q->name));
        q->maxmsg   = maxmsg;
        q->msgsize  = msgsize;
        q->curmsgs  = 0;
        q->msgs     = NULL;
        q->refs     = 0;
        q->perm.__key = 0;
        q->perm.uid  = q->perm.cuid = p ? p->euid : 0;
        q->perm.gid  = q->perm.cgid = p ? p->egid : 0;
        q->perm.mode = mode & 0777;
        q->perm.__seq = q->seq;
        created = 1;
    }

    /* Allocate a descriptor. */
    int free_d = -1;
    for (int i = 0; i < MQ_DESC_MAX; i++)
        if (!mqdescs[i].in_use) { free_d = i; break; }
    if (free_d < 0) {
        /* No descriptor free.  Roll back a queue we created this call. */
        if (created)
            mq_free_queue(q);
        mutex_unlock(&mq_lock);
        return -EMFILE;
    }

    struct mqdesc *d = &mqdescs[free_d];
    unsigned short dseq = d->seq;
    memset(d, 0, sizeof(*d));
    d->seq    = dseq;
    d->in_use = 1;
    d->qindex = (int)(q - mqueues);
    d->qseq   = q->seq;
    d->oflag  = (accmode) | (oflag & O_NONBLOCK);
    d->pid    = current_process ? current_process->pid : 0;
    q->refs++;

    int mqd = mqd_make(free_d, d->seq);
    mutex_unlock(&mq_lock);
    return mqd;
}

int kern_mq_unlink(const char *name)
{
    mq_lazy_init();
    if (!name || name[0] != '/')
        return -EINVAL;

    mutex_lock(&mq_lock);
    struct mqueue *q = NULL;
    for (int i = 0; i < MQ_OPEN_MAX; i++) {
        if (mqueues[i].in_use && !mqueues[i].unlinked &&
            strcmp(mqueues[i].name, name) == 0) {
            q = &mqueues[i];
            break;
        }
    }
    if (!q) {
        mutex_unlock(&mq_lock);
        return -ENOENT;
    }
    if (!mq_perm_ok(q, 2)) {
        mutex_unlock(&mq_lock);
        return -EACCES;
    }

    q->unlinked = 1;
    q->name[0] = '\0';           /* free the name for reuse immediately */
    /* Wake any blocked senders/receivers so they report EIDRM (they re-check
     * q->unlinked on wake). */
    void *sc = mq_send_chan(q), *rc = mq_recv_chan(q);
    if (q->refs == 0)
        mq_free_queue(q);        /* no open descriptors: destroy now */
    mutex_unlock(&mq_lock);
    mq_wake(sc);
    mq_wake(rc);
    return 0;
}

int kern_mq_close(int mqd)
{
    mq_lazy_init();
    mutex_lock(&mq_lock);
    int err = 0;
    struct mqdesc *d = mqd_lookup(mqd, &err);
    if (!d) { mutex_unlock(&mq_lock); return -err; }
    struct mqueue *q = mq_from_desc(d);
    /* If this descriptor held the notify registration, clear it. */
    if (q && q->notify.active && q->notify.pid ==
        (current_process ? current_process->pid : -1)) {
        memset(&q->notify, 0, sizeof(q->notify));
    }
    d->in_use = 0;
    d->seq++;                    /* invalidate this mqd */
    if (q)
        mq_queue_unref(q);
    mutex_unlock(&mq_lock);
    return 0;
}

/* ---- core: send ---- */

int kern_mq_send(int mqd, const char *msg, size_t len, unsigned prio,
                 const struct timespec *abstime)
{
    mq_lazy_init();
    if (prio >= MQ_PRIO_MAX)
        return -EINVAL;

    int expired = 0;
    uint64_t deadline = mq_abs_to_deadline(abstime, &expired);

    mutex_lock(&mq_lock);
    int err = 0;
    struct mqdesc *d = mqd_lookup(mqd, &err);
    if (!d) { mutex_unlock(&mq_lock); return -err; }
    if ((d->oflag & O_ACCMODE) == O_RDONLY) {
        mutex_unlock(&mq_lock);
        return -EBADF;           /* not opened for writing */
    }
    struct mqueue *q = mq_from_desc(d);
    if (!q) { mutex_unlock(&mq_lock); return -EBADF; }
    if ((long)len > q->msgsize) {
        mutex_unlock(&mq_lock);
        return -EMSGSIZE;
    }

    for (;;) {
        q = mq_from_desc(d);
        if (!q || q->unlinked) {
            mutex_unlock(&mq_lock);
            return -EIDRM;
        }
        if (q->curmsgs < q->maxmsg)
            break;               /* room to enqueue */

        if (d->oflag & O_NONBLOCK) {
            mutex_unlock(&mq_lock);
            return -EAGAIN;
        }
        /* About to block: POSIX requires EINVAL for a malformed absolute
         * timeout (tv_nsec outside [0,1e9)).  Checked only on the blocking
         * path — a non-blocking completion never consults abs_timeout
         * (mq_timedsend/19-1, mq_timedreceive/17-1,17-2,17-3). */
        if (abstime && (abstime->tv_nsec < 0 ||
                        abstime->tv_nsec >= 1000000000L)) {
            mutex_unlock(&mq_lock);
            return -EINVAL;
        }
        if (deadline) {
            if (expired || get_ticks() >= deadline) {
                mutex_unlock(&mq_lock);
                return -ETIMEDOUT;
            }
        }
        q->send_waiters++;
        int r = mq_block(mq_send_chan(q), deadline);
        /* q may have been freed while we slept; re-resolve to decrement. */
        struct mqueue *q2 = mq_from_desc(d);
        if (q2 && q2->send_waiters > 0)
            q2->send_waiters--;
        if (r == -EINTR) {
            mutex_unlock(&mq_lock);
            return -EINTR;
        }
    }

    /* Enqueue the message. */
    struct mq_msg *node = (struct mq_msg *)kmalloc(sizeof(struct mq_msg) + len);
    if (!node) {
        mutex_unlock(&mq_lock);
        return -ENOMEM;
    }
    node->prio = prio;
    node->len  = len;
    if (len)
        memcpy(MQ_MSG_DATA(node), msg, len);

    /* Insert highest-priority-first, FIFO within a priority. */
    struct mq_msg **pp = &q->msgs;
    while (*pp && (*pp)->prio >= prio)
        pp = &(*pp)->next;
    node->next = *pp;
    *pp = node;

    int was_empty = (q->curmsgs == 0);
    q->curmsgs++;

    void *rc = mq_recv_chan(q);
    int   deliver_notify = 0;
    int   notify_pid = 0, notify_signo = 0;

    if (q->recv_waiters > 0) {
        /* A blocked receiver will take it — no notification. */
    } else if (was_empty && q->notify.active &&
               q->notify.notify == SIGEV_SIGNAL) {
        deliver_notify = 1;
        notify_pid = q->notify.pid;
        notify_signo = q->notify.signo;
        memset(&q->notify, 0, sizeof(q->notify));   /* one-shot */
    }

    mutex_unlock(&mq_lock);
    mq_wake(rc);

    if (deliver_notify) {
        process_t *tp = proc_find(notify_pid);
        if (tp)
            psignal(tp, notify_signo);
    }
    return 0;
}

/* ---- core: receive ---- */

ssize_t kern_mq_receive(int mqd, char *msg, size_t len, unsigned *prio,
                        const struct timespec *abstime)
{
    mq_lazy_init();

    int expired = 0;
    uint64_t deadline = mq_abs_to_deadline(abstime, &expired);

    mutex_lock(&mq_lock);
    int err = 0;
    struct mqdesc *d = mqd_lookup(mqd, &err);
    if (!d) { mutex_unlock(&mq_lock); return -err; }
    if ((d->oflag & O_ACCMODE) == O_WRONLY) {
        mutex_unlock(&mq_lock);
        return -EBADF;           /* not opened for reading */
    }
    struct mqueue *q = mq_from_desc(d);
    if (!q) { mutex_unlock(&mq_lock); return -EBADF; }
    /* POSIX: the receive buffer must be at least mq_msgsize. */
    if ((long)len < q->msgsize) {
        mutex_unlock(&mq_lock);
        return -EMSGSIZE;
    }

    for (;;) {
        q = mq_from_desc(d);
        if (!q || q->unlinked) {
            mutex_unlock(&mq_lock);
            return -EIDRM;
        }
        if (q->curmsgs > 0)
            break;               /* a message is available */

        if (d->oflag & O_NONBLOCK) {
            mutex_unlock(&mq_lock);
            return -EAGAIN;
        }
        /* About to block: POSIX requires EINVAL for a malformed absolute
         * timeout (tv_nsec outside [0,1e9)).  Checked only on the blocking
         * path — a non-blocking completion never consults abs_timeout
         * (mq_timedsend/19-1, mq_timedreceive/17-1,17-2,17-3). */
        if (abstime && (abstime->tv_nsec < 0 ||
                        abstime->tv_nsec >= 1000000000L)) {
            mutex_unlock(&mq_lock);
            return -EINVAL;
        }
        if (deadline) {
            if (expired || get_ticks() >= deadline) {
                mutex_unlock(&mq_lock);
                return -ETIMEDOUT;
            }
        }
        q->recv_waiters++;
        int r = mq_block(mq_recv_chan(q), deadline);
        struct mqueue *q2 = mq_from_desc(d);
        if (q2 && q2->recv_waiters > 0)
            q2->recv_waiters--;
        if (r == -EINTR) {
            mutex_unlock(&mq_lock);
            return -EINTR;
        }
    }

    /* Dequeue the head (highest priority, oldest). */
    struct mq_msg *node = q->msgs;
    q->msgs = node->next;
    q->curmsgs--;
    size_t mlen = node->len;
    unsigned mprio = node->prio;
    if (mlen)
        memcpy(msg, MQ_MSG_DATA(node), mlen);
    kfree(node, sizeof(struct mq_msg) + node->len);

    void *sc = mq_send_chan(q);
    mutex_unlock(&mq_lock);
    mq_wake(sc);                  /* a blocked sender may now proceed */

    if (prio)
        *prio = mprio;
    return (ssize_t)mlen;
}

/* ---- core: notify ---- */

int kern_mq_notify(int mqd, const struct sigevent *sev)
{
    mq_lazy_init();
    mutex_lock(&mq_lock);
    int err = 0;
    struct mqdesc *d = mqd_lookup(mqd, &err);
    if (!d) { mutex_unlock(&mq_lock); return -err; }
    struct mqueue *q = mq_from_desc(d);
    if (!q) { mutex_unlock(&mq_lock); return -EBADF; }

    int pid = current_process ? current_process->pid : 0;

    if (sev == NULL) {
        /* Deregister — only if we are the registered process. */
        if (q->notify.active && q->notify.pid == pid)
            memset(&q->notify, 0, sizeof(q->notify));
        mutex_unlock(&mq_lock);
        return 0;
    }

    if (q->notify.active && q->notify.pid != pid) {
        mutex_unlock(&mq_lock);
        return -EBUSY;           /* another process is registered */
    }

    if (sev->sigev_notify == SIGEV_NONE) {
        q->notify.active = 1;
        q->notify.pid    = pid;
        q->notify.notify = SIGEV_NONE;
        q->notify.signo  = 0;
        q->notify.value  = sev->sigev_value;
    } else if (sev->sigev_notify == SIGEV_SIGNAL) {
        if (sev->sigev_signo <= 0 || sev->sigev_signo > NSIG) {
            mutex_unlock(&mq_lock);
            return -EINVAL;
        }
        q->notify.active = 1;
        q->notify.pid    = pid;
        q->notify.notify = SIGEV_SIGNAL;
        q->notify.signo  = sev->sigev_signo;
        q->notify.value  = sev->sigev_value;
    } else {
        /* SIGEV_THREAD is realized in userspace (librt) and reaches the
         * kernel as SIGEV_SIGNAL, so an unexpected type is an error. */
        mutex_unlock(&mq_lock);
        return -EINVAL;
    }
    mutex_unlock(&mq_lock);
    return 0;
}

/* ---- core: getattr / setattr ---- */

int kern_mq_getattr(int mqd, struct mq_attr *attr)
{
    mq_lazy_init();
    mutex_lock(&mq_lock);
    int err = 0;
    struct mqdesc *d = mqd_lookup(mqd, &err);
    if (!d) { mutex_unlock(&mq_lock); return -err; }
    struct mqueue *q = mq_from_desc(d);
    if (!q) { mutex_unlock(&mq_lock); return -EBADF; }
    attr->mq_flags   = (d->oflag & O_NONBLOCK) ? O_NONBLOCK : 0;
    attr->mq_maxmsg  = q->maxmsg;
    attr->mq_msgsize = q->msgsize;
    attr->mq_curmsgs = q->curmsgs;
    mutex_unlock(&mq_lock);
    return 0;
}

int kern_mq_setattr(int mqd, const struct mq_attr *newattr,
                    struct mq_attr *oldattr)
{
    mq_lazy_init();
    mutex_lock(&mq_lock);
    int err = 0;
    struct mqdesc *d = mqd_lookup(mqd, &err);
    if (!d) { mutex_unlock(&mq_lock); return -err; }
    struct mqueue *q = mq_from_desc(d);
    if (!q) { mutex_unlock(&mq_lock); return -EBADF; }

    if (oldattr) {
        oldattr->mq_flags   = (d->oflag & O_NONBLOCK) ? O_NONBLOCK : 0;
        oldattr->mq_maxmsg  = q->maxmsg;
        oldattr->mq_msgsize = q->msgsize;
        oldattr->mq_curmsgs = q->curmsgs;
    }
    /* Only mq_flags (the O_NONBLOCK bit) is settable per POSIX. */
    if (newattr) {
        if (newattr->mq_flags & O_NONBLOCK)
            d->oflag |= O_NONBLOCK;
        else
            d->oflag &= ~O_NONBLOCK;
    }
    mutex_unlock(&mq_lock);
    return 0;
}

/* ---- process-exit cleanup ---- */

void mq_proc_cleanup(int pid)
{
    if (!mq_ready)
        return;
    mutex_lock(&mq_lock);
    /* Drop any notify registration owned by this pid. */
    for (int i = 0; i < MQ_OPEN_MAX; i++) {
        struct mqueue *q = &mqueues[i];
        if (q->in_use && q->notify.active && q->notify.pid == pid)
            memset(&q->notify, 0, sizeof(q->notify));
    }
    /* Close descriptors owned by this pid. */
    for (int i = 0; i < MQ_DESC_MAX; i++) {
        struct mqdesc *d = &mqdescs[i];
        if (d->in_use && d->pid == pid) {
            struct mqueue *q = mq_from_desc(d);
            d->in_use = 0;
            d->seq++;
            if (q)
                mq_queue_unref(q);
        }
    }
    mutex_unlock(&mq_lock);
}

/* ---- native-ABI syscall wrappers ---- */

int sys_mq_open(const char *uname, int oflag, mode_t mode,
                const struct mq_attr *uattr)
{
    char kname[MQ_NAME_MAX + 1];
    size_t got = 0;
    int nrc = copyinstr(uname, kname, sizeof(kname), &got);
    if (nrc != 0)
        return -nrc;   /* ENAMETOOLONG on an over-long name, EFAULT on a bad pointer */

    struct mq_attr kattr;
    const struct mq_attr *attrp = NULL;
    if ((oflag & O_CREAT) && uattr) {
        if (copyin(uattr, &kattr, sizeof(kattr)) != 0)
            return -EFAULT;
        attrp = &kattr;
    }
    return kern_mq_open(kname, oflag, mode, attrp);
}

int sys_mq_close(int mqd)
{
    return kern_mq_close(mqd);
}

int sys_mq_unlink(const char *uname)
{
    char kname[MQ_NAME_MAX + 1];
    size_t got = 0;
    int nrc = copyinstr(uname, kname, sizeof(kname), &got);
    if (nrc != 0)
        return -nrc;   /* ENAMETOOLONG on an over-long name, EFAULT on a bad pointer */
    return kern_mq_unlink(kname);
}

int sys_mq_timedsend(int mqd, const char *umsg, size_t len,
                     unsigned prio, const struct timespec *uts)
{
    struct timespec kts;
    const struct timespec *tsp = NULL;
    if (uts) {
        if (copyin(uts, &kts, sizeof(kts)) != 0)
            return -EFAULT;
        tsp = &kts;
    }
    if (len > MQ_MSGSIZE_LIMIT)
        return -EMSGSIZE;

    char *kbuf = NULL;
    if (len) {
        kbuf = (char *)kmalloc(len);
        if (!kbuf)
            return -ENOMEM;
        if (copyin(umsg, kbuf, len) != 0) {
            kfree(kbuf, len);
            return -EFAULT;
        }
    }
    int r = kern_mq_send(mqd, kbuf, len, prio, tsp);
    if (kbuf)
        kfree(kbuf, len);
    return r;
}

ssize_t sys_mq_timedreceive(int mqd, char *umsg, size_t len,
                            unsigned *uprio, const struct timespec *uts)
{
    struct timespec kts;
    const struct timespec *tsp = NULL;
    if (uts) {
        if (copyin(uts, &kts, sizeof(kts)) != 0)
            return -EFAULT;
        tsp = &kts;
    }
    if (len > (size_t)MQ_MSGSIZE_LIMIT + 1)
        len = (size_t)MQ_MSGSIZE_LIMIT + 1;   /* bound the kernel buffer */

    char *kbuf = NULL;
    if (len) {
        kbuf = (char *)kmalloc(len);
        if (!kbuf)
            return -ENOMEM;
    }
    unsigned kprio = 0;
    ssize_t r = kern_mq_receive(mqd, kbuf, len, &kprio, tsp);
    if (r >= 0) {
        if (r > 0 && copyout(kbuf, umsg, (size_t)r) != 0)
            r = -EFAULT;
        else if (uprio && copyout(&kprio, uprio, sizeof(kprio)) != 0)
            r = -EFAULT;
    }
    if (kbuf)
        kfree(kbuf, len);
    return r;
}

int sys_mq_notify(int mqd, const struct sigevent *usev)
{
    struct sigevent ksev;
    const struct sigevent *sevp = NULL;
    if (usev) {
        if (copyin(usev, &ksev, sizeof(ksev)) != 0)
            return -EFAULT;
        sevp = &ksev;
    }
    return kern_mq_notify(mqd, sevp);
}

int sys_mq_getsetattr(int mqd, const struct mq_attr *unew,
                      struct mq_attr *uold)
{
    struct mq_attr knew, kold;
    const struct mq_attr *newp = NULL;
    if (unew) {
        if (copyin(unew, &knew, sizeof(knew)) != 0)
            return -EFAULT;
        newp = &knew;
    }
    int r = kern_mq_setattr(mqd, newp, uold ? &kold : NULL);
    if (r == 0 && uold) {
        if (copyout(&kold, uold, sizeof(kold)) != 0)
            return -EFAULT;
    }
    return r;
}
