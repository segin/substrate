/*
 * tidkern — the exit store set_tid_address arranges goes to user memory
 * only.
 *
 * When a thread exits, the kernel writes to the address it registered
 * with set_tid_address (or clone's CLONE_CHILD_CLEARTID) and wakes any
 * futex waiter there.  That address is the thread's to choose, and it
 * was stored with a raw ring-0 write after only a page-table lookup, so
 * a kernel address made it a write of 1 to arbitrary kernel memory.
 *
 * The test aims the store at the kernel's hostname buffer, whose first
 * bytes uname(2) reads back, and at the program's own read-only text;
 * neither may change, and the system must stay up.  The kernel address
 * is build-specific, so it is passed in:
 *
 *      nm sys/kernel.multiboot | grep ' D kernel_hostname'
 *      ./tidkern.linux 0xc029d0c0
 *
 * set_tid_address is a Linux system call, so this is a static i386 Linux
 * program (make tidkern.linux), run under the Linux personality.
 */
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

/* A child registers `addr` and exits, which makes the kernel store there. */
static int exit_with_tid_address(unsigned long addr) {
    pid_t pid = fork();
    if (pid == 0) {
        syscall(SYS_set_tid_address, addr);
        syscall(SYS_exit, 0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: %s <address of kernel_hostname>\n", argv[0]);
        return 2;
    }
    unsigned long kaddr = strtoul(argv[1], NULL, 0);

    struct utsname before, after;
    uname(&before);
    check(exit_with_tid_address(kaddr), "child with a kernel tid address exits");
    uname(&after);
    check(strcmp(before.nodename, after.nodename) == 0,
          "kernel hostname unchanged by the exit store");

    check(exit_with_tid_address((unsigned long)(void *)main),
          "child with a read-only tid address exits");
    check(*(volatile unsigned char *)(void *)main != 0x01 ||
          *((volatile unsigned char *)(void *)main + 1) != 0x00,
          "read-only text unchanged");

    printf("%s\n", failed ? "tidkern: FAIL" : "tidkern: PASS");
    return failed;
}
