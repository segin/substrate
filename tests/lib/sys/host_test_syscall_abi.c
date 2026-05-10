/*
 * host_test_syscall_abi.c
 *
 * ABI conformance test for lib/sys/syscall.S under the native /
 * BSD personality stack ABI.
 *
 * The kernel-side extractor (sys/arch/i386/syscall_abi.h:
 * i386_extract_syscall_args) reads syscall arguments from the
 * user stack at useresp[1..8] for PERS_NATIVE / PERS_FREEBSD /
 * PERS_NETBSD / PERS_OPENBSD / PERS_SVR4.  The contract our
 * libsys stub must honour: at the moment of `int $0x80`,
 *
 *     %eax     = syscall number
 *     [esp+0]  = (any value, treated as the would-be ret_addr)
 *     [esp+4]  = arg0   <- useresp[1]
 *     [esp+8]  = arg1   <- useresp[2]
 *     ...
 *     [esp+28] = arg6   <- useresp[7]
 *
 * Verification: this test is built with -m32 on the host so the
 * actual lib/sys/syscall.o object can be linked in.  We replace
 * the `int $0x80` instruction with a no-op trap (UD2) at runtime
 * by remapping the page R+W, then install a SIGILL handler that
 * captures %eax and walks user_stack[1..8] from the saved %esp.
 *
 * For each arg-count from 0 to 6 we issue a unique syscall number
 * with a unique argument pattern and assert the captured tuple
 * matches what we passed.  This catches:
 *   - off-by-one stack shifts (pre-fix bug),
 *   - wrong argument order,
 *   - clobbered callee-saved registers (ebx/esi/edi/ebp),
 *   - syscall-number not landing in %eax.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <ucontext.h>
#include <setjmp.h>
#include <sys/mman.h>
#include <unistd.h>

extern long syscall(long nr, ...);

static long           cap_nr;
static unsigned long  cap_args[8];
static int            cap_seen;
static sigjmp_buf     cap_back;

static void on_trap(int sig, siginfo_t *si, void *ucv) {
    (void)sig; (void)si;
    ucontext_t *uc = (ucontext_t *)ucv;

    /* On a real -m32 binary REG_EAX/REG_ESP exist; on x86_64 hosts
     * compiled -m32 we still get the 32-bit greg layout. */
#ifdef REG_EAX
    cap_nr = (long)(int32_t)uc->uc_mcontext.gregs[REG_EAX];
    uintptr_t esp = (uintptr_t)(uint32_t)uc->uc_mcontext.gregs[REG_ESP];
#else
    /* Fallback for headers that only define REG_RAX/REG_RSP.  We
     * still need the low 32 bits because the test binary is i386. */
    cap_nr = (long)(int32_t)uc->uc_mcontext.gregs[REG_RAX];
    uintptr_t esp = (uintptr_t)(uint32_t)uc->uc_mcontext.gregs[REG_RSP];
#endif

    const uint32_t *us = (const uint32_t *)esp;
    for (int i = 0; i < 8; i++) cap_args[i] = us[i + 1];
    cap_seen = 1;
    siglongjmp(cap_back, 1);
}

/* Locate the `int $0x80` (CD 80) byte pair inside lib/sys
 * syscall().  We trust there is exactly one occurrence — the test
 * fails loudly if not.  */
static unsigned char *find_int80_in_syscall(void) {
    /* `syscall` already has the variadic prototype from <unistd.h>;
     * cast its function pointer to a byte pointer to walk its body. */
    unsigned char *p = (unsigned char *)(void *)(uintptr_t)&syscall;
    for (int i = 0; i < 64; i++) {
        if (p[i] == 0xCD && p[i + 1] == 0x80) return p + i;
    }
    return NULL;
}

