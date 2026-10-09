/*
 * kqueue.c - kernel event queues: kqueue(2) and kevent(2).
 *
 * A queue is a descriptor holding a set of registrations ("knotes"), each
 * an (ident, filter) pair a program wants to hear about; kevent() changes
 * the set and collects the registrations that have something to report.
 * It is the BSDs' answer to poll(2), and the programs of those
 * personalities use it where a Linux program would use epoll or a futex:
 * Wine's server for its descriptors, and Wine itself to park and wake its
 * threads (EVFILT_USER).  See kqueue(9).
 *
 * Four filters:
 *
 *   EVFILT_READ, EVFILT_WRITE  ident is a descriptor; ready as poll(2)
 *                              would say it is.
 *   EVFILT_USER                ident is the program's; fires when the
 *                              program says so (NOTE_TRIGGER).
 *   EVFILT_TIMER               ident is the program's; fires every
 *                              `data` milliseconds (or NOTE_* units).
 *
 * Readiness of descriptors comes from the same per-file poll hooks
 * poll(2) uses, through kern_poll(): a scan asks about every registered
 * descriptor without waiting, and if there is nothing to report the
 * caller waits in kern_poll() on those descriptors and on the queue's own,
 * which is readable when a user event has fired -- so a trigger from
 * another thread wakes the waiter the way data on a pipe would.
 *
 * Not as the BSDs have it, and why:
 *
 *   - EV_CLEAR on a descriptor reports once per change from not ready to
 *     ready.  BSD reports again when more arrives while it is still
 *     ready; nothing here sees that happen.  While such a registration
 *     is ready and reported, the wait is cut into KQ_CLEAR_SLICE_MS
 *     pieces to notice it stop being so.
 *   - `data` of a read or write event is 1 when the descriptor is ready
 *     and 0 when it has only hung up, not the byte count.
 *   - A queue belongs to the process that made it: a forked child's copy
 *     of the descriptor gives EBADF, where BSD does not copy it at all.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <kern/file.h>
#include <kern/sched.h>
#include <kern/sleepq.h>
#include <kern/time.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/kqueue.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <sys/poll.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

#define KQ_MAX_NOTES        4096
#define KQ_CLEAR_SLICE_MS   20
#define KQ_STICKY           (KQ_EV_ONESHOT | KQ_EV_CLEAR | KQ_EV_DISPATCH)

struct knote {
    struct knote *next;
    uintptr_t ident;
    int16_t   filter;
    uint16_t  flags;        /* KQ_STICKY bits */
    uint8_t   disabled;
    uint8_t   fired;        /* USER: triggered and not yet collected */
    uint8_t   reported;     /* READ/WRITE + EV_CLEAR: told, still ready */
    uint32_t  sfflags;      /* USER: the flags it carries */
    int64_t   sdata;
    uintptr_t udata;
    file_t   *file;         /* READ/WRITE: what ident named when added */
    uint64_t  period;       /* TIMER, in ticks */
    uint64_t  due;
};

struct kqueue {
    mutex_t lock;
    struct knote *notes;
    int nnotes;
    int refs;               /* the descriptor, and each kevent() in progress */
    int closed;
    int owner;              /* pid */
};

static uint64_t kq_ms_to_ticks(uint64_t ms) {
    uint64_t t = (ms * (uint64_t)HZ + 999ULL) / 1000ULL;

    return t ? t : 1;
}

static struct kqueue *kq_from_fd(int fd) {
    file_t *f;
    fs_node_t *node;

    if (!current_process || fd < 0 || fd >= MAX_FD) return NULL;
    f = current_process->fds[fd];
    if (!f || f->f_type != DTYPE_KQUEUE || !f->f_data) return NULL;
    node = (fs_node_t *)f->f_data;
    return (struct kqueue *)(uintptr_t)node->impl;
}

static void kq_wake(struct kqueue *kq) {
    sleepq_wake_all(kq);
    sched_wakeup(kq);
}

static void kq_release(struct kqueue *kq) {
    int last;

    mutex_lock(&kq->lock);
    last = --kq->refs == 0;
    mutex_unlock(&kq->lock);
    if (last) kfree(kq, sizeof(*kq));
}

