#ifndef _SYS_TTY_H
#define _SYS_TTY_H

#include <sys/termios.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/types.h>
#include <stddef.h>
#include <stdint.h>

#define TTY_BUF_SIZE 2048

#define TTY_INPUT_BREAK        0x01U
#define TTY_INPUT_PARITY_ERROR 0x02U

struct tty;

typedef struct tty_buffer {
    char data[TTY_BUF_SIZE];
    int head;
    int tail;
    int count;
} tty_buffer_t;

struct tty_driver {
    const char *driver_name;
    const char *name; // e.g., "tty"
    int major;
    int minor_start;
    int num;
    int (*install)(struct tty_driver *driver, struct tty *tty);
    void (*remove)(struct tty_driver *driver, struct tty *tty);
    int (*open)(struct tty *tty);
    void (*close)(struct tty *tty);
    int (*write)(struct tty *tty, const unsigned char *buf, int count);
    int (*put_char)(struct tty *tty, unsigned char c);
    void (*flush_chars)(struct tty *tty);
    int (*write_room)(struct tty *tty);
    int (*chars_in_buffer)(struct tty *tty);
    int (*ioctl)(struct tty *tty, uint32_t cmd, unsigned long arg);
    void (*set_termios)(struct tty *tty);
    void (*throttle)(struct tty *tty);
    void (*unthrottle)(struct tty *tty);
};

struct tty {
    int magic;
    struct tty_driver *driver;
    int index; // Device index (e.g., 0 for tty0)
    int count; // Refcount
    int driver_active; // Driver open callback completed successfully
    int lifecycle_busy; // Open/close transition in progress
    
    struct termios termios;
    struct winsize winsize;
    
    int pgrp; // Foreground process group
    int session; // Session ID
    
    // Buffers
    // read_buf: cooked canonical data waiting to be read by user
    tty_buffer_t read_buf; 
    
    // raw_buf: incoming raw data from hardware (interrupts)
    tty_buffer_t raw_buf;
    
    // write_buf: data waiting to be sent to hardware (if buffered)
    tty_buffer_t write_buf;
    
    // Synchronization
    spinlock_t lock;
    int delct; // Delimiter count in raw_buf
    int canon_len; // Current canonical line length for echo/editing
    int output_col; // Current output column for tab/newline expansion
    
    // Wait queues
    void *read_wait;
    void *write_wait;
    void *poll_wait;
    
    // State flags
    int stopped; // Output stopped (IXON)
    int input_stopped; // Input stopped (IXOFF)
    int hung_up;       // Carrier-lost / peer gone — read returns 0 (EOF),
                       // write returns -EIO.  Set by drivers (PTY master
                       // close, modem CD drop) and checked by tty_read /
                       // tty_write inside the wait loop.

    void *driver_data; // Private driver data
    struct fs_node *devnode; // Published device node (for direct stdio attachment)
};

// API
void tty_init(void);
struct tty *tty_alloc(struct tty_driver *driver, int idx);
void tty_free(struct tty *tty);
void tty_register_device(struct tty *tty, char *name);
struct tty *tty_get(int idx);

// File Operations (to be called by VFS wrapper)
int tty_open(struct tty *tty);
void tty_close(struct tty *tty);
int tty_read(struct tty *tty, char *buf, int len);
int tty_write(struct tty *tty, const char *buf, int len);
void tty_start(struct tty *tty);
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg);
/*
 * Personality ioctl translation for tty device nodes.  Implemented in the
 * exec personality layer (compat.c).  Translates a BSD-personality caller's
 * tty/syscons ioctl to native semantics on `tp`; sets *handled when it
 * consumed the request, else leaves it 0 so the node uses tty_ioctl().
 */
int perso_tty_ioctl(struct tty *tp, uint32_t request, void *arg, int *handled);
int tty_ioctl_kern(struct tty *tty, uint32_t cmd, uintptr_t arg);
int tty_revoke(struct tty *tty);
int tty_poll(struct tty *tty, void *waiter);
int tty_check_change(struct tty *tty);

// Input processing (called by driver interrupt/worker)
void tty_flip_buffer_push(struct tty *tty, char c);
void tty_flip_buffer_push_status(struct tty *tty, char c, uint32_t status);

/*
 * tty_inject_input_locked - push 'len' bytes into tty->raw_buf and wake
 * any read/poll waiters.  CALLER MUST ALREADY HOLD tty->lock.
 *
 * Used by ANSI-handler `respond` callbacks (e.g. CSI Device Status
 * Report, Device Attributes), which fire from inside the write path
 * with tty->lock already held — re-acquiring the lock there triggers
 * the spinlock_acquire deadlock check.  Returns the number of bytes
 * actually accepted (may be less than len if raw_buf is full).
 */
size_t tty_inject_input_locked(struct tty *tty, const char *buf, size_t len);

// Helper
void tty_default_termios(struct termios *t);

// VFS-facing tty operations exposed to other drivers (PTYs, vt, etc.).
struct fs_node;
size_t tty_fs_read(struct fs_node *node, off_t offset, size_t size,
                   uint8_t *buffer);
size_t tty_fs_write(struct fs_node *node, off_t offset, size_t size,
                    const uint8_t *buffer);
int    tty_fs_ioctl(struct fs_node *node, uint32_t request, void *arg);
void   tty_fs_open(struct fs_node *node);
void   tty_fs_close(struct fs_node *node);
int    tty_fs_poll(struct fs_node *node, void *waiter);

#endif
