#include <sys/input.h>
#include <sys/file.h>
#include <vm/vm_kmem.h>
#include <kern/sched.h>
#include <string.h>
#include <kern/console.h>
#include <vfs/vfs.h>
#include <sys/time.h>
#include <kern/time.h>
#include <sys/lock.h>
#include <sys/errno.h>
#include <sys/poll.h>

#define INPUT_QUEUE_SIZE 64

// An open handle to an input device (or the multiplexer)
typedef struct input_handle {
    input_event_t queue[INPUT_QUEUE_SIZE];
    int head;
    int tail;
    int dropped;
    struct input_handle *next;
} input_handle_t;

// Global list of devices
static input_dev_t *input_devices = NULL;

static input_event_t global_event_log[INPUT_QUEUE_SIZE];
static uint64_t global_seq = 0; // Total events written
/* Serializes the (global_event_log, global_seq) pair so a fast device
 * (especially a virtio one in a SMP guest) can't race the reader: with
 * no lock the reader can see a stale global_seq, miss events at the
 * tail, or read a half-written entry while a producer is mid-write. */
static spinlock_t input_lock = { 0 };
static int input_lock_init = 0;

static inline void input_lock_take(void) {
    if (!input_lock_init) {
        spinlock_init(&input_lock, "input");
        input_lock_init = 1;
    }
    spinlock_acquire(&input_lock);
}

static fs_node_t event_node;

void input_register_devfs(void);

void input_init(void) {
    input_devices = NULL;
    kprint("Input Subsystem Initialized\n");
    input_register_devfs();
}

int input_register_device(input_dev_t *dev) {
    if (!dev) return -1;
    dev->next = input_devices;
    input_devices = dev;
    kprint("Input: Registered device: ");
    kprint(dev->name);
    kprint("\n");
    return 0;
}

void input_unregister_device(input_dev_t *dev) {
    if (!dev) return;

    if (input_devices == dev) {
        input_devices = dev->next;
        dev->next = NULL;
        kprint("Input: Unregistered device: ");
        kprint(dev->name);
        kprint("\n");
        return;
    }

    input_dev_t *curr = input_devices;
    while (curr) {
        if (curr->next == dev) {
            curr->next = dev->next;
            dev->next = NULL;
            kprint("Input: Unregistered device: ");
            kprint(dev->name);
            kprint("\n");
            return;
        }
        curr = curr->next;
    }
}

void input_notify_readers(void) {
    sched_wakeup(&global_event_log);
}

// Distribute event to all handles
void input_report_event(input_dev_t *dev, uint16_t type, uint16_t code, int32_t value) {
    (void)dev; // In future, use this to filter

    struct timeval tv;
    sys_gettimeofday(&tv, NULL);

    input_lock_take();
    uint64_t idx = global_seq % INPUT_QUEUE_SIZE;
    global_event_log[idx].time_sec = tv.tv_sec;
    global_event_log[idx].time_usec = tv.tv_usec;
    global_event_log[idx].type = type;
    global_event_log[idx].code = code;
    global_event_log[idx].value = value;
    global_seq++;
    spinlock_release(&input_lock);

    input_notify_readers();
}

void input_sync(input_dev_t *dev) {
    input_report_event(dev, EV_SYN, 0, 0);
}

// Legacy compatibility wrapper
static input_dev_t legacy_dev = { .name = "Legacy Input" };
void input_enqueue(uint16_t type, uint16_t code, int32_t value) {
    input_report_event(&legacy_dev, type, code, value);
}

