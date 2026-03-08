#include <kern/console.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <kern/version.h>
#include <sys/session.h>
#include <drivers/console/uart/uart.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <stdio.h>
#include <stdarg.h>

// Globals
static console_backend_t *backends = NULL;
static struct tty *console_tty = NULL;

void console_init(void) {
    backends = NULL;
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
    if (console_tty) {
        tty_flip_buffer_push(console_tty, c);
    }
}

// DevFS Hooks using TTY Layer
static size_t console_node_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    if (!console_tty) return 0;
    return tty_read(console_tty, (char*)buffer, size);
}

static size_t console_node_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    if (!console_tty) {
        backend_write((const char *)buffer, size);
        return size;
    }

    int written = tty_write(console_tty, (const char*)buffer, size);
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
    if (!console_tty) return -1;
    return tty_ioctl(console_tty, request, (unsigned long)arg);
}

static int console_node_poll(fs_node_t *node, void *waiter) {
    (void)node;
    if (!console_tty) return 0; // POLLNVAL?
    return tty_poll(console_tty, waiter);
}

static void console_node_open(fs_node_t *node) {
    (void)node;
    if (console_tty) {
        tty_open(console_tty);
    }
}

static void console_node_close(fs_node_t *node) {
    (void)node;
    if (console_tty) {
        tty_close(console_tty);
    }
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

void console_attach_std_fds(struct process *proc) {
    if (!proc) return;

    /*
     * This helper is called explicitly from kinit_task() for the init
     * process. Do not hard-code a numeric PID here; background kernel
     * workers may be spawned before init during bring-up.
     */
    if (proc->is_kernel_task) return;

    fs_node_t *node = console_get_node();
    if (!node) {
        kprint("console: Cannot attach std fds - node not found!\n");
        return;
    }

    // Associate process with console TTY
    if (console_tty) {
        proc->tty = console_tty;
        /*
         * Init becomes session leader before this call.
         * Make that session/pgrp foreground on the console so
         * job-control shells don't spin on tcgetpgrp/getpgrp mismatch.
         */
        if (proc->p_pgrp && proc->p_pgrp->pg_session) {
            console_tty->session = proc->p_pgrp->pg_session->s_sid;
            console_tty->pgrp = proc->p_pgrp->pg_id;
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
