#ifndef _SYS_KQUEUE_H_
#define _SYS_KQUEUE_H_

#include <stdint.h>

/*
 * Kernel event queues: kqueue(2) and kevent(2) as the BSDs have them, for
 * the personalities whose programs call them (kern/kqueue.c, kqueue(9)).
 * The numbers are FreeBSD's <sys/event.h>, which NetBSD and OpenBSD share
 * for everything here.
 */

/* Filters. */
#define KQ_EVFILT_READ      (-1)
#define KQ_EVFILT_WRITE     (-2)
#define KQ_EVFILT_TIMER     (-7)
#define KQ_EVFILT_USER      (-11)

/* Actions and flags. */
#define KQ_EV_ADD           0x0001
#define KQ_EV_DELETE        0x0002
#define KQ_EV_ENABLE        0x0004
#define KQ_EV_DISABLE       0x0008
#define KQ_EV_ONESHOT       0x0010
#define KQ_EV_CLEAR         0x0020
#define KQ_EV_RECEIPT       0x0040
#define KQ_EV_DISPATCH      0x0080
#define KQ_EV_KEEPUDATA     0x0200
#define KQ_EV_ERROR         0x4000
#define KQ_EV_EOF           0x8000

/* EVFILT_USER: what to do with the low 24 bits of fflags, and the trigger. */
#define KQ_NOTE_FFNOP       0x00000000U
#define KQ_NOTE_FFAND       0x40000000U
#define KQ_NOTE_FFOR        0x80000000U
#define KQ_NOTE_FFCOPY      0xc0000000U
#define KQ_NOTE_FFCTRLMASK  0xc0000000U
#define KQ_NOTE_FFLAGSMASK  0x00ffffffU
#define KQ_NOTE_TRIGGER     0x01000000U

/* EVFILT_TIMER: the unit of data; milliseconds if none is given. */
#define KQ_NOTE_SECONDS     0x00000001U
#define KQ_NOTE_MSECONDS    0x00000002U
#define KQ_NOTE_USECONDS    0x00000004U
#define KQ_NOTE_NSECONDS    0x00000008U
#define KQ_NOTE_ABSTIME     0x00000010U

/* An event as the kernel holds it; each personality has its own layout
 * of the same fields. */
struct kq_event {
    uintptr_t ident;
    int16_t   filter;
    uint16_t  flags;
    uint32_t  fflags;
    int64_t   data;
    uintptr_t udata;
};

/* A new queue: its descriptor, or -errno. */
int kern_kqueue(void);

/*
 * Apply `nchanges` changes to the queue `kq`, then collect up to
 * `nevents` events, waiting `timeout_ms` for the first (-1: until there
 * is one; 0: not at all).  Returns the number of events, or -errno.
 */
int kern_kevent(int kq, const struct kq_event *changes, int nchanges,
                struct kq_event *events, int nevents, int timeout_ms);

#endif /* _SYS_KQUEUE_H_ */
