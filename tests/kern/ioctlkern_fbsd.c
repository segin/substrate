/*
 * ioctlkern_fbsd — the FreeBSD-personality version of ioctlkern: a tty
 * ioctl's argument from a FreeBSD program is never used as a kernel
 * buffer.
 *
 * freebsd_sys_ioctl (FreeBSD and NetBSD programs) passed the program's
 * argument through perso_tty_ioctl to tty_ioctl, which takes anything at
 * or above KERN_BASE to be a kernel buffer: TIOCGWINSZ wrote the window
 * size to whatever kernel address the program named.
 *
 * No FreeBSD libc is needed: this is freestanding, makes its system
 * calls with int $0x80 in FreeBSD's convention (arguments on the stack,
 * the error flag in CF) and is branded with FreeBSD's ELF OSABI (9):
 *
 *      make ioctlkern_fbsd
 *      ./ioctlkern_fbsd 0xc029d0c0 < /dev/console    # kernel_hostname
 *
 * It prints "ioctlkern_fbsd: PASS" if the call fails with EFAULT.  The
 * caller checks with hostname(1) afterwards that the buffer is intact.
 */
#define FBSD_SYS_exit   1
#define FBSD_SYS_write  4
#define FBSD_SYS_ioctl  54
#define FBSD_TIOCGWINSZ 0x40087468UL   /* _IOR('t', 104, struct winsize) */
#define FBSD_EFAULT     14

/* A FreeBSD i386 system call: returns the result, or -errno when CF is
 * set. */
static long fbsd_syscall3(long nr, long a, long b, long c) {
    long ret;
    unsigned char cf;
    __asm__ volatile(
        "pushl %[c]\n\tpushl %[b]\n\tpushl %[a]\n\tpushl $0\n\t"
        "int $0x80\n\t"
        "setc %[cf]\n\t"
        "addl $16, %%esp"
        : "=a"(ret), [cf] "=q"(cf)
        : "0"(nr), [a] "r"(a), [b] "r"(b), [c] "r"(c)
        : "memory", "cc", "edx", "ecx");
    return cf ? -ret : ret;
}

static void say(const char *s) {
    long n = 0;
    while (s[n]) n++;
    fbsd_syscall3(FBSD_SYS_write, 1, (long)s, n);
}

static unsigned long parse_hex(const char *s) {
    unsigned long v = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    for (; *s; s++) {
        int d = (*s >= '0' && *s <= '9') ? *s - '0' :
                (*s >= 'a' && *s <= 'f') ? *s - 'a' + 10 :
                (*s >= 'A' && *s <= 'F') ? *s - 'A' + 10 : -1;
        if (d < 0) break;
        v = v * 16 + (unsigned long)d;
    }
    return v;
}

__attribute__((used)) static void cmain(long argc, char **argv) {
    int ok = 0;
    if (argc >= 2) {
        long r = fbsd_syscall3(FBSD_SYS_ioctl, 0, (long)FBSD_TIOCGWINSZ,
                               (long)parse_hex(argv[1]));
        ok = (r == -FBSD_EFAULT);
        say(ok ? "ok    TIOCGWINSZ into kernel memory: EFAULT\n"
               : "FAIL  TIOCGWINSZ into kernel memory: EFAULT\n");
    } else {
        say("usage: ioctlkern_fbsd <address of kernel_hostname>\n");
    }
    say(ok ? "ioctlkern_fbsd: PASS\n" : "ioctlkern_fbsd: FAIL\n");
    fbsd_syscall3(FBSD_SYS_exit, ok ? 0 : 1, 0, 0);
    for (;;) { }
}

/* argc, then argv[], on the initial stack. */
__asm__(".globl _start\n_start:\n\t"
        "movl (%esp), %eax\n\t"
        "leal 4(%esp), %ecx\n\t"
        "andl $-16, %esp\n\t"
        "pushl %ecx\n\tpushl %eax\n\t"
        "call cmain\n");