/*
 * /dev/input/event0 ioctl — minimal Linux evdev compat surface so that
 * xorg-server's kdrive evdev backend can complete its device-enable
 * path.  evdev calls:
 *   EVIOCGRAB(int)         — claim exclusive ownership; we accept the
 *                            call and return 0 (we don't enforce
 *                            exclusivity, but kdrive only cares that
 *                            the call doesn't fatal).
 *   EVIOCGBIT(0,  buf)     — supported event-type bitmap.  We return
 *                            zeroed buf: kdrive interprets that as
 *                            "no special event types declared", skips
 *                            the EV_KEY/EV_REL/EV_ABS probing, and
 *                            registers the fd for raw event reads.
 *                            Events still flow through input_read.
 *   EVIOCGBIT(EV_*, buf)   — handled the same way (caller never asks
 *                            for these once the type bitmap returned
 *                            empty, but we're conservative).
 *   EVIOCGNAME(buf)        — driver name.
 *   EVIOCGID  (struct)     — bus/vendor/product/version triple.
 *   EVIOCGVERSION(int)     — evdev protocol version.
 *
 * Linux ioctl encoding: bits 30-31 dir, 16-29 size, 8-15 type, 0-7 nr.
 * 'E' (0x45) is the evdev type byte.
 */
#define _SIOC_DIR(req)   (((req) >> 30) & 0x3)
#define _SIOC_SIZE(req)  (((req) >> 16) & 0x3fff)
#define _SIOC_TYPE(req)  (((req) >>  8) & 0xff)
#define _SIOC_NR(req)    (((req) >>  0) & 0xff)
#define EVIOC_TYPE       0x45    /* 'E' */
#define EVIOC_NR_GVERSION  0x01
#define EVIOC_NR_GID       0x02
#define EVIOC_NR_GNAME_BASE 0x06
#define EVIOC_NR_GBIT_BASE 0x20
#define EVIOC_NR_GRAB      0x90

extern int copyout(const void *src, void *dst, unsigned int size);