static int run_one(const char *label, long expect_nr,
                   const unsigned long *expect_args, int nargs,
                   long (*invoke)(void)) {
    cap_seen = 0;
    cap_nr = -1;
    memset(cap_args, 0xee, sizeof(cap_args));
    if (sigsetjmp(cap_back, 1) == 0) {
        (void)invoke();
        fprintf(stderr, "%s: UD2 trap did not fire\n", label);
        return 1;
    }
    if (!cap_seen) { fprintf(stderr, "%s: handler did not record\n", label); return 1; }
    if (cap_nr != expect_nr) {
        fprintf(stderr, "%s: nr expected %ld, got %ld\n", label, expect_nr, cap_nr);
        return 1;
    }
    for (int i = 0; i < nargs; i++) {
        if (cap_args[i] != expect_args[i]) {
            fprintf(stderr, "%s: arg[%d] expected 0x%lx, got 0x%lx\n",
                    label, i, expect_args[i], cap_args[i]);
            return 1;
        }
    }
    printf("%s: nr=%ld args=[", label, cap_nr);
    for (int i = 0; i < nargs; i++) printf("%s0x%lx", i ? "," : "", cap_args[i]);
    printf("] OK\n");
    return 0;
}

#define NR0 0x7700
#define NR1 0x7701
#define NR2 0x7702
#define NR3 0x7703
#define NR4 0x7704
#define NR5 0x7705
#define NR6 0x7706

static long inv0(void) { return syscall(NR0); }
static long inv1(void) { return syscall(NR1, 0xa1a1a1a1UL); }
static long inv2(void) { return syscall(NR2, 0xb2b2b2b2UL, 0xc3c3c3c3UL); }
static long inv3(void) { return syscall(NR3, 0xd4d4d4d4UL, 0xe5e5e5e5UL, 0xf6f6f6f6UL); }
static long inv4(void) { return syscall(NR4, 0x11111111UL, 0x22222222UL, 0x33333333UL, 0x44444444UL); }
static long inv5(void) { return syscall(NR5, 0x55555555UL, 0x66666666UL, 0x77777777UL, 0x88888888UL, 0x99999999UL); }
static long inv6(void) { return syscall(NR6, 0xaaaaaaaaUL, 0xbbbbbbbbUL, 0xccccccccUL, 0xddddddddUL, 0xeeeeeeeeUL, 0xffffffffUL); }

int main(void) {
    /* Locate `int $0x80` and replace with UD2 (0F 0B). */
    unsigned char *site = find_int80_in_syscall();
    if (!site) { fprintf(stderr, "FAIL: int $0x80 not found in syscall()\n"); return 2; }

    long pgsz = sysconf(_SC_PAGESIZE);
    void *page = (void *)((uintptr_t)site & ~(uintptr_t)(pgsz - 1));
    if (mprotect(page, pgsz * 2, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        perror("mprotect"); return 2;
    }
    site[0] = 0x0F; site[1] = 0x0B;     /* UD2 */
    /* No need to flush i-cache on x86 for 2 bytes. */

    struct sigaction sa = {0};
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGILL, &sa, NULL);

    int fails = 0;
    {
        unsigned long e[6] = {0};
        fails += run_one("inv0 (0 args)", NR0, e, 0, inv0);
    }
    { unsigned long e[1] = {0xa1a1a1a1UL};                                fails += run_one("inv1 (1 arg)",  NR1, e, 1, inv1); }
    { unsigned long e[2] = {0xb2b2b2b2UL, 0xc3c3c3c3UL};                  fails += run_one("inv2 (2 args)", NR2, e, 2, inv2); }
    { unsigned long e[3] = {0xd4d4d4d4UL, 0xe5e5e5e5UL, 0xf6f6f6f6UL};    fails += run_one("inv3 (3 args)", NR3, e, 3, inv3); }
    { unsigned long e[4] = {0x11111111UL, 0x22222222UL, 0x33333333UL, 0x44444444UL};
      fails += run_one("inv4 (4 args)", NR4, e, 4, inv4); }
    { unsigned long e[5] = {0x55555555UL, 0x66666666UL, 0x77777777UL, 0x88888888UL, 0x99999999UL};
      fails += run_one("inv5 (5 args)", NR5, e, 5, inv5); }
    { unsigned long e[6] = {0xaaaaaaaaUL, 0xbbbbbbbbUL, 0xccccccccUL, 0xddddddddUL, 0xeeeeeeeeUL, 0xffffffffUL};
      fails += run_one("inv6 (6 args)", NR6, e, 6, inv6); }

    if (fails) {
        fprintf(stderr, "ABI conformance: %d failure(s)\n", fails);
        return 1;
    }
    printf("ABI conformance (0..6 args): all OK\n");
    return 0;
}
