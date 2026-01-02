#include "console.h"
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

void kprint(const char *str) {
    if (!str) return;
    size_t len = 0;
    const char *s = str;
    while (*s++) len++;
    console_write(str, len);
}
