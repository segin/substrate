#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <arch/i386/intr.h>
#include <arch/x86-common/lapic.h>
#include <drivers/console/uart/uart.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/file.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/version.h>
#include <sys/file.h>
#include <sys/lock.h>
#include <sys/major.h>
#include <sys/proc.h>
#include <sys/session.h>
#include <sys/smp.h>
#include <sys/tty.h>
#include <sys/vt.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

// Globals
static console_backend_t *backends = NULL;
static struct tty *console_tty = NULL;
static spinlock_t console_input_lock = SPINLOCK_INIT("console_input");

/*
 * Console output serialization.  Without it, two CPUs writing at once interleave
 * byte-by-byte (the garbled SMP boot banner).  Held across a whole backend write
 * and each klog append so every message is emitted atomically.
 *
 *  - Uniprocessor: no lock and no IRQ change -- there is no contention, and this
 *    runs before the LAPIC is up, so lapic_get_id() would be unsafe.  Behaviour
 *    is exactly as before on UP.
 *  - SMP: spin for the lock with IRQs disabled.  IRQs off means the panic IPI
 *    (maskable) can't strand the lock on a mid-write CPU -- it finishes and
 *    releases first; panic's own output uses a separate direct emitter anyway.
 *  - Recursion-safe: a backend write that itself kprints on the same CPU (e.g.
 *    the framebuffer path) finds this CPU already the owner and proceeds without
 *    re-locking, so substrate's non-recursive spinlock never tripwires.
 *
 * The owner is identified by lapic_get_id() (the physical APIC id), NOT the
 * logical smp_get_cpu_id(): during AP bring-up the latter routes through
 * percpu_get() and can fall back to CPU 0 for an AP whose percpu mapping is not
 * established yet, so several APs would compute id 0, all false-match the owner
 * check, skip the lock, and interleave.  lapic_get_id() is a direct MMIO read
 * that is correct per-CPU from the moment the AP runs (it prints its own LAPIC
 * ID in the bring-up banner).
 */
static spinlock_t console_out_lock = SPINLOCK_INIT("console_out");
static volatile int console_out_owner = -1;     /* LAPIC id emitting; -1 = none */
static volatile int console_out_depth;          /* UP reentry depth */

/*
 * UP is NOT safe without this.  The original code returned immediately when
 * smp_get_cpu_count() <= 1, on the assumption that one CPU means one writer.
 * It does not: the kernel is preemptible, so the timer can interrupt a thread
 * midway through emitting a line and schedule another that also kprints, and
 * ISRs print too.  The result is exactly the shredded output seen at boot --
 *
 *   VFS: Mounted sysfs on /sys
 *   Freeing setup mekinitmory... :D one.
 *   SFreeingtarting init process...
 *
 * -- kinit's line spliced through kmain's, one character group at a time.
 *
 * On UP, disabling interrupts around the emit IS the mutual exclusion: with
 * IF clear neither preemption nor an ISR can enter, so no lock is needed and
 * none can deadlock.  Reentry (a backend whose own write path kprints) is
 * then detectable with a plain depth counter, because with interrupts off
 * only we can be touching it.
 *
 * A sleeping mutex would be wrong here whatever the CPU count: kprint() is
 * called from interrupt handlers, and sleeping in one is fatal.
 */
static int console_out_enter(unsigned long *pflags) {
    unsigned long flags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    *pflags = flags;

    if (smp_get_cpu_count() <= 1) {
        if (console_out_depth) {
            /* Reentry.  Interrupts were already off from the outer enter, so
             * restoring `flags` here is a no-op; leave() must not undo them. */
            __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
            return 0;
        }
        console_out_depth = 1;
        return 1;
    }

    int id = (int)lapic_get_id();
    if (console_out_owner == id) {
        /* Reentry on this CPU: we already hold the lock. */
        __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
        return 0;
    }
    spinlock_acquire(&console_out_lock);
    console_out_owner = id;
    return 1;
}

static void console_out_leave(int held, unsigned long flags) {
    if (!held)
        return;                                  /* reentry: nothing to undo */
    if (smp_get_cpu_count() <= 1) {
        console_out_depth = 0;
    } else {
        console_out_owner = -1;
        spinlock_release(&console_out_lock);
    }
    __asm__ volatile("pushl %0; popfl" :: "r"(flags) : "memory", "cc");
}

#define CONSOLE_INPUT_BUF_SIZE 256
static char console_input_buf[CONSOLE_INPUT_BUF_SIZE];
static unsigned int console_input_head;
static unsigned int console_input_tail;
static unsigned int console_input_count;

