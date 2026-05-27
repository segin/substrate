/*
 * input_subsys.c — per-device input event subsystem.
 *
 * Previously this kept ONE global event queue shared across every
 * registered input device, and exposed ONE /dev/input/event0 node
 * that served events from that global queue.  Two consequences:
 *
 *   - Every reader saw every device's events.  X server binds one
 *     fd as "keyboard" and another as "pointer" — both ended up
 *     reading the same shared queue.  Mouse BTN_LEFT (0x110) events
 *     showed up on the keyboard fd; X's KdEnqueueKeyboardEvent
 *     takes scan_code as `unsigned char`, so 0x110 truncated to
 *     0x10 = KEY_Q.  Mouse buttons produced q/w/e keypresses.
 *
 *   - All devices contended for a single 64-entry ring.  A fast
 *     mouse could push keyboard events off the back.
 *
 * Refactored: each `input_dev_t` carries its own ring buffer + seq
 * counter + lock + fs_node, and is registered as a separate
 * /dev/input/eventN node at register-time.  EVIOCGBIT returns a
 * per-device capability bitmap (keyboard advertises EV_KEY only;
 * pointer advertises EV_REL + EV_KEY for BTN_*).  Readers get
 * only the events for the device they opened.
 */

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

// Global list of devices (head of the next-linked list)
static input_dev_t *input_devices = NULL;

/* Each input_dev_t now carries its own ring + seq + lock.  The
 * legacy global ring is retired.  evnum_next is the allocator
 * for /dev/input/eventN slot numbers — monotonic, never reused. */
static int evnum_next = 0;

extern int copyout(const void *src, void *dst, unsigned int size);
extern int snprintf(char *buf, size_t size, const char *fmt, ...);

/* Forward decls for the fs_node_t hooks. */
static uint32_t input_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer);
static int      input_poll(fs_node_t *node, void *waiter);
static int      input_ioctl(fs_node_t *node, uint32_t request, void *arg);

static inline input_dev_t *dev_from_node(fs_node_t *node) {
    return node ? (input_dev_t *)node->impl : NULL;
}

void input_init(void) {
    input_devices = NULL;
    kprint("Input Subsystem Initialized\n");
}

int input_register_device(input_dev_t *dev) {
    if (!dev) return -1;
    if (dev->registered) return 0;     /* idempotent */

    /* Driver pre-populated name + caps + ops + driver_data.  We own
     * everything below. */
    spinlock_init(&dev->lock, "input-dev");
    dev->seq = 0;
    memset(dev->queue, 0, sizeof(dev->queue));
    dev->evnum = evnum_next++;

    memset(&dev->devfs_node, 0, sizeof(dev->devfs_node));
    snprintf(dev->devfs_node.name, sizeof(dev->devfs_node.name),
             "input/event%d", dev->evnum);
    dev->devfs_node.flags = FS_CHARDEVICE;
    dev->devfs_node.read  = &input_read;
    dev->devfs_node.ioctl = &input_ioctl;
    dev->devfs_node.poll  = &input_poll;
    dev->devfs_node.impl  = (uintptr_t)dev;
    devfs_register_device(&dev->devfs_node);

    dev->next = input_devices;
    input_devices = dev;
    dev->registered = 1;

    kprintf("Input: Registered device '%s' as /dev/input/event%d (caps=0x%x)\n",
            dev->name, dev->evnum, dev->caps);
    return 0;
}

void input_unregister_device(input_dev_t *dev) {
    if (!dev || !dev->registered) return;

    if (input_devices == dev) {
        input_devices = dev->next;
    } else {
        input_dev_t *curr = input_devices;
        while (curr) {
            if (curr->next == dev) {
                curr->next = dev->next;
                break;
            }
            curr = curr->next;
        }
    }
    dev->next = NULL;
    dev->registered = 0;

    /* devfs unregister is not currently exposed; the fs_node stays
     * but no further reads/writes route here (impl pointer is now
     * dangling, but no one will follow it because we cleared
     * `registered` and dropped from the list).  When devfs gains
     * an unregister API, hook it here. */

    kprintf("Input: Unregistered device '%s'\n", dev->name);
}

static void input_notify_readers(input_dev_t *dev) {
    sched_wakeup(&dev->queue);
}

void input_report_event(input_dev_t *dev, uint16_t type, uint16_t code, int32_t value) {
    if (!dev || !dev->registered) return;

    struct timeval tv;
    sys_gettimeofday(&tv, NULL);

    spinlock_acquire(&dev->lock);
    uint64_t idx = dev->seq % INPUT_QUEUE_SIZE;
    dev->queue[idx].time_sec  = tv.tv_sec;
    dev->queue[idx].time_usec = tv.tv_usec;
    dev->queue[idx].type      = type;
    dev->queue[idx].code      = code;
    dev->queue[idx].value     = value;
    dev->seq++;
    spinlock_release(&dev->lock);

    input_notify_readers(dev);
}

void input_sync(input_dev_t *dev) {
    input_report_event(dev, EV_SYN, 0, 0);
}

