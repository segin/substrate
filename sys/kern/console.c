#include "console.h"
#include <sys/proc.h>
#include "../vfs/vfs.h"
#include <string.h>

static console_backend_t *backends = NULL;

void console_init(void) {
    backends = NULL;
}

void console_register(console_backend_t *backend) {
    if (!backend) return;
    backend->next = backends;
    backends = backend;
}

void console_write(const char *data, size_t len) {
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

void console_putchar(char c) {
    console_backend_t *b = backends;
    while (b) {
        if (b->putchar) {
            b->putchar(c);
        } else if (b->write) {
            b->write(&c, 1);
        }
        b = b->next;
    }
}

void console_clear(void) {
    console_backend_t *b = backends;
    while (b) {
        if (b->clear) {
            b->clear();
        }
        b = b->next;
    }
}

#define CONSOLE_BUF_SIZE 1024
static char console_in_buf[CONSOLE_BUF_SIZE];
static int console_in_head = 0;
static int console_in_tail = 0;

void console_push_char(char c) {
    int next = (console_in_head + 1) % CONSOLE_BUF_SIZE;
    if (next != console_in_tail) {
        console_in_buf[console_in_head] = c;
        console_in_head = next;
        extern void sched_wakeup(void *chan);
        sched_wakeup(&console_in_buf);
    }
}

extern thread_t *current_thread;
extern void sched_yield(void);

static inline void cli(void) { __asm__ volatile("cli"); }
static inline void sti(void) { __asm__ volatile("sti"); }

int console_read(char *data, size_t len) {
    if (len == 0) return 0;
    
    size_t i = 0;
    cli();
    // Wait for at least one character
    while (console_in_head == console_in_tail) {
        if (current_thread) {
            current_thread->wait_chan = &console_in_buf;
            current_thread->state = THREAD_BLOCKED;
        }
        sti();
        sched_yield();
        cli();
    }
    
    // Read as much as available into request, up to len
    while (i < len && console_in_head != console_in_tail) {
        data[i++] = console_in_buf[console_in_tail];
        console_in_tail = (console_in_tail + 1) % CONSOLE_BUF_SIZE;
    }
    sti();
    return (int)i;
}

static uint32_t console_node_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    console_read((char*)buffer, size);
    return size;
}

static uint32_t console_node_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    console_write((const char*)buffer, size);
    return size;
}

static int console_node_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    (void)node; (void)arg;
    // Basic TTY ioctls for BusyBox shell
    if (request == 0x5401) { // TCGETS
        return 0; // Succeed
    }
    if (request == 0x5402) { // TCSETS
        return 0; // Succeed
    }
    if (request == 0x5413) { // TIOCGWINSZ
        struct winsize {
            unsigned short ws_row;
            unsigned short ws_col;
            unsigned short ws_xpixel;
            unsigned short ws_ypixel;
        } *ws = (struct winsize*)arg;
        if (ws) {
            ws->ws_row = 25;
            ws->ws_col = 80;
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
        }
        return 0;
    }
    if (request == 0x540E) { // TIOCSCTTY
        // Make this the controlling terminal for the process
        // For now, accept success
        return 0;
    }
    if (request == 0x540F) { // TIOCGPGRP
        if (arg) {
            // Return process group ID (just usage of PID for now)
            *(int*)arg = current_process->pid;
        }
        return 0;
    }
    if (request == 0x5410) { // TIOCSPGRP
        // Set process group
        return 0;
    }
    return -1;
}

static fs_node_t console_node = {
    .name = "console",
    .flags = FS_CHARDEVICE,
    .read = console_node_read,
    .write = console_node_write,
    .ioctl = console_node_ioctl
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
    console_write(str, len);
}

#include <sys/file.h>
#include "../vm/vm_kmem.h"

void console_attach_std_fds(struct process *proc) {
    if (!proc) return;

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
        f->node = node;
        f->offset = 0;
        f->flags = 2; // R/W (O_RDWR) - assuming 2 is RDWR from standard fcntl
        // In sys/file.h: R_OK=4, W_OK=2. Flags are usually O_RDONLY=0, O_WRONLY=1, O_RDWR=2.
        // Let's assume standard POSIX values for file flags which sys_open likely uses.
        f->ref_count = 1;
        
        proc->fds[i] = f;
    }
}