static struct tty *console_resolve_tty(void) {
    struct tty *tty = vt_get_active_tty();
    if (tty) {
        return tty;
    }
    tty = tty_get(vt_get_active());
    if (tty) {
        return tty;
    }
    return console_tty;
}

void console_init(void) {
    backends = NULL;
    console_input_head = 0;
    console_input_tail = 0;
    console_input_count = 0;
    tty_init();
}

void console_set_tty(struct tty *tty) {
    console_tty = tty;
}

void console_register(console_backend_t *backend) {
    if (!backend) return;
    
    // Check if already registered to prevent circular lists
    console_backend_t *curr = backends;
    while (curr) {
        if (curr == backend) return;
        curr = curr->next;
    }

    backend->next = backends;
    backends = backend;
}

int console_get_terminal_size(int *cols, int *rows) {
    console_backend_t *backend = backends;

    if (!cols || !rows) {
        return -1;
    }

    while (backend) {
        if (backend->get_terminal_size &&
            backend->get_terminal_size(cols, rows) == 0) {
            return 0;
        }
        backend = backend->next;
    }

    return -1;
}

/*
 * "slow" boot option: pause after every line of console output.
 *
 * On a machine with no serial port the boot log scrolls past far faster than
 * it can be read, and the interesting part is the last screenful before a
 * hang -- exactly the part that cannot be scrolled back to.  This spaces the
 * output out so it can be read, or photographed, as it goes.
 */
#define CONSOLE_SLOW_MS 500

static int console_slow_state = -1;   /* -1 = command line not parsed yet */

static int console_slow_enabled(void) {
    if (console_slow_state < 0) {
        /* Do not cache an answer from before the command line existed: every
         * line printed during early boot would otherwise latch "off". */
        if (!cmdline_is_initialized()) {
            return 0;
        }
        console_slow_state = cmdline_has("slow") ? 1 : 0;
    }
    return console_slow_state;
}

static void console_slow_pause(void) {
    /* Not get_uptime_ms(): that counts timer-tick interrupts, and most of the
     * boot output this option exists to pace is printed with interrupts
     * disabled, where the tick can never advance.  timer_busywait_ms() polls
     * the PIT directly and so works in that window. */
    timer_busywait_ms(CONSOLE_SLOW_MS);
}

// Low-level backend write (used by kprint directly or via TTY)
static void backend_write(const char *data, size_t len) {
    unsigned long f;
    int held = console_out_enter(&f);
    console_backend_t *b = backends;
    while (b) {
        if (b->write) {
            b->write(data, len);
        } else if (b->putchar) {
            for (size_t i = 0; i < len; i++) {
                b->putchar(data[i]);
            }
        }
        b = b->next;
    }
    console_out_leave(held, f);

    /*
     * Deliberately outside the output lock: console_out_enter() disables
     * interrupts, so waiting on the timer tick while holding it would wait
     * for a tick that cannot arrive.
     */
    if (console_slow_enabled()) {
        for (size_t i = 0; i < len; i++) {
            if (data[i] == '\n') {
                console_slow_pause();
                break;
            }
        }
    }
}

// Public wrapper for kernel printing
void console_write(const char *data, size_t len) {
    backend_write(data, len);
}

void console_putchar(char c) {
    backend_write(&c, 1);
}

void console_clear(void) {
    console_backend_t *b = backends;
    while (b) {
        if (b->clear) b->clear();
        b = b->next;
    }
}

// Push input to TTY layer (called by keyboard handler etc.)
void console_push_char(char c) {
    struct tty *tty = console_resolve_tty();
    if (tty) {
        tty_flip_buffer_push(tty, c);
        return;
    }

    {
        uint32_t flags = intr_disable();
        spinlock_acquire(&console_input_lock);
        if (console_input_count < CONSOLE_INPUT_BUF_SIZE) {
            console_input_buf[console_input_head] = c;
            console_input_head = (console_input_head + 1U) % CONSOLE_INPUT_BUF_SIZE;
            console_input_count++;
        }
        spinlock_release(&console_input_lock);
        intr_restore(flags);
    }

    sched_wakeup((void *)&console_input_count);
}

// DevFS Hooks using TTY Layer
static size_t console_node_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    struct tty *tty = console_resolve_tty();
    if (tty) {
        return tty_read(tty, (char*)buffer, size);
    }

    if (!buffer || size == 0) {
        return 0;
    }

    for (;;) {
        size_t count = 0;
        uint32_t flags = intr_disable();
        spinlock_acquire(&console_input_lock);

        while (count < size && console_input_count > 0) {
            buffer[count++] = (uint8_t)console_input_buf[console_input_tail];
            console_input_tail = (console_input_tail + 1U) % CONSOLE_INPUT_BUF_SIZE;
            console_input_count--;
        }

        if (count > 0) {
            spinlock_release(&console_input_lock);
            intr_restore(flags);
            return count;
        }

        current_thread->wait_chan = (void *)&console_input_count;
        current_thread->state = THREAD_BLOCKED;
        spinlock_release(&console_input_lock);
        intr_restore(flags);
        sched_yield();
    }
}