/*
 * Legacy compatibility wrapper.  Pre-refactor callers used
 * input_enqueue() — a global emit that routed everything through
 * the shared queue.  Now we route it through a synthetic
 * "legacy" device that gets its own /dev/input/eventN.  Drivers
 * should migrate to input_report_event(dev, ...) with their own
 * input_dev_t.
 */
static input_dev_t legacy_dev = { .name = "Legacy Input" };
static int legacy_dev_ready = 0;
void input_enqueue(uint16_t type, uint16_t code, int32_t value) {
    if (!legacy_dev_ready) {
        legacy_dev.caps = (1u << EV_KEY) | (1u << EV_REL) | (1u << EV_ABS);
        input_register_device(&legacy_dev);
        legacy_dev_ready = 1;
    }
    input_report_event(&legacy_dev, type, code, value);
}

/*
 * Linux evdev ioctl compat — see comment at top of file for the
 * surface kdrive exercises.  Refactored to return per-device
 * capability bitmaps based on dev->caps, so the keyboard fd
 * advertises EV_KEY only and the pointer fd advertises EV_REL +
 * EV_KEY (with the BTN_* range marked in the EV_KEY bitmap, not
 * the whole keyboard range).
 *
 * ioctl encoding: bits 30-31 dir, 16-29 size, 8-15 type, 0-7 nr.
 * 'E' (0x45) is the evdev type byte.
 */
#define _SIOC_DIR(req)   (((req) >> 30) & 0x3)
#define _SIOC_SIZE(req)  (((req) >> 16) & 0x3fff)
#define _SIOC_TYPE(req)  (((req) >>  8) & 0xff)
#define _SIOC_NR(req)    (((req) >>  0) & 0xff)
#define EVIOC_TYPE       0x45
#define EVIOC_NR_GVERSION   0x01
#define EVIOC_NR_GID        0x02
#define EVIOC_NR_GNAME_BASE 0x06
#define EVIOC_NR_GBIT_BASE  0x20
#define EVIOC_NR_GRAB       0x90

/* Set a bit in an evdev-style bitmap (LSB-first within a byte). */
static inline void set_bit(uint8_t *buf, unsigned bufsz, unsigned bit) {
    if ((bit >> 3) < bufsz) buf[bit >> 3] |= (uint8_t)(1u << (bit & 7));
}

static int input_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    input_dev_t *dev = dev_from_node(node);
    if (!dev) return -ENOTTY;
    if (_SIOC_TYPE(request) != EVIOC_TYPE) return -ENOTTY;
    uint32_t nr = _SIOC_NR(request);
    uint32_t sz = _SIOC_SIZE(request);

    /* EVIOCGBIT(ev_kind, sz) — per-device capability bitmap. */
    if (nr >= EVIOC_NR_GBIT_BASE && nr < EVIOC_NR_GBIT_BASE + 0x20) {
        if (!arg || sz == 0 || sz > 4096) return -EINVAL;
        uint8_t buf[256];
        memset(buf, 0, sizeof(buf));
        uint32_t ev_kind = nr - EVIOC_NR_GBIT_BASE;

        if (ev_kind == 0) {
            /* Event-type bitmap: bits = (1 << EV_*) for each type
             * this device emits.  EV_SYN is always present. */
            uint32_t types = dev->caps | (1u << EV_SYN);
            for (unsigned t = 0; t < 8; t++) {
                if (types & (1u << t)) set_bit(buf, sizeof(buf), t);
            }
        } else if (ev_kind == EV_REL) {
            /* This device's relative axes.  We don't currently
             * track which specific axes a driver supports — set
             * REL_X, REL_Y, REL_WHEEL if the device claims EV_REL
             * at all.  Drivers can narrow this later. */
            if (dev->caps & (1u << EV_REL)) {
                set_bit(buf, sizeof(buf), REL_X);
                set_bit(buf, sizeof(buf), REL_Y);
                set_bit(buf, sizeof(buf), REL_WHEEL);
            }
        } else if (ev_kind == EV_KEY) {
            /* This device's key/button bitmap.  If the device
             * also claims EV_REL it's a pointer — advertise only
             * BTN_LEFT/RIGHT/MIDDLE/SIDE/EXTRA.  Otherwise it's
             * a keyboard — advertise the full 1..255 range. */
            if (dev->caps & (1u << EV_KEY)) {
                if (dev->caps & (1u << EV_REL)) {
                    set_bit(buf, sizeof(buf), BTN_LEFT);
                    set_bit(buf, sizeof(buf), BTN_RIGHT);
                    set_bit(buf, sizeof(buf), BTN_MIDDLE);
                    set_bit(buf, sizeof(buf), BTN_SIDE);
                    set_bit(buf, sizeof(buf), BTN_EXTRA);
                } else {
                    /* keys 1..255 — bit 0 (key 0 = reserved) stays 0 */
                    for (unsigned k = 1; k <= 255; k++)
                        set_bit(buf, sizeof(buf), k);
                }
            }
        }
        /* EV_ABS and others stay zero. */

        uint32_t remaining = sz;
        uint8_t *dst = (uint8_t *)arg;
        const uint8_t *src = buf;
        uint8_t pad[256];
        memset(pad, 0, sizeof(pad));
        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
            if (src == buf) {
                if (copyout(buf, dst, chunk) != 0) return -EFAULT;
                src = pad;
            } else {
                if (copyout(pad, dst, chunk) != 0) return -EFAULT;
            }
            dst += chunk;
            remaining -= chunk;
        }
        return 0;
    }

    /* EVIOCGRAB(int) — accept; we have no exclusive-owner machinery
     * and never had any.  kdrive only cares that the call doesn't
     * fatal. */
    if (nr == EVIOC_NR_GRAB) return 0;

    /* EVIOCGVERSION(int) — evdev v1.0.1. */
    if (nr == EVIOC_NR_GVERSION) {
        if (!arg) return -EINVAL;
        int ver = 0x010001;
        if (copyout(&ver, arg, sizeof(ver)) != 0) return -EFAULT;
        return 0;
    }

    /* EVIOCGID — bus=VIRTUAL, no real vendor/product. */
    if (nr == EVIOC_NR_GID) {
        if (!arg) return -EINVAL;
        struct { uint16_t bustype, vendor, product, version; } id = { 0x06, 0, 0, 0 };
        if (copyout(&id, arg, sizeof(id)) != 0) return -EFAULT;
        return 0;
    }

    /* EVIOCGNAME(sz) — returns the device's name. */
    if (nr >= EVIOC_NR_GNAME_BASE && nr < 0x20) {
        if (!arg || sz == 0) return -EINVAL;
        const char *name = dev->name[0] ? dev->name : "substrate-input";
        size_t nlen = strlen(name) + 1;
        if (nlen > sz) nlen = sz;
        if (copyout(name, arg, nlen) != 0) return -EFAULT;
        return (int)nlen;
    }

    return -ENOTTY;
}

/* POLLIN iff the calling fd's offset (in events) is behind the
 * device's seq.  Per-fd offset gives each opener an independent
 * cursor into the device's ring — but they all share THIS device's
 * events (not the global mix). */
static int input_poll(fs_node_t *node, void *waiter) {
    (void)waiter;
    input_dev_t *dev = dev_from_node(node);
    if (!dev) return POLLNVAL;

    int events = POLLOUT;
    uint64_t caller_seq = 0;
    int caller_has_pos = 0;
    if (current_thread && current_thread->io_file) {
        caller_seq = (uint64_t)current_thread->io_file->f_offset
                     / sizeof(input_event_t);
        caller_has_pos = 1;
    }
    spinlock_acquire(&dev->lock);
    uint64_t snap = dev->seq;
    spinlock_release(&dev->lock);
    int has_event = caller_has_pos ? (caller_seq < snap) : (snap > 0);
    if (has_event) events |= POLLIN | POLLRDNORM;
    return events;
}

static uint32_t input_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    input_dev_t *dev = dev_from_node(node);
    if (!dev) return 0;
    if (size < sizeof(input_event_t)) return 0;

    uint64_t current_seq = offset / sizeof(input_event_t);

    int nonblock = 0;
    if (current_thread && current_thread->io_file &&
        (current_thread->io_file->f_flag & FNONBLOCK)) {
        nonblock = 1;
    }

    for (;;) {
        spinlock_acquire(&dev->lock);
        uint64_t snap = dev->seq;
        /* Wrap-recovery: if the caller fell more than a ring's worth
         * behind, snap them forward to the oldest event we still
         * have.  Same logic as before but per-device. */
        if (snap > current_seq + INPUT_QUEUE_SIZE) {
            current_seq = (snap > INPUT_QUEUE_SIZE)
                              ? (snap - INPUT_QUEUE_SIZE)
                              : 0;
        }
        spinlock_release(&dev->lock);
        if (current_seq < snap) break;

        if (nonblock) return (uint32_t)-EAGAIN;

        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            return (uint32_t)-EINTR;
        }

        extern void sched_sleep(void *chan);
        if (current_thread) {
            current_thread->flags |= THREAD_F_INTERRUPTIBLE;
            current_thread->wait_chan = &dev->queue;
        }
        sched_sleep(&dev->queue);
        if (current_thread) {
            current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
            current_thread->wait_chan = NULL;
        }

        if (current_thread &&
            (current_thread->sig_pending & ~current_thread->sig_mask)) {
            return (uint32_t)-EINTR;
        }
    }

    int read_count = 0;
    input_event_t *out = (input_event_t*)buffer;

    spinlock_acquire(&dev->lock);
    if (dev->seq > current_seq + INPUT_QUEUE_SIZE) {
        current_seq = (dev->seq > INPUT_QUEUE_SIZE)
                          ? (dev->seq - INPUT_QUEUE_SIZE)
                          : 0;
    }
    while (current_seq < dev->seq && size >= sizeof(input_event_t)) {
        uint64_t idx = current_seq % INPUT_QUEUE_SIZE;
        *out = dev->queue[idx];
        out++;
        read_count++;
        current_seq++;
        size -= sizeof(input_event_t);
    }
    spinlock_release(&dev->lock);

    return read_count * sizeof(input_event_t);
}
