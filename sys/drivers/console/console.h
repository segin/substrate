#ifndef _KERN_CONSOLE_H
#define _KERN_CONSOLE_H

#include <stddef.h>
#include <stdint.h>

struct fs_node;
typedef struct fs_node fs_node_t;

// Console Backend Interface
struct termios;
typedef struct console_backend {
    const char *name;
    void (*write)(const char *data, size_t len);
    void (*putchar)(char c);
    void (*clear)(void);
    struct console_backend *next;
    void (*set_termios)(struct termios *t);
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
int console_read(char *data, size_t len);
void console_putchar(char c);
void console_clear(void);
void console_push_char(char c);

// For /dev/tty proxy
fs_node_t *console_get_node(void);

// Helper for formatted printing
void kprint(const char *str);
int kprintf(const char *fmt, ...);
char *kasprintf(const char *fmt, ...);
#ifndef HOST_TEST
char *kvasprintf(const char *fmt, __builtin_va_list ap);
#endif

struct process;
// Attach console to a process's FDs 0, 1, 2
void console_attach_std_fds(struct process *proc);

// Register /dev/console
void console_register_devfs(void);

#endif