static size_t console_node_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    struct tty *tty = console_resolve_tty();
    if (!tty) {
        backend_write((const char *)buffer, size);
        return size;
    }

    int written = tty_write(tty, (const char*)buffer, size);
    if (serial_debug_enabled && size > 0) {
        uart_write((const char *)buffer, size);
    }
    if (written <= 0 && size > 0) {
        backend_write((const char *)buffer, size);
        return size;
    }
    return (size_t)written;
}

static int console_node_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node;
    struct tty *tty = console_resolve_tty();
    if (!tty) return -1;
    /* Same personality ioctl translation as tty_fs_ioctl: a BSD caller's
     * tty/syscons ioctls are accepted by the device node, not the syscall. */
    int handled = 0;
    int r = perso_tty_ioctl(tty, request, arg, &handled);
    if (handled) {
        return r;
    }
    return tty_ioctl(tty, request, (unsigned long)arg);
}

static int console_node_poll(fs_node_t *node, void *waiter) {
    (void)node;
    struct tty *tty = console_resolve_tty();
    if (!tty) return 0; // POLLNVAL?
    return tty_poll(tty, waiter);
}

static void console_node_open(fs_node_t *node) {
    (void)node;
    /*
     * /dev/console is a singleton façade over the already-installed console
     * TTY. Opening the vnode must not recurse into TTY open/refcount paths
     * during early stdio attachment.
     */
}

static void console_node_close(fs_node_t *node) {
    (void)node;
    /*
     * Matching close is a no-op for the same reason as open: the backing
     * console TTY lifetime is owned by the console/video/uart bring-up, not
     * by transient /dev/console vnode opens.
     */
}

static fs_node_t console_node = {
    .name = "console",
    .flags = FS_CHARDEVICE,
    .mask  = 0600,
    .rdev  = makedev(TTYAUX_MAJOR, TTYAUX_MINOR_CONSOLE),
    .read = console_node_read,
    .write = console_node_write,
    .ioctl = console_node_ioctl,
    .poll = console_node_poll,
    .open = console_node_open,
    .close = console_node_close
};

fs_node_t *console_get_node(void) {
    return &console_node;
}

/*
 * revoke(2) support for the /dev/console façade.  The vnode carries no
 * struct tty in node->ptr (it resolves the live console TTY on demand),
 * so a generic node->ptr revoke can't reach it — route to the resolved
 * TTY here.  Used by sys_revoke() so NetBSD init can release the console
 * before claiming it for a single-user shell / getty.
 */
int console_revoke(void) {
    return tty_revoke(console_resolve_tty());
}

void console_register_devfs(void) {

    devfs_register_device(&console_node);
}

/*
 * Kernel message ring buffer.  Every kprint()/kprintf() byte is teed in
 * here so userland can retrieve the boot/kernel log via /proc/kmsg (and
 * the dmesg(1) utility).  Lock-free best-effort: a concurrent writer from
 * IRQ context may interleave bytes (the console itself has the same
 * property), but it can never corrupt out of bounds.
 */
/*
 * The ring starts on a small static 16 KiB buffer — kprint() runs long before
 * the allocator exists — and is grown once installed RAM is known, by
 * klog_init_dynamic() (called from kmain() after kmem_init()): 64 KiB at
 * 128 MiB, 256 KiB at 256 MiB, quadrupling per RAM doubling, capped at 1 MiB.
 * Small machines keep the modest 16 KiB ring.
 */
#define KLOG_MIN_SIZE (16u * 1024u)
#define KLOG_MAX_SIZE (1024u * 1024u)
static char     klog_static[KLOG_MIN_SIZE];
static char    *klog_buf = klog_static;
static size_t   klog_capacity = KLOG_MIN_SIZE;
static uint32_t klog_head;       /* next write index */
static int      klog_wrapped;    /* set once the ring has wrapped around */

static void klog_append(const char *s, size_t len) {
    unsigned long f;
    int held = console_out_enter(&f);
    for (size_t i = 0; i < len; i++) {
        klog_buf[klog_head] = s[i];
        if (++klog_head >= klog_capacity) {
            klog_head = 0;
            klog_wrapped = 1;
        }
    }
    console_out_leave(held, f);
}

/* Total bytes currently held in the ring (oldest .. newest). */
size_t klog_size(void) {
    return klog_wrapped ? klog_capacity : klog_head;
}