static struct knote **kq_find(struct kqueue *kq, uintptr_t ident,
                              int filter) {
    struct knote **pp;

    for (pp = &kq->notes; *pp; pp = &(*pp)->next) {
        if ((*pp)->ident == ident && (*pp)->filter == filter) return pp;
    }
    return NULL;
}

static void kq_unlink(struct kqueue *kq, struct knote **pp) {
    struct knote *kn = *pp;

    *pp = kn->next;
    kq->nnotes--;
    kfree(kn, sizeof(*kn));
}

/* A registration has been reported: what its flags say happens to it.
 * Returns 1 if it is gone. */
static int kq_reported(struct kqueue *kq, struct knote **pp) {
    if ((*pp)->flags & KQ_EV_ONESHOT) {
        kq_unlink(kq, pp);
        return 1;
    }
    if ((*pp)->flags & KQ_EV_DISPATCH) (*pp)->disabled = 1;
    return 0;
}

static int kq_pending(struct kqueue *kq) {
    struct knote *kn;

    for (kn = kq->notes; kn; kn = kn->next) {
        if (!kn->disabled && kn->filter == KQ_EVFILT_USER && kn->fired)
            return 1;
    }
    return 0;
}

/* The queue's own descriptor is readable when a user event has fired;
 * that is what a thread waiting in kern_kevent() is woken by. */
static int kqueue_poll(fs_node_t *node, void *waiter) {
    struct kqueue *kq = (struct kqueue *)(uintptr_t)node->impl;
    int events;

    if (!kq) return POLLNVAL;
    mutex_lock(&kq->lock);
    events = kq_pending(kq) ? (POLLIN | POLLRDNORM) : 0;
    mutex_unlock(&kq->lock);
    if (events == 0 && waiter) *(void **)waiter = kq;
    return events;
}

static void kqueue_close(fs_node_t *node) {
    struct kqueue *kq = (struct kqueue *)(uintptr_t)node->impl;

    node->impl = 0;
    kfree(node, sizeof(*node));
    if (!kq) return;
    mutex_lock(&kq->lock);
    kq->closed = 1;
    while (kq->notes) kq_unlink(kq, &kq->notes);
    mutex_unlock(&kq->lock);
    kq_wake(kq);
    kq_release(kq);
}

int kern_kqueue(void) {
    struct kqueue *kq;
    fs_node_t *node;
    file_t *f;
    int fd;

    if (!current_process) return -EINVAL;
    kq = kmalloc(sizeof(*kq));
    node = kmalloc(sizeof(*node));
    if (!kq || !node) {
        if (kq) kfree(kq, sizeof(*kq));
        if (node) kfree(node, sizeof(*node));
        return -ENOMEM;
    }
    memset(kq, 0, sizeof(*kq));
    mutex_init(&kq->lock, "kqueue");
    kq->refs = 1;
    kq->owner = current_process->pid;

    memset(node, 0, sizeof(*node));
    strlcpy(node->name, "kqueue", sizeof(node->name));
    node->flags = FS_CHARDEVICE;
    node->poll = &kqueue_poll;
    node->close = &kqueue_close;
    node->impl = (uintptr_t)kq;

    fd = proc_alloc_fd(current_process);
    f = fd >= 0 ? file_alloc() : NULL;
    if (!f) {
        if (fd >= 0) proc_clear_fd(current_process, fd);
        kfree(node, sizeof(*node));
        kfree(kq, sizeof(*kq));
        return fd >= 0 ? -ENOMEM : -EMFILE;
    }
    f->f_data = node;
    f->f_flag = FREAD | FWRITE;
    f->f_type = DTYPE_KQUEUE;
    proc_set_fd(current_process, fd, f);
    return fd;
}

