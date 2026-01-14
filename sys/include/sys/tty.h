#ifndef _SYS_TTY_H
#define _SYS_TTY_H

#include <sys/termios.h>
#include <sys/proc.h> 
#include <stddef.h>
#include <stdint.h>

#define TTY_BUF_SIZE 2048

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
    
    int (*open)(struct tty *tty);
    void (*close)(struct tty *tty);
    int (*write)(struct tty *tty, const unsigned char *buf, int count);
    int (*put_char)(struct tty *tty, unsigned char c);
    void (*flush_chars)(struct tty *tty);
    int (*write_room)(struct tty *tty);
    int (*chars_in_buffer)(struct tty *tty);
    int (*ioctl)(struct tty *tty, uint32_t cmd, unsigned long arg);
};

struct tty {
    int magic;
    struct tty_driver *driver;
    int index; // Device index (e.g., 0 for tty0)
    int count; // Refcount
    
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
    // TODO: Add mutex/spinlock
    int buf_lock;
    int delct; // Delimiter count in raw_buf
    
    // Wait queues
    void *read_wait;
    void *write_wait;
    
    // State flags
    int stopped;
    
    void *driver_data; // Private driver data
};

// API
void tty_init(void);
struct tty *tty_alloc(struct tty_driver *driver, int idx);
void tty_free(struct tty *tty);
void tty_register_device(struct tty *tty, char *name);

// File Operations (to be called by VFS wrapper)
int tty_open(struct tty *tty);
void tty_close(struct tty *tty);
int tty_read(struct tty *tty, char *buf, int len);
int tty_write(struct tty *tty, const char *buf, int len);
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg);
int tty_poll(struct tty *tty, void *waiter);

// Input processing (called by driver interrupt/worker)
void tty_flip_buffer_push(struct tty *tty, char c);

// Helper
void tty_default_termios(struct termios *t);

#endif