/* Copy the ring oldest-first into dst (up to dstlen).  Returns bytes copied. */
size_t klog_read(char *dst, size_t dstlen) {
    size_t avail = klog_size();
    size_t start = klog_wrapped ? klog_head : 0;
    size_t n = 0;
    while (n < avail && n < dstlen) {
        dst[n] = klog_buf[(start + n) % klog_capacity];
        n++;
    }
    return n;
}

/*
 * Grow the kernel-log ring to a size scaled to installed RAM.  Called once
 * from kmain() after kmem_init() makes kmalloc() usable.  The existing log
 * (captured on the static ring during early boot) is preserved.  Never shrinks.
 */
void klog_init_dynamic(size_t ram_bytes) {
    size_t target = KLOG_MIN_SIZE;
    size_t cand = 64u * 1024u;
    size_t threshold = 128u * 1024u * 1024u;
    size_t ram;
    char *nb;
    uint32_t flags;
    size_t n;

    /* The allocator reports slightly less than the installed RAM (kernel image
     * + reserved regions), so round up to a power of two: a 256 MiB box that
     * has ~240 MiB usable still lands on the 256 MiB tier. */
    ram = 1u;
    while (ram < ram_bytes && ram < (1u << 31)) {
        ram <<= 1;
    }

    while (ram >= threshold && cand <= KLOG_MAX_SIZE) {
        target = cand;
        cand *= 4u;
        threshold *= 2u;
    }
    if (target <= klog_capacity) {
        return;
    }

    nb = (char *)kmalloc(target);
    if (!nb) {
        return;                     /* stay on the static ring */
    }

    flags = intr_disable();         /* atomic swap vs. an IRQ-context writer */
    n = klog_read(nb, target);      /* carry the early-boot log forward */
    klog_buf = nb;
    klog_capacity = target;
    klog_head = (uint32_t)n;
    klog_wrapped = 0;
    intr_restore(flags);

    kprintf("klog: ring sized to %u KiB for %u MiB RAM\n",
            (unsigned)(target / 1024u),
            (unsigned)(ram_bytes / (1024u * 1024u)));
}

void kprint(const char *str) {
    if (!str) return;
    size_t len = 0;
    const char *s = str;
    while (*s++) len++;

    /* Hold the console across BOTH halves.  Serialising only backend_write
     * still let a preemption land between the ring-buffer append and the
     * emit, so dmesg and the console disagreed on ordering even when each
     * line reached the console whole.  console_out_enter is reentrant, so the
     * nested acquire inside backend_write is a no-op. */
    unsigned long f;
    int held = console_out_enter(&f);
    klog_append(str, len);
    backend_write(str, len);
    console_out_leave(held, f);
}

int kprintf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    kprint(buf);
    return ret;
}



int console_read(char *data, size_t len) {
    return (int)console_node_read(&console_node, 0, len, (uint8_t *)data);
}

void console_attach_std_fds(struct process *proc) {
    struct tty *tty;
    fs_node_t *node;

    if (!proc) return;

    /*
     * This helper is called explicitly from kinit_task() before init has
     * exec'd out of its kernel-task wrapper. Do not reject kernel tasks here:
     * the call site, not this helper, decides which process should inherit the
     * console stdio set.
     */

    tty = console_resolve_tty();
    node = console_get_node();
    if (!node) {
        kprint("console: Cannot attach std fds - node not found!\n");
        return;
    }

    // Associate process with console TTY
    if (tty) {
        proc->tty = tty;
        /*
         * Init becomes session leader before this call.
         * Make that session/pgrp foreground on the console so
         * job-control shells don't spin on tcgetpgrp/getpgrp mismatch.
         */
        if (proc->p_pgrp && proc->p_pgrp->pg_session) {
            proc->tty->session = proc->p_pgrp->pg_session->s_sid;
            proc->tty->pgrp = proc->p_pgrp->pg_id;
        }
    }

    // Populate FDs 0, 1, 2 (stdin, stdout, stderr)
    for (int i = 0; i < 3; i++) {
        // If FD is already occupied (unlikely for PID 1 at this stage), skip it.
        if (proc->fds[i]) continue;

        file_t *f = file_alloc();
        if (!f) {
            kprint("console: system file table full during std fd init\n");
            return;
        }

        memset(f, 0, sizeof(file_t));
        f->f_type = DTYPE_VNODE;
        f->f_data = node;
        f->f_offset = 0;
        f->f_flag = FREAD | FWRITE;
        f->f_count = 1;
        
        // Notify VFS that we've opened the node
        open_fs(node, 1, 1);
        
        proc_set_fd(proc, i, f);
    }

    // Update next_fd hint if it was 0
    if (proc->next_fd < 3) {
        proc->next_fd = 3;
    }
}
