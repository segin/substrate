/*
 * linuxptr — the Linux personality's system calls copy user memory in
 * and out instead of using the program's pointers directly.
 *
 * Twenty-odd entries of the Linux system call table (gettimeofday,
 * times, wait4, sigaction, sigprocmask, readlink, getdents, ...) pointed
 * at the kernel-internal kern_* functions, which take kernel pointers:
 * the program's pointer was dereferenced in ring 0 as is, so a kernel
 * address read or wrote kernel memory.  They now go through the same
 * sys_* entry points as native programs.
 *
 * The calls must still work, and gettimeofday aimed at the kernel's
 * hostname buffer must fail EFAULT and leave it alone.  The address is
 * build-specific, so it is passed in:
 *
 *      nm sys/kernel.multiboot | grep ' D kernel_hostname'
 *      ./linuxptr.linux 0xc029d0c0
 *
 * A static i386 Linux program (make linuxptr.linux), run under the Linux
 * personality.  System calls are made with syscall() so the C library's
 * own variants (vDSO, newer calls) cannot stand in for the ones tested.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <sys/wait.h>

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

    long tv[2] = { 0, 0 };
    check(syscall(SYS_gettimeofday, tv, NULL) == 0 && tv[0] > 0, "gettimeofday");

    long tms[4];
    check(syscall(SYS_times, tms) != -1, "times");

    long t = 0;
    check(syscall(SYS_time, &t) > 0 && t > 0, "time");

    char link[64];
    long n = syscall(SYS_readlink, "/proc/self/exe", link, sizeof(link) - 1);
    check(n > 0, "readlink");

    unsigned long oldmask[2] = { 0, 0 };
    check(syscall(SYS_sigprocmask, SIG_BLOCK, NULL, oldmask) == 0, "sigprocmask");

    pid_t pid = fork();
    if (pid == 0) _exit(7);
    int st = 0;
    check(syscall(SYS_wait4, pid, &st, 0, NULL) == pid && WIFEXITED(st) &&
          WEXITSTATUS(st) == 7, "wait4");

    struct utsname before, after;
    uname(&before);
    errno = 0;
    long r = syscall(SYS_gettimeofday, (void *)kaddr, NULL);
    check(r == -1 && errno == EFAULT, "gettimeofday into kernel memory: EFAULT");
    uname(&after);
    check(strcmp(before.nodename, after.nodename) == 0, "kernel hostname unchanged");

    printf("%s\n", failed ? "linuxptr: FAIL" : "linuxptr: PASS");
    return failed;
}
