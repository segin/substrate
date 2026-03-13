#include <kern/console.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <kern/version.h>
#include <sys/session.h>
#include <drivers/console/uart/uart.h>
#include <sys/vt.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/lock.h>
#include <kern/sched.h>
#include <intr.h>

// Globals
static console_backend_t *backends = NULL;
static struct tty *console_tty = NULL;
static spinlock_t console_input_lock = SPINLOCK_INIT("console_input");

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

// Low-level backend write (used by kprint directly or via TTY)
static void backend_write(const char *data, size_t len) {
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

void console_register_devfs(void) {
    extern void devfs_register_device(fs_node_t *node);
    devfs_register_device(&console_node);
}

void kprint(const char *str) {
    if (!str) return;
    size_t len = 0;
    const char *s = str;
    while (*s++) len++;
    backend_write(str, len);
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

#include <kern/file.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <string.h>

int console_read(char *data, size_t len) {
    return (int)console_node_read(&console_node, 0, len, (uint8_t *)data);
}

void console_attach_std_fds(struct process *proc) {
    if (!proc) return;

    /*
     * This helper is called explicitly from kinit_task() before init has
     * exec'd out of its kernel-task wrapper. Do not reject kernel tasks here:
     * the call site, not this helper, decides which process should inherit the
     * console stdio set.
     */

    fs_node_t *node = console_get_node();
    if (!node) {
        kprint("console: Cannot attach std fds - node not found!\n");
        return;
    }

    // Associate process with console TTY
    if (console_resolve_tty()) {
        proc->tty = console_resolve_tty();
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
