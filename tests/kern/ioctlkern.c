/*
 * ioctlkern — a tty ioctl's argument is never taken as a kernel buffer
 * when it comes from a user program.
 *
 * The tty layer treats an argument at or above KERN_BASE as one of the
 * kernel's own buffers (the personality layers pass those) and uses it
 * without copyin/copyout.  Native ioctl(2) refused such arguments, but
 * the Linux, FreeBSD/NetBSD, SunOS, SysV and SVR4 entry points handed
 * the user's argument straight through, so TIOCGWINSZ wrote to, and
 * TCSETS read from, any kernel address the program named.
 *
 * The test aims TIOCGWINSZ at the kernel's hostname buffer, which
 * uname(2) reads back: the call must fail with EFAULT and the hostname
 * must not change.  The address is build-specific, so it is passed in:
 *
 *      nm sys/kernel.multiboot | grep ' D kernel_hostname'
 *      ./ioctlkern.linux 0xc029d0c0
 *
 * A static i386 Linux program (make ioctlkern.linux), run under the
 * Linux personality with stdin on the console.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <address of kernel_hostname>\n", argv[0]);
        return 2;
    }
    unsigned long kaddr = strtoul(argv[1], NULL, 0);

    struct winsize ws;
    check(ioctl(0, TIOCGWINSZ, &ws) == 0, "TIOCGWINSZ on the console works");

    struct utsname before, after;
    uname(&before);
    errno = 0;
    int r = ioctl(0, TIOCGWINSZ, (void *)kaddr);
    check(r == -1 && errno == EFAULT, "TIOCGWINSZ into kernel memory: EFAULT");
    uname(&after);
    check(strcmp(before.nodename, after.nodename) == 0, "kernel hostname unchanged");

    printf("%s\n", failed ? "ioctlkern: FAIL" : "ioctlkern: PASS");
    return failed;
}
