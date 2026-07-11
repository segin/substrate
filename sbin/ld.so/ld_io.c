/*
 * ld_io.c - minimal write/exit stubs for /sbin/ld.so.
 *
 * The dynamic linker can't call libc - libc isn't loaded yet.
 * We hand-roll the two syscalls we need and a few formatting
 * helpers.  Native syscall ABI is stack-based: number in %eax,
 * args at [esp+4..], dummy slot at [esp+0], int $0x80.
 */

#include "ld.h"

/* Set from envp scan in ld_main.c.  When zero (default) the verbose
 * loader trace is suppressed entirely. */
int ld_debug = 0;

/* Native syscall ABI: number in %eax, args at [esp+4..], dummy at
 * [esp+0].  Inline-asm pitfall: any "g"/"m"-constrained operand
 * may resolve to a stack slot, and explicit pushes shift %esp
 * before the operand is read - the kernel then sees garbage.
 * i386 also has only 7 GPRs total, so a 6-arg syscall plus the
 * number can exhaust the register pool.
 *
 * Robust pattern: build the kernel's arg block as a local array
 * at a stable address, swap %esp to point at that array for the
 * duration of int $0x80, restore %esp on return.  The kernel
 * uses its own stack so playing tricks with our %esp is safe.
 */

static long ld_syscall3(int nr, ld_u32 a, ld_u32 b, ld_u32 c) {
    ld_u32 stk[4] = { 0 /*dummy*/, a, b, c };
    long ret, saved;
    __asm__ volatile (
        "movl %%esp, %1\n\t"
        "movl %2, %%esp\n\t"
        "int  $0x80\n\t"
        "movl %1, %%esp\n\t"
        : "=a"(ret), "=&r"(saved)
        : "r"(stk), "0"(nr)
        : "memory", "cc",
          /* Substrate's syscall return path stuffs the high half of
           * the 64-bit return into %edx - any caller that doesn't
           * sign-extend (cdq) or re-load edx will pick up the
           * clobbered value.  Without the explicit clobber GCC
           * happily reuses edx for `stk` across calls, and the
           * second int 0x80 swaps esp to whatever the kernel
           * stuffed there.  Spell it out so GCC reloads.  */
          "edx"
    );
    return ret;
}

static long ld_syscall1(int nr, ld_u32 a) {
    ld_u32 stk[2] = { 0, a };
    long ret, saved;
    __asm__ volatile (
        "movl %%esp, %1\n\t"
        "movl %2, %%esp\n\t"
        "int  $0x80\n\t"
        "movl %1, %%esp\n\t"
        : "=a"(ret), "=&r"(saved)
        : "r"(stk), "0"(nr)
        : "memory", "cc",
          /* Substrate's syscall return path stuffs the high half of
           * the 64-bit return into %edx - any caller that doesn't
           * sign-extend (cdq) or re-load edx will pick up the
           * clobbered value.  Without the explicit clobber GCC
           * happily reuses edx for `stk` across calls, and the
           * second int 0x80 swaps esp to whatever the kernel
           * stuffed there.  Spell it out so GCC reloads.  */
          "edx"
    );
    return ret;
}

static long ld_syscall6(int nr, ld_u32 a, ld_u32 b, ld_u32 c,
                        ld_u32 d, ld_u32 e, ld_u32 f) {
    ld_u32 stk[7] = { 0, a, b, c, d, e, f };
    long ret, saved;
    __asm__ volatile (
        "movl %%esp, %1\n\t"
        "movl %2, %%esp\n\t"
        "int  $0x80\n\t"
        "movl %1, %%esp\n\t"
        : "=a"(ret), "=&r"(saved)
        : "r"(stk), "0"(nr)
        : "memory", "cc",
          /* Substrate's syscall return path stuffs the high half of
           * the 64-bit return into %edx - any caller that doesn't
           * sign-extend (cdq) or re-load edx will pick up the
           * clobbered value.  Without the explicit clobber GCC
           * happily reuses edx for `stk` across calls, and the
           * second int 0x80 swaps esp to whatever the kernel
           * stuffed there.  Spell it out so GCC reloads.  */
          "edx"
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

int ld_open(const char *path, int flags) {
    /* sys_open(path, flags, mode) - pass 0 for mode since
     * we never create files. */
    return (int)ld_syscall3(SYS_open, (ld_u32)(unsigned long)path,
                            (ld_u32)flags, 0);
}

int ld_close(int fd) {
    return (int)ld_syscall1(SYS_close, (ld_u32)fd);
}

long ld_read(int fd, void *buf, ld_size n) {
    return ld_syscall3(SYS_read, (ld_u32)fd, (ld_u32)(unsigned long)buf, (ld_u32)n);
}

long ld_getdents(int fd, void *buf, ld_size n) {
    return ld_syscall3(SYS_getdents, (ld_u32)fd,
                       (ld_u32)(unsigned long)buf, (ld_u32)n);
}

long ld_lseek(int fd, long off, int whence) {
    return ld_syscall3(SYS_lseek, (ld_u32)fd, (ld_u32)off, (ld_u32)whence);
}

void *ld_mmap(void *addr, ld_size len, int prot, int flags,
              int fd, ld_u32 page_off) {
    long r = ld_syscall6(SYS_mmap, (ld_u32)(unsigned long)addr,
                         (ld_u32)len, (ld_u32)prot, (ld_u32)flags,
                         (ld_u32)fd, page_off);
    return (void *)r;
}

int ld_sys_set_gsbase(ld_u32 base) {
    return (int)ld_syscall1(SYS_set_gsbase, base);
}

long ld_futex(int *uaddr, int op, int val) {
    /* sys_futex(uaddr, op, val, timeout, uaddr2, val3) */
    return ld_syscall6(SYS_futex, (ld_u32)(unsigned long)uaddr,
                       (ld_u32)op, (ld_u32)val, 0, 0, 0);
}

int ld_thr_self(void) {
    return (int)ld_syscall1(SYS_thr_self, 0);
}
