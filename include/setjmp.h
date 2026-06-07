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

/* sigsetjmp MUST be a macro, not a function: setjmp() saves the context of
 * whoever *calls* it, so it has to run directly in the caller's frame.  A
 * function-wrapped sigsetjmp would have setjmp() record the wrapper's frame,
 * which is dead by the time siglongjmp() restores it — corrupting %ebp/%esp
 * and crashing.  __sigjmp_save() only stashes the signal mask; the in-place
 * setjmp() does the real context save. */
void __sigjmp_save(sigjmp_buf env, int savemask);
#define sigsetjmp(env, savemask) \
    (__sigjmp_save((env), (savemask)), setjmp((env)[0].__env))
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */
