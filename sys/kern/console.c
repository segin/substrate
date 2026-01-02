#include "console.h"
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

void console_read(char *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        while (console_in_head == console_in_tail) {
            extern void sched_sleep(void *chan);
            sched_sleep(&console_in_buf);
        }
        data[i++] = console_in_buf[console_in_tail];
        console_in_tail = (console_in_tail + 1) % CONSOLE_BUF_SIZE;
    }
}

static uint32_t console_node_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    console_read((char*)buffer, size);
    return size;
}

static uint32_t console_node_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    console_write((const char*)buffer, size);
    return size;
}

static fs_node_t console_node = {
    .name = "console",
    .flags = FS_CHARDEVICE,
    .read = console_node_read,
    .write = console_node_write
};

fs_node_t *console_get_node(void) {
    return &console_node;
}

void kprint(const char *str) {
    if (!str) return;
    size_t len = 0;
    const char *s = str;
    while (*s++) len++;
    console_write(str, len);
}
