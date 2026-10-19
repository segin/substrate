/*
 * vm86guest — what the VM86 monitor does with a guest it cannot carry on
 * with, and with a guest that asks for privileged flags.
 *
 *   - An instruction the monitor does not emulate (INT3) ends the guest
 *     with SIGILL.  It used to return without advancing, so the guest
 *     trapped on the same instruction forever.
 *   - A PUSHF whose SS:SP is not mapped ends the guest with SIGSEGV.  The
 *     monitor used to store through the guest's stack pointer in ring 0
 *     with no fault recovery.
 *   - A guest entered with IOPL 3 and NT in its flags gets neither: PUSHF
 *     still traps to the monitor, and the flags it pushes show IOPL 0 and
 *     NT clear.  With IOPL 3, CLI, STI, INT n and PUSHF would run
 *     natively instead of being emulated.
 *
 * Each guest runs in a child; the parent checks how the child ended.
 * Substrate i386 only (native SYS_VM86).
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/vm86.h>
#include <sys/wait.h>
#include <arch/i386/syscall.h>

#define GUEST_SIZE   0x10000UL

static int failed;
static volatile uint8_t *guest;
static uint16_t seg;                /* the guest's CS = SS */

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

/* Run `code` as a guest at seg:0000 in a child; return its wait status,
 * or -1 if it had not ended after a few seconds (and was killed). */
static int run_guest(const uint8_t *code, size_t len, uint16_t ss, uint16_t sp,
                     long eflags) {
    memcpy((void *)guest, code, len);
    pid_t pid = fork();
    if (pid == 0) {
        struct vm86_struct info;
        memset(&info, 0, sizeof(info));
        info.regs.cs = seg;
        info.regs.eip = 0;
        info.regs.ss = ss;
        info.regs.esp = sp;
        info.regs.ds = info.regs.es = seg;
        info.regs.eflags = eflags;
        syscall(SYS_VM86, (long)&info);
        _exit(100);                         /* never comes back */
    }
    for (int i = 0; i < 50; i++) {
        int st;
        if (waitpid(pid, &st, WNOHANG) == pid) return st;
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    return -1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Shared, so the parent sees what the guest pushed.  Not MAP_FIXED:
     * the kernel places low anonymous mappings below 1 MiB by itself. */
    void *m = mmap(NULL, GUEST_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    int low = m != MAP_FAILED && (uintptr_t)m + GUEST_SIZE <= 0x100000 &&
              ((uintptr_t)m & 0xF) == 0;
    check(low, "map the guest's memory below 1 MiB");
    if (!low) return 1;
    guest = m;
    seg = (uint16_t)((uintptr_t)m >> 4);

    /* INT3 is not emulated: the guest ends with SIGILL. */
    static const uint8_t int3[] = { 0xCC };
    int st = run_guest(int3, sizeof(int3), seg, 0xFF00, 0x0202);
    check(st != -1, "unemulated INT3: the guest ends");
    check(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGILL,
          "unemulated INT3: by SIGILL");

    /* PUSHF onto an unmapped stack (SS:SP = 9000:0100, past the guest's
     * memory): SIGSEGV. */
    static const uint8_t pushf[] = { 0x9C, 0xCC };
    st = run_guest(pushf, sizeof(pushf), 0x9000, 0x0100, 0x0202);
    check(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV,
          "PUSHF onto an unmapped stack: SIGSEGV");

    /* Ask for IOPL 3 and NT; PUSHF must still trap and push neither. */
    volatile uint16_t *top = (volatile uint16_t *)(guest + 0xFF00 - 2);
    *top = 0xFFFF;
    st = run_guest(pushf, sizeof(pushf), seg, 0xFF00, 0x0202 | 0x3000 | 0x4000);
    check(st != -1 && WIFSIGNALED(st) && WTERMSIG(st) == SIGILL,
          "IOPL 3 requested: PUSHF emulated, INT3 still trapped");
    check((*top & 0x7000) == 0, "IOPL 3 requested: pushed flags have IOPL 0, NT 0");

    printf("%s\n", failed ? "vm86guest: FAIL" : "vm86guest: PASS");
    return failed;
}
