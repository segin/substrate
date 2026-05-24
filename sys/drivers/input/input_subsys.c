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

    /* EVIOCGBIT(ev, sz) — nr is 0x20+ev, sz is the user buffer size. */
    if (nr >= EVIOC_NR_GBIT_BASE && nr < EVIOC_NR_GBIT_BASE + 0x20) {
        if (!arg || sz == 0 || sz > 4096) return -EINVAL;
        uint8_t zero[256];
        memset(zero, 0, sizeof(zero));
        uint32_t remaining = sz;
        uint8_t *dst = (uint8_t *)arg;
        while (remaining > 0) {
            uint32_t chunk = remaining > sizeof(zero) ? sizeof(zero) : remaining;
            if (copyout(zero, dst, chunk) != 0) return -EFAULT;
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

/* POLLIN iff there are events the caller hasn't consumed yet.  The
 * caller's current_seq is f_offset / sizeof(input_event_t); compare
 * to the global producer sequence.  Substrate's kern_poll repolls
 * every ~10ms so we don't need a real wait-channel wakeup. */
extern int kprintf(const char *, ...);
static int input_poll(fs_node_t *node, void *waiter) {
    (void)node;
    (void)waiter;

    int events = POLLOUT;        /* never block on write (no-op) */
    file_t *f = current_process ? current_process->fds[0] : NULL;
    uint64_t reader_seq = 0;
    /* We can't introspect WHICH fd the poller used; the convention
     * across substrate poll callbacks is to inspect the global
     * producer state versus the reader's offset.  We pessimistically
     * report POLLIN whenever ANY events are in the queue: callers
     * who've consumed past global_seq will just read 0 bytes and
     * re-poll, which is benign. */
    (void)f;
    (void)reader_seq;

    input_lock_take();
    int has_event = (global_seq > 0);
    spinlock_release(&input_lock);
    if (has_event) events |= POLLIN | POLLRDNORM;
    return events;
}

static uint32_t input_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    if (size < sizeof(input_event_t)) return 0;

    uint64_t current_seq = offset / sizeof(input_event_t);

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
        extern void sched_sleep(void *chan);
        sched_sleep(&global_event_log);
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

