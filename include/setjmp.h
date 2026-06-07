#ifndef _SETJMP_H
#define _SETJMP_H

/* i386 jmp_buf: ebx, esi, edi, ebp, esp, eip (6 registers) */
typedef int jmp_buf[6];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

/* sigjmp_buf: same shape as jmp_buf plus a signal-mask slot at the
 * tail.  sigsetjmp(env, 1) saves the current sigprocmask into env's
 * tail; siglongjmp restores it. */
typedef struct {
    jmp_buf      __env;
    int          __savemask;   /* 0 = don't save, 1 = saved below */
    unsigned int __mask;
} sigjmp_buf[1];

/* sigsetjmp is implemented in assembly (lib/c/arch/i386/setjmp.S): it saves
 * the CALLER's frame directly, the way setjmp does, so the saved context is
 * the caller's and survives until siglongjmp.  (A C wrapper that called
 * setjmp internally would save the wrapper's own — soon-dead — frame and
 * crash on siglongjmp.) */
int sigsetjmp(sigjmp_buf env, int savemask);
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */
