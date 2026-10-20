#ifndef _LIBC_TLS_LOCAL_H
#define _LIBC_TLS_LOCAL_H

/*
 * Thread-local storage for statically linked programs (src/tls_static.c).
 * Internal to libc.
 */

/* Set up the initial thread's TLS from the executable's PT_TLS segment,
 * found through the auxiliary vector that follows envp.  Returns at once
 * when the dynamic linker has done it, and does its work only once. */
void __libc_static_tls_init(char **envp);

/* A TLS block for a new thread (libpthread), or NULL without TLS. */
void *__libc_alloc_tls(void);

#endif
