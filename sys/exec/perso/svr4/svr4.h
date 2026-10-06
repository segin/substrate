/*
 * svr4.h - the System V Release 4 personality's trap and signal hooks
 * (svr4_calls.c), for the struct personality in perso_svr4.c.
 */
#ifndef _EXEC_PERSO_SVR4_SVR4_H
#define _EXEC_PERSO_SVR4_SVR4_H

#include <stdint.h>

/* What the ELF loader knows a Release 4 program by: its interpreter, or,
 * for a static one, the personality root it was found under. */
#define SVR4_INTERP_PATH  "/usr/lib/libc.so.1"
#define SVR4_ROOT_PREFIX  "/perso/svr4/"

/* Emulate the `lcall` a #GP/#NP was raised by; 0 if it was not one. */
int svr4_handle_trap(void *regs);

/* Enter a signal handler: handler(signo, siginfo, ucontext). */
void svr4_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                  void *regs);

#endif /* _EXEC_PERSO_SVR4_SVR4_H */
