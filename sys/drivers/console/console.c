#include <kern/console.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <string.h>
#include <vm/vm_kmem.h>
#include <stdio.h>
#include <stdarg.h>

// Globals
static console_backend_t *backends = NULL;
static struct tty *console_tty = NULL;

// TTY Driver Methods Forward Declarations
static int console_tty_write(struct tty *tty, const unsigned char *buf, int count);
static int console_tty_put_char(struct tty *tty, unsigned char c);
static void console_tty_set_termios(struct tty *tty);

// Driver Structure
static struct tty_driver console_driver = {
    .driver_name = "console",
    .name = "console",
    .major = 5,
    .minor_start = 1,
    .write = console_tty_write,
    .put_char = console_tty_put_char,
    .set_termios = console_tty_set_termios
};

void console_init(void) {
    backends = NULL;
    tty_init();
    
    // Allocate TTY for console
    console_tty = tty_alloc(&console_driver, 0);
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

// TTY Driver Implementation
static int console_tty_write(struct tty *tty, const unsigned char *buf, int count) {
    (void)tty;
    backend_write((const char*)buf, count);
    return count;
}

static int console_tty_put_char(struct tty *tty, unsigned char c) {
    (void)tty;
    backend_write((const char*)&c, 1);
    return 1;
}

static void console_tty_set_termios(struct tty *tty) {
    if (!tty) return;
    console_backend_t *b = backends;
    while (b) {
        if (b->set_termios) {
            b->set_termios(&tty->termios);
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
    if (!console_tty) return 0;
    return tty_write(console_tty, (const char*)buffer, size);
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

static fs_node_t console_node = {
    .name = "console",
    .flags = FS_CHARDEVICE,
    .read = console_node_read,
    .write = console_node_write,
    .ioctl = console_node_ioctl,
    .poll = console_node_poll
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

void console_attach_std_fds(struct process *proc) {
    if (!proc) return;
    
    // Only init (PID 1) gets console attached by default.
    // Others inherit from parent or open explicitly.
    if (proc->pid != 1) return;

    fs_node_t *node = console_get_node();
    if (!node) {
        kprint("console: Cannot attach std fds - node not found!\n");
        return;
    }

    // Manually populate FDs 0, 1, 2
    for (int i = 0; i < 3; i++) {
        file_t *f = (file_t*)kmalloc(sizeof(file_t));
        if (!f) {
            kprint("console: OOM during std fd init\n");
            return;
        }
        memset(f, 0, sizeof(file_t));
        f->f_data = node;
        f->f_offset = 0;
        f->f_flag = 2; // R/W
        f->f_count = 1;
        
        proc->fds[i] = f;
    }
}
