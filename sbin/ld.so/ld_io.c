/*
 * ld_io.c - raw system calls and minimal output helpers for the dynamic
 * linker.
 *
 * The dynamic linker can't call libc - libc isn't loaded yet.  We
 * hand-roll the system calls we need and a few formatting helpers.
 * The raw entry sequence is the architecture's: `int $0x80` with the
 * arguments on the stack for i386, `syscall` with the arguments in
 * registers for amd64.  Both report failure to their callers as a
 * negative errno.
 */

#include "ld.h"

/* Set from envp scan in ld_main.c.  When zero (default) the verbose
 * loader trace is suppressed entirely. */
int ld_debug = 0;

#ifdef LD_ARCH_AMD64

/*
 * amd64 native system call (docs/specs/abi-amd64.md, section 3): number
 * in %rax, arguments in %rdi %rsi %rdx %r10 %r8 %r9.  The instruction
 * overwrites %rcx and %r11; a second result may come back in %rdx.  An
 * error is the carry flag with the positive errno in %rax, which is
 * negated here so every caller sees the same -errno the i386 path
 * returns.
 */
static long ld_syscall6(int nr, ld_addr a, ld_addr b, ld_addr c,
                        ld_addr d, ld_addr e, ld_addr f) {
    long ret;
    register ld_addr r10 __asm__("r10") = d;
    register ld_addr r8  __asm__("r8")  = e;
    register ld_addr r9  __asm__("r9")  = f;
    __asm__ volatile (
        "syscall\n\t"
        "jnc  1f\n\t"
        "negq %%rax\n"
        "1:"
        : "=a"(ret), "+d"(c)
        : "0"((long)nr), "D"(a), "S"(b), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc"
    );
    return ret;
}

static long ld_syscall3(int nr, ld_addr a, ld_addr b, ld_addr c) {
    return ld_syscall6(nr, a, b, c, 0, 0, 0);
}

static long ld_syscall1(int nr, ld_addr a) {
    return ld_syscall6(nr, a, 0, 0, 0, 0, 0);
}

#else /* LD_ARCH_I386 */

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

/* Signal safety for the %esp-swap wrappers.  While %esp points at the
 * arg block during int $0x80, a signal delivered on syscall return has
 * the kernel push its sigframe just BELOW %esp.  The worst case is the
 * SA_SIGINFO frame (siginfo_t + a full ucontext whose mcontext carries
 * a 512-byte fpstate), a bit under 1 KiB.  Put the arg block at the top
 * of a padded buffer so that sigframe lands in the pad instead of over
 * the caller's live locals / return address.  1.5 KiB covers it with
 * margin; the buffer is a transient stack local, and these wrappers do
 * not recurse. */
#define LD_SYSCALL_PAD_WORDS 384        /* 1536 bytes below the arg block */

static long ld_syscall3(int nr, ld_u32 a, ld_u32 b, ld_u32 c) {
    volatile ld_u32 buf[LD_SYSCALL_PAD_WORDS + 4];
    volatile ld_u32 *stk = &buf[LD_SYSCALL_PAD_WORDS];
    stk[0] = 0 /*dummy*/; stk[1] = a; stk[2] = b; stk[3] = c;
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
    volatile ld_u32 buf[LD_SYSCALL_PAD_WORDS + 2];
    volatile ld_u32 *stk = &buf[LD_SYSCALL_PAD_WORDS];
    stk[0] = 0; stk[1] = a;
    long ret, saved;
    __asm__ volatile (
        "movl %%esp, %1\n\t"
        "movl %2, %%esp\n\t"
        "int  $0x80\n\t"
        "movl %1, %%esp\n\t"
        : "=a"(ret), "=&r"(saved)
        : "r"(stk), "0"(nr)
        : "memory", "cc",
          /* See ld_syscall3: the kernel's return path writes %edx. */
          "edx"
    );
    return ret;
}

static long ld_syscall6(int nr, ld_u32 a, ld_u32 b, ld_u32 c,
                        ld_u32 d, ld_u32 e, ld_u32 f) {
    volatile ld_u32 buf[LD_SYSCALL_PAD_WORDS + 7];
    volatile ld_u32 *stk = &buf[LD_SYSCALL_PAD_WORDS];
    stk[0] = 0; stk[1] = a; stk[2] = b; stk[3] = c;
    stk[4] = d; stk[5] = e; stk[6] = f;
    long ret, saved;
    __asm__ volatile (
        "movl %%esp, %1\n\t"
        "movl %2, %%esp\n\t"
        "int  $0x80\n\t"
        "movl %1, %%esp\n\t"
        : "=a"(ret), "=&r"(saved)
        : "r"(stk), "0"(nr)
        : "memory", "cc",
          /* See ld_syscall3: the kernel's return path writes %edx. */
          "edx"
    );
    return ret;
}

