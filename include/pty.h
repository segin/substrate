/*
 * <pty.h> — pseudo-terminal helpers (Linux/glibc placement).
 *
 * Substrate implements openpty + login_tty + forkpty in libc;
 * this header gives them a declaration so callers don't fall back
 * to implicit-K&R prototypes.
 */

#ifndef _PTY_H
#define _PTY_H

#include <sys/types.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

struct winsize;

int openpty(int *amaster, int *aslave, char *name,
            const struct termios *termp, const struct winsize *winp);
int login_tty(int fd);
int forkpty(int *amaster, char *name,
            const struct termios *termp, const struct winsize *winp);

#ifdef __cplusplus
}
#endif

#endif
