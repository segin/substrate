/*
 * ld_io.c — minimal write/exit stubs for /sbin/ld.so.
 *
 * The dynamic linker can't call libc — libc isn't loaded yet.
 * We hand-roll the two syscalls we need and a few formatting
 * helpers.  Native syscall ABI is stack-based: number in %eax,
 * args at [esp+4..], dummy slot at [esp+0], int $0x80.
 */

#include "ld.h"

static long ld_syscall3(int nr, ld_u32 a, ld_u32 b, ld_u32 c) {
    long ret;
    __asm__ volatile (
        "pushl %4\n\t"
        "pushl %3\n\t"
        "pushl %2\n\t"
        "pushl $0\n\t"          /* dummy ret slot */
        "int   $0x80\n\t"
        "addl  $16, %%esp\n\t"
        : "=a"(ret)
        : "0"(nr), "g"(a), "g"(b), "g"(c)
        : "memory", "cc"
    );
    return ret;
}

static long ld_syscall1(int nr, ld_u32 a) {
    long ret;
    __asm__ volatile (
        "pushl %2\n\t"
        "pushl $0\n\t"
        "int   $0x80\n\t"
        "addl  $8, %%esp\n\t"
        : "=a"(ret)
        : "0"(nr), "g"(a)
        : "memory", "cc"
    );
    return ret;
}

static ld_size ld_strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (ld_size)(p - s);
}

void ld_write(int fd, const char *buf, ld_size len) {
    if (len == 0) return;
    ld_syscall3(SYS_write, (ld_u32)fd, (ld_u32)(unsigned long)buf, (ld_u32)len);
}

void ld_puts(const char *s) {
    ld_write(2, s, ld_strlen(s));
}

void ld_putx(ld_u32 v) {
    char buf[11];
    static const char hex[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[2 + i] = hex[(v >> ((7 - i) * 4)) & 0xf];
    }
    buf[10] = '\0';
    ld_write(2, buf, 10);
}

void ld_putd(ld_u32 v) {
    char buf[12];
    int  n = 0;
    if (v == 0) { ld_write(2, "0", 1); return; }
    while (v > 0 && n < (int)sizeof(buf)) {
        buf[n++] = '0' + (char)(v % 10);
        v /= 10;
    }
    /* Reverse */
    char out[12];
    for (int i = 0; i < n; i++) out[i] = buf[n - 1 - i];
    ld_write(2, out, (ld_size)n);
}

void ld_die(const char *msg) {
    ld_puts("ld.so: fatal: ");
    ld_puts(msg);
    ld_puts("\n");
    ld_syscall1(SYS_exit, 127);
    for (;;) { /* unreachable */ }
}