#endif /* LD_ARCH_* */

static ld_size ld_strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (ld_size)(p - s);
}

void ld_write(int fd, const char *buf, ld_size len) {
    if (len == 0) return;
    ld_syscall3(SYS_write, (ld_addr)fd, (ld_addr)(unsigned long)buf, (ld_addr)len);
}

void ld_puts(const char *s) {
    ld_write(2, s, ld_strlen(s));
}

/* "0x" and every hex digit of an address: 8 digits on i386, 16 on amd64. */
#define LD_HEX_DIGITS ((int)(sizeof(ld_addr) * 2))

void ld_putx(ld_addr v) {
    char buf[2 + sizeof(ld_addr) * 2 + 1];
    static const char hex[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < LD_HEX_DIGITS; i++) {
        buf[2 + i] = hex[(v >> ((LD_HEX_DIGITS - 1 - i) * 4)) & 0xf];
    }
    buf[2 + LD_HEX_DIGITS] = '\0';
    ld_write(2, buf, 2 + LD_HEX_DIGITS);
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

void ld_exit(int status) {
    ld_syscall1(SYS_exit, (ld_addr)status);
    for (;;) { /* unreachable */ }
}

void ld_die(const char *msg) {
    ld_puts(LD_SELF_NAME ": fatal: ");
    ld_puts(msg);
    ld_puts("\n");
    ld_exit(127);
}

int ld_open(const char *path, int flags) {
    /* sys_open(path, flags, mode) - pass 0 for mode since
     * we never create files. */
    return (int)ld_syscall3(SYS_open, (ld_addr)(unsigned long)path,
                            (ld_addr)flags, 0);
}

int ld_close(int fd) {
    return (int)ld_syscall1(SYS_close, (ld_addr)fd);
}

long ld_read(int fd, void *buf, ld_size n) {
    return ld_syscall3(SYS_read, (ld_addr)fd, (ld_addr)(unsigned long)buf, (ld_addr)n);
}

long ld_getdents(int fd, void *buf, ld_size n) {
    return ld_syscall3(SYS_getdents, (ld_addr)fd,
                       (ld_addr)(unsigned long)buf, (ld_addr)n);
}

long ld_lseek(int fd, long off, int whence) {
#ifdef LD_ARCH_AMD64
    /* sys_lseek(fd, off_lo, off_hi, whence): the offset travels as two
     * 32-bit halves on every architecture. */
    return ld_syscall6(SYS_lseek, (ld_addr)fd, (ld_addr)(ld_u32)off,
                       (ld_addr)((ld_u64)off >> 32), (ld_addr)whence, 0, 0);
#else
    return ld_syscall3(SYS_lseek, (ld_addr)fd, (ld_addr)off, (ld_addr)whence);
#endif
}

/* The last argument is the file offset in pages, on both architectures. */
void *ld_mmap(void *addr, ld_size len, int prot, int flags,
              int fd, ld_addr page_off) {
    long r = ld_syscall6(SYS_mmap, (ld_addr)(unsigned long)addr,
                         (ld_addr)len, (ld_addr)prot, (ld_addr)flags,
                         (ld_addr)fd, page_off);
    return (void *)r;
}

long ld_munmap(void *addr, ld_size len) {
    return ld_syscall3(SYS_munmap, (ld_addr)(unsigned long)addr,
                       (ld_addr)len, 0);
}

long ld_mprotect(void *addr, ld_size len, int prot) {
    return ld_syscall3(SYS_mprotect, (ld_addr)(unsigned long)addr,
                       (ld_addr)len, (ld_addr)prot);
}

int ld_sys_set_gsbase(ld_addr base) {
    return (int)ld_syscall1(SYS_set_gsbase, base);
}

long ld_futex(int *uaddr, int op, int val) {
    /* sys_futex(uaddr, op, val, timeout, uaddr2, val3) */
    return ld_syscall6(SYS_futex, (ld_addr)(unsigned long)uaddr,
                       (ld_addr)op, (ld_addr)val, 0, 0, 0);
}

int ld_thr_self(void) {
    return (int)ld_syscall1(SYS_thr_self, 0);
}
