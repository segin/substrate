#ifndef _THR_H
#define _THR_H

#include <stdint.h>

// FreeBSD-like thr_param structure
struct thr_param {
    void    (*start_func)(void *);
    void    *arg;
    void    *stack_base;
    size_t  stack_size;
    void    *tls_base;
    size_t  tls_size;
    long    *child_tid; // Address to store TID
    long    *parent_tid;
    int     flags;
};

// Syscall number (arbitrary choice for Substrate native, or matching FreeBSD 455)
#define SYS_THR_NEW 455
#define SYS_THR_EXIT 431

#endif