static int input_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    if (_SIOC_TYPE(request) != EVIOC_TYPE) return -ENOTTY;
    uint32_t nr = _SIOC_NR(request);
    uint32_t sz = _SIOC_SIZE(request);

    /* EVIOCGBIT(ev, sz) — nr is 0x20+ev, sz is the user buffer size.
     *
     * Earlier this returned an all-zero bitmap which let kdrive's
     * device-enable path past the call, but EvdevPtrMotion's inner
     * loop iterates 0..max_rel and ISBITSET(relbits, axis) — both
     * computed from these very bitmaps — so an empty bitmap meant
     * read events were silently dropped and the cursor never moved.
     *
     * Advertise:
     *   EVIOCGBIT(0, *)      — supported event TYPES.  Set bits for
     *                          EV_SYN(0), EV_KEY(1), EV_REL(2), EV_ABS(3).
     *   EVIOCGBIT(EV_REL, *) — supported relative axes.  Set REL_X(0),
     *                          REL_Y(1), REL_WHEEL(8).
     *   EVIOCGBIT(EV_ABS, *) — supported absolute axes.  None (substrate
     *                          input is rel-only); return zeroes.
     *   EVIOCGBIT(EV_KEY, *) — supported key codes.  Set the BTN_* mouse
     *                          range (0x110..0x117) and the full keyboard
     *                          range (1..255).  Doing this conservatively
     *                          is fine for kdrive: ISBITSET only checks
     *                          per-axis presence; spurious "I have this"
     *                          bits never produce events of their own.
     */
    if (nr >= EVIOC_NR_GBIT_BASE && nr < EVIOC_NR_GBIT_BASE + 0x20) {
        if (!arg || sz == 0 || sz > 4096) return -EINVAL;
        uint8_t buf[256];
        memset(buf, 0, sizeof(buf));
        uint32_t ev_kind = nr - EVIOC_NR_GBIT_BASE;
        if (ev_kind == 0) {
            /* event-type bitmap: bits 0..3 = SYN/KEY/REL/ABS */
            if (sz >= 1) buf[0] = 0x0Fu;
        } else if (ev_kind == 2) {
            /* EV_REL: REL_X(0), REL_Y(1), REL_WHEEL(8) */
            if (sz >= 1) buf[0] |= 0x03u;
            if (sz >= 2) buf[1] |= 0x01u;
        } else if (ev_kind == 1) {
            /* EV_KEY: mark 1..0x2FF range present.  Linux KEY_MAX = 0x2FF
             * so we mark 96 bytes (768 bits).  Cap by sz. */
            uint32_t fill = sz > 96u ? 96u : sz;
            memset(buf, 0xFFu, fill);
            buf[0] &= ~0x01u;   /* bit 0 (key 0 = reserved) is never set */
        }
        /* ev_kind == 3 (EV_ABS) and others: stay zero. */
        uint32_t remaining = sz;
        uint8_t *dst = (uint8_t *)arg;
        const uint8_t *src = buf;
        uint8_t pad[256];
        memset(pad, 0, sizeof(pad));
        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            if (src == buf) {
                if (copyout(buf, dst, chunk) != 0) return -EFAULT;
                /* Subsequent chunks (only happens if sz > 256) are
                 * just zeros. */
                src = pad;
            } else {
                if (copyout(pad, dst, chunk) != 0) return -EFAULT;
            }
            dst += chunk;
            remaining -= chunk;
        }
        return 0;
    }

    /* EVIOCGRAB(int) — accept the grab; we have no exclusive-owner
     * machinery so this is effectively advisory. */
    if (nr == EVIOC_NR_GRAB) {
        return 0;
    }

    /* EVIOCGVERSION(int) — report 0x010001 (matches Linux evdev v1). */
    if (nr == EVIOC_NR_GVERSION) {
        if (!arg) return -EINVAL;
        int ver = 0x010001;
        if (copyout(&ver, arg, sizeof(ver)) != 0) return -EFAULT;
        return 0;
    }

    /* EVIOCGID — bus=0x06 (BUS_VIRTUAL), vendor/product/version = 0. */
    if (nr == EVIOC_NR_GID) {
        if (!arg) return -EINVAL;
        struct { uint16_t bustype, vendor, product, version; } id = { 0x06, 0, 0, 0 };
        if (copyout(&id, arg, sizeof(id)) != 0) return -EFAULT;
        return 0;
    }

    /* EVIOCGNAME(sz) — nr in [0x06..0x1f] is the get-name family. */
    if (nr >= EVIOC_NR_GNAME_BASE && nr < 0x20) {
        if (!arg || sz == 0) return -EINVAL;
        const char *name = "substrate-input";
        size_t nlen = strlen(name) + 1;
        if (nlen > sz) nlen = sz;
        if (copyout(name, arg, nlen) != 0) return -EFAULT;
        return (int)nlen;
    }

    return -ENOTTY;
}

/* POLLIN iff there are events queued PAST THE CALLING FD'S CURRENT
 * READ POSITION.  Substrate's evdev queue is global, but each
 * reader's progress is tracked via f_offset.  Earlier this just
 * checked `global_seq > 0` — true forever after the first event —
 * so kdrive's WaitForSomething loop reported the evdev fd readable
 * on every iteration, called read(), and the read parked because
 * there were no unread events for that fd.  Net effect: X server
 * hung on internal client connects until a fresh input event
 * unblocked the read, because select() returned readable on the
 * evdev fd but the read consumed nothing and EvdevPtrRead never
 * came back to look at the socket fds.
 *
 * We pull the fd's current offset out of current_thread->io_file
 * (set by kern_poll across this call); without io_file we fall
 * back to "anything ever" so unfamiliar callers don't regress. */
static int input_poll(fs_node_t *node, void *waiter) {
    (void)node;
    (void)waiter;

    int events = POLLOUT;
    uint64_t caller_seq = 0;
    int caller_has_pos = 0;
    if (current_thread && current_thread->io_file) {
        caller_seq = (uint64_t)current_thread->io_file->f_offset
                     / sizeof(input_event_t);
        caller_has_pos = 1;
    }
    input_lock_take();
    uint64_t snap = global_seq;
    spinlock_release(&input_lock);
    int has_event = caller_has_pos ? (caller_seq < snap) : (snap > 0);
    if (has_event) events |= POLLIN | POLLRDNORM;
    return events;
}

