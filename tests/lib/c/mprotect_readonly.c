/* mprotect_readonly -- memory that may not be written cannot be written.
 *
 * A page is read-only in the page table both when it is waiting to be
 * copied (after fork) and when its mapping simply does not allow writing.
 * The kernel's copy-on-write path took every write to a present read-only
 * page for the first, so once a read-only page had been touched a write to
 * it went through: mprotect(PROT_READ) did nothing, a PROT_READ mapping
 * was writable, and so was a program's own text.  The general fault
 * handler let the rest through for any private mapping.
 *
 * Each case below writes where it must not and expects SIGSEGV, and checks
 * that what must still work -- copy-on-write itself, a page beside a
 * protected one, protection lifted again -- does.
 *
 *     i386-unknown-substrate-gcc -o mprotect_readonly mprotect_readonly.c
 *
 * Prints a line per check and "mprotect_readonly: PASS" or "FAIL"; the
 * exit status says the same.  Run on substrate (either kernel, either
 * userland).
 */
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static sigjmp_buf jb;
static int fails;

static void on_fault(int sig) {
    (void)sig;
    siglongjmp(jb, 1);
}

/* 1 if a write to *p faults. */
static int faults(volatile char *p) {
    if (sigsetjmp(jb, 1)) {
        return 1;
    }
    *p = 1;
    return 0;
}

static void check(const char *what, int ok) {
    printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        fails++;
    }
}

int main(void) {
    char *p;
    int fd, st;
    pid_t pid;

    signal(SIGSEGV, on_fault);
    signal(SIGBUS, on_fault);

    p = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check("anonymous RW page is writable", !faults(p));
    mprotect(p, 4096, PROT_READ);
    check("after mprotect(PROT_READ) a write faults", faults(p));
    check("  and it still reads", p[0] == 1);
    check("  the page beside it is still writable", !faults(p + 4096));
    mprotect(p, 4096, PROT_READ | PROT_WRITE);
    check("after mprotect(RW) it is writable again", !faults(p));

    p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check("a read of a PROT_READ page", p[0] == 0);
    check("  then a write to it faults", faults(p));

    fd = open("/bin/sh", O_RDONLY);
    p = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    check("a read of a read-only file mapping", p != MAP_FAILED && p[0] == 0x7f);
    check("  then a write to it faults", faults(p));

    check("a write to the program's own text faults",
          faults((volatile char *)(void *)main));

    /* Copy-on-write is not what was wrong, and still works. */
    p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    strcpy(p, "parent");
    pid = fork();
    if (pid == 0) {
        strcpy(p, "child");
        _exit(strcmp(p, "child") ? 1 : 0);
    }
    waitpid(pid, &st, 0);
    check("fork: the child writes its copy",
          WIFEXITED(st) && WEXITSTATUS(st) == 0);
    check("fork: the parent's is untouched", !strcmp(p, "parent"));
    check("fork: the parent can still write", !faults(p));

    /* A read-only page is still read-only in a child, where every page
     * is read-only in the page table. */
    mprotect(p, 4096, PROT_READ);
    pid = fork();
    if (pid == 0) {
        signal(SIGSEGV, SIG_DFL);
        *(volatile char *)p = 2;
        _exit(0);
    }
    waitpid(pid, &st, 0);
    check("fork: a read-only page is read-only in the child",
          WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV);

    printf("mprotect_readonly: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
