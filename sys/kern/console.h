#ifndef _KERN_CONSOLE_H
#define _KERN_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

// Console Backend Interface
typedef struct console_backend {
    const char *name;
    void (*write)(const char *data, size_t len);
    void (*putchar)(char c);
    void (*clear)(void);
    struct console_backend *next;
} console_backend_t;

// API
void console_init(void);

// GEOM Init
void geom_init(void);
void geom_mbr_init(void);
void geom_bsd_init(void);
void geom_gpt_init(void);

void console_register(console_backend_t *backend);
void console_write(const char *data, size_t len);
void console_putchar(char c);
void console_clear(void);

// Helper for formatted printing (replaces kprintf later)
void kprint(const char *str);

#endif