static uint32_t input_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    if (size < sizeof(input_event_t)) return 0;

    uint64_t current_seq = offset / sizeof(input_event_t);

    /* O_NONBLOCK honoring — substrate's input_read used to park
     * unconditionally, ignoring the per-fd flag.  X server probes
     * its evdev fds with poll() + read() and depends on read
     * returning EAGAIN when the queue is empty for this fd. */
    int nonblock = 0;
    if (current_thread && current_thread->io_file &&
        (current_thread->io_file->f_flag & FNONBLOCK)) {
        nonblock = 1;
    }

    // WaitForData
    for (;;) {
        input_lock_take();
        uint64_t snap = global_seq;
        /* If the caller's sequence has fallen so far behind that
         * the events they wanted have already been overwritten in
         * the ring buffer, snap them forward to the oldest event
         * we still have.  This is the recovery path for an evdev
         * reader that opens /dev/input/event0 after the queue has
         * already wrapped (e.g. a new shell-launched program when
         * 64+ events from keyboard typing have happened first) —
         * without this they'd get -EPERM on every read and never
         * receive any events.  We lose history but stay live. */
        if (snap > current_seq + INPUT_QUEUE_SIZE) {
            current_seq = (snap > INPUT_QUEUE_SIZE)
                              ? (snap - INPUT_QUEUE_SIZE)
                              : 0;
        }
        spinlock_release(&input_lock);
        if (current_seq < snap) break;

        if (nonblock) return (uint32_t)-EAGAIN;

        /* Pre-sleep signal check — don't park if a fatal signal is
         * already pending or we'll wedge waiting for an event that
         * may never come.  Same race-free pattern as tty_read. */
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            return (uint32_t)-EINTR;
        }

        /* Mark the sleep interruptible so psignal() will kick us out
         * of sched_sleep when a signal lands.  Without this flag,
         * kill -9 on a process stuck in read() of /dev/input/event0
         * couldn't reap it — which is exactly the substrate gap the
         * tty layer fixed earlier. */
        extern void sched_sleep(void *chan);
        if (current_thread) {
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
            current_thread->wait_chan = &global_event_log;
        }
        sched_sleep(&global_event_log);
        if (current_thread) {
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            current_thread->wait_chan = NULL;
        }

        /* Woken by a signal?  Return EINTR (or whatever partial data
         * we have — but for this path we haven't read anything yet). */
        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            return (uint32_t)-EINTR;
        }
    }

    int read_count = 0;
    input_event_t *out = (input_event_t*)buffer;

    input_lock_take();
    /* Same snap inside the lock — a fast producer could have wrapped
     * the ring between the wait loop and here. */
    if (global_seq > current_seq + INPUT_QUEUE_SIZE) {
        current_seq = (global_seq > INPUT_QUEUE_SIZE)
                          ? (global_seq - INPUT_QUEUE_SIZE)
                          : 0;
    }
    while (current_seq < global_seq && size >= sizeof(input_event_t)) {
        uint64_t idx = current_seq % INPUT_QUEUE_SIZE;
        *out = global_event_log[idx];
        out++;
        read_count++;
        current_seq++;
        size -= sizeof(input_event_t);
    }
    spinlock_release(&input_lock);

    return read_count * sizeof(input_event_t);
}

void input_register_devfs(void) {
    memset(&event_node, 0, sizeof(fs_node_t));
    strlcpy(event_node.name, "input/event0", sizeof(event_node.name));
    event_node.flags = FS_CHARDEVICE;
    event_node.read = &input_read;
    event_node.ioctl = &input_ioctl;
    event_node.poll = &input_poll;
    devfs_register_device(&event_node);
}

