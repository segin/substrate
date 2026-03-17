#ifndef _SETJMP_H
#define _SETJMP_H

/* i386 jmp_buf: ebx, esi, edi, ebp, esp, eip (6 registers) */
typedef int jmp_buf[6];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif /* _SETJMP_H */