/* The period of a timer, in ticks, from `data` in the unit fflags names. */
static int kq_timer_period(const struct kq_event *ch, uint64_t *ticks) {
    uint64_t ms;

    if (ch->data < 0 || (ch->fflags & KQ_NOTE_ABSTIME)) return EINVAL;
    switch (ch->fflags & (KQ_NOTE_SECONDS | KQ_NOTE_MSECONDS |
                          KQ_NOTE_USECONDS | KQ_NOTE_NSECONDS)) {
    case 0:
    case KQ_NOTE_MSECONDS:  ms = (uint64_t)ch->data; break;
    case KQ_NOTE_SECONDS:   ms = (uint64_t)ch->data * 1000ULL; break;
    case KQ_NOTE_USECONDS:  ms = ((uint64_t)ch->data + 999ULL) / 1000ULL; break;
    case KQ_NOTE_NSECONDS:
        ms = ((uint64_t)ch->data + 999999ULL) / 1000000ULL;
        break;
    default:
        return EINVAL;
    }
    *ticks = kq_ms_to_ticks(ms);
    return 0;
}

/* One change.  Returns 0 or an errno; *wake is set if a waiter may now
 * have something to collect. */
static int kq_apply(struct kqueue *kq, const struct kq_event *ch, int *wake) {
    int is_fd = ch->filter == KQ_EVFILT_READ || ch->filter == KQ_EVFILT_WRITE;
    struct knote **pp, *kn;
    uint64_t period = 0;
    int created = 0, err;

    if (!is_fd && ch->filter != KQ_EVFILT_USER &&
        ch->filter != KQ_EVFILT_TIMER) {
        return EINVAL;
    }
    pp = kq_find(kq, ch->ident, ch->filter);

    if (ch->flags & KQ_EV_ADD) {
        if (is_fd && (ch->ident >= (uintptr_t)MAX_FD ||
                      !current_process->fds[ch->ident])) {
            return EBADF;
        }
        if (ch->filter == KQ_EVFILT_TIMER &&
            (err = kq_timer_period(ch, &period)) != 0) {
            return err;
        }
        if (!pp) {
            if (kq->nnotes >= KQ_MAX_NOTES) return ENOMEM;
            kn = kmalloc(sizeof(*kn));
            if (!kn) return ENOMEM;
            memset(kn, 0, sizeof(*kn));
            kn->ident = ch->ident;
            kn->filter = ch->filter;
            /* What becomes of it once reported is settled here and not
             * by a later EV_ADD of the same registration. */
            kn->flags = ch->flags & KQ_STICKY;
            kn->next = kq->notes;
            kq->notes = kn;
            kq->nnotes++;
            pp = &kq->notes;
            created = 1;
            if (ch->filter == KQ_EVFILT_USER) {
                kn->sfflags = ch->fflags & KQ_NOTE_FFLAGSMASK;
                kn->sdata = ch->data;
                kn->fired = (ch->fflags & KQ_NOTE_TRIGGER) != 0;
            }
        }
        kn = *pp;
        if (is_fd) {
            kn->file = current_process->fds[ch->ident];
            kn->reported = 0;
        }
        if (ch->filter == KQ_EVFILT_TIMER) {
            kn->period = period;
            kn->due = get_ticks() + period;
        }
    } else if (!pp) {
        return ENOENT;
    }
    kn = *pp;

    if (ch->flags & KQ_EV_DELETE) {
        kq_unlink(kq, pp);
        return 0;
    }
    /* Any change carries the udata to hand back, an add or not. */
    if (!(ch->flags & KQ_EV_KEEPUDATA)) kn->udata = ch->udata;
    if (ch->flags & KQ_EV_ENABLE) kn->disabled = 0;
    if (ch->flags & KQ_EV_DISABLE) kn->disabled = 1;

    if (ch->filter == KQ_EVFILT_USER && !created) {
        uint32_t f = ch->fflags & KQ_NOTE_FFLAGSMASK;

        switch (ch->fflags & KQ_NOTE_FFCTRLMASK) {
        case KQ_NOTE_FFAND:  kn->sfflags &= f; break;
        case KQ_NOTE_FFOR:   kn->sfflags |= f; break;
        case KQ_NOTE_FFCOPY: kn->sfflags = f; break;
        default: break;
        }
        kn->sdata = ch->data;
        if (ch->flags & KQ_EV_CLEAR) {
            kn->fired = 0;
            kn->sfflags = 0;
            kn->sdata = 0;
        }
        if (ch->fflags & KQ_NOTE_TRIGGER) kn->fired = 1;
    }
    if (kn->fired && !kn->disabled) *wake = 1;
    return 0;
}

static void kq_emit(struct kq_event *ev, const struct knote *kn,
                    uint16_t flags, uint32_t fflags, int64_t data) {
    ev->ident = kn->ident;
    ev->filter = kn->filter;
    ev->flags = (uint16_t)(kn->flags | flags);
    ev->fflags = fflags;
    ev->data = data;
    ev->udata = kn->udata;
}

/*
 * Collect what there is to report, without waiting.  `pfds` has room for
 * the queue's descriptor and one entry per registration; on return with
 * nothing collected it holds the set to wait on, *npfds entries of it,
 * and *wait_ms is how long at most (-1: no limit of the queue's own).
 */
static int kq_scan(struct kqueue *kq, int kqfd, struct kq_event *events,
                   int nevents, struct pollfd *pfds, int cap, int *npfds,
                   int *wait_ms) {
    struct knote **pp, *kn;
    uint64_t now = get_ticks();
    int count = 0, n = 1, i;

    *wait_ms = -1;
    pfds[0].fd = kqfd;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    mutex_lock(&kq->lock);
    if (kq->closed) {
        mutex_unlock(&kq->lock);
        return -EBADF;
    }
    for (pp = &kq->notes; (kn = *pp) != NULL; ) {
        if (kn->filter == KQ_EVFILT_READ || kn->filter == KQ_EVFILT_WRITE) {
            /* A descriptor closed since, or closed and the number taken
             * by another file, takes its registration with it. */
            if (kn->ident >= (uintptr_t)MAX_FD ||
                current_process->fds[kn->ident] != kn->file) {
                kq_unlink(kq, pp);
                continue;
            }
            if (!kn->disabled && n < cap) {
                pfds[n].fd = (int)kn->ident;
                pfds[n].events = kn->filter == KQ_EVFILT_READ
                    ? POLLIN : POLLOUT;
                pfds[n].revents = 0;
                n++;
            }
        } else if (!kn->disabled && count < nevents) {
            if (kn->filter == KQ_EVFILT_USER && kn->fired) {
                kq_emit(&events[count++], kn, 0, kn->sfflags, kn->sdata);
                if (kn->flags & KQ_EV_CLEAR) {
                    kn->fired = 0;
                    kn->sfflags = 0;
                    kn->sdata = 0;
                }
                if (kq_reported(kq, pp)) continue;
            } else if (kn->filter == KQ_EVFILT_TIMER) {
                if (now >= kn->due) {
                    uint64_t times = 1 + (now - kn->due) / kn->period;

                    kq_emit(&events[count++], kn, 0, 0, (int64_t)times);
                    kn->due += times * kn->period;
                    if (kq_reported(kq, pp)) continue;
                } else {
                    uint64_t ms = (kn->due - now) * 1000ULL / (uint64_t)HZ;

                    if (ms == 0) ms = 1;
                    if (ms > 0x7fffffffULL) ms = 0x7fffffffULL;
                    if (*wait_ms < 0 || (int)ms < *wait_ms)
                        *wait_ms = (int)ms;
                }
            }
        }
        pp = &kn->next;
    }
    mutex_unlock(&kq->lock);

    if (n > 1) {
        int rc = kern_poll(pfds + 1, (unsigned int)(n - 1), 0);

        if (rc < 0) return count > 0 ? count : rc;
    }

    mutex_lock(&kq->lock);
    for (i = 1; i < n; i++) {
        int filter = pfds[i].events == POLLIN
            ? KQ_EVFILT_READ : KQ_EVFILT_WRITE;
        short rev = pfds[i].revents;
        short ready = rev & (pfds[i].events | POLLHUP | POLLERR);

        pp = kq_find(kq, (uintptr_t)pfds[i].fd, filter);
        if (!pp || (*pp)->disabled) {
            pfds[i].fd = -1;
            continue;
        }
        kn = *pp;
        if (rev & POLLNVAL) {
            kq_unlink(kq, pp);
            pfds[i].fd = -1;
            continue;
        }
        if (!ready) {
            kn->reported = 0;
            continue;
        }
        if (kn->reported) {
            /* Told already and still ready: not something to wait for. */
            pfds[i].fd = -1;
            if (*wait_ms < 0 || *wait_ms > KQ_CLEAR_SLICE_MS)
                *wait_ms = KQ_CLEAR_SLICE_MS;
            continue;
        }
        if (count < nevents) {
            kq_emit(&events[count++], kn,
                    (rev & (POLLHUP | POLLERR)) ? KQ_EV_EOF : 0, 0,
                    (rev & pfds[i].events) ? 1 : 0);
            if (kn->flags & KQ_EV_CLEAR) kn->reported = 1;
            (void)kq_reported(kq, pp);
        }
    }
    mutex_unlock(&kq->lock);

    *npfds = n;
    return count;
}

int kern_kevent(int kqfd, const struct kq_event *changes, int nchanges,
                struct kq_event *events, int nevents, int timeout_ms) {
    struct kqueue *kq = kq_from_fd(kqfd);
    struct pollfd *pfds = NULL;
    size_t pfds_size = 0;
    uint64_t deadline = 0;
    int count = 0, wake = 0, ret, i;

    if (!kq) return -EBADF;
    if (nchanges < 0 || nevents < 0) return -EINVAL;

    mutex_lock(&kq->lock);
    if (kq->closed || kq->owner != current_process->pid) {
        mutex_unlock(&kq->lock);
        return -EBADF;
    }
    kq->refs++;

    /* A change that fails, or that asks for a receipt, is answered in the
     * event list if there is room; a failure with no room ends the call. */
    for (i = 0; i < nchanges; i++) {
        int err = kq_apply(kq, &changes[i], &wake);

        if (err == 0 && !(changes[i].flags & KQ_EV_RECEIPT)) continue;
        if (count < nevents) {
            events[count] = changes[i];
            events[count].flags = KQ_EV_ERROR;
            events[count].data = err;
            count++;
        } else if (err) {
            mutex_unlock(&kq->lock);
            if (wake) kq_wake(kq);
            kq_release(kq);
            return -err;
        }
    }
    mutex_unlock(&kq->lock);
    if (wake) kq_wake(kq);

    if (count > 0 || nevents == 0) {
        kq_release(kq);
        return count;
    }
    if (timeout_ms > 0)
        deadline = get_ticks() + kq_ms_to_ticks((uint64_t)timeout_ms);

    for (;;) {
        int npfds = 0, wait_ms = -1, cap;

        mutex_lock(&kq->lock);
        cap = kq->nnotes + 1;
        mutex_unlock(&kq->lock);
        if ((size_t)cap * sizeof(*pfds) > pfds_size) {
            if (pfds) kfree(pfds, pfds_size);
            pfds_size = (size_t)(cap + 8) * sizeof(*pfds);
            pfds = kmalloc(pfds_size);
            if (!pfds) {
                ret = -ENOMEM;
                break;
            }
        }

        ret = kq_scan(kq, kqfd, events, nevents, pfds,
                      (int)(pfds_size / sizeof(*pfds)), &npfds, &wait_ms);
        if (ret != 0 || timeout_ms == 0) break;

        if (timeout_ms > 0) {
            uint64_t now = get_ticks();
            uint64_t left;

            if (now >= deadline) break;
            left = (deadline - now) * 1000ULL / (uint64_t)HZ;
            if (left == 0) left = 1;
            if (wait_ms < 0 || (uint64_t)wait_ms > left) wait_ms = (int)left;
        }
        ret = kern_poll(pfds, (unsigned int)npfds, wait_ms);
        if (ret < 0) break;
        ret = 0;
    }

    if (pfds) kfree(pfds, pfds_size);
    kq_release(kq);
    return ret;
}
