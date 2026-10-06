#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <exec/perso/personality.h>
#include <exec/perso/svr4/svr4.h>
#include <exec/perso/compat.h>
#include <arch/i386/syscall.h>

/* The System V calls are svr4/svr4_calls.c, which needs the whole kernel
 * behind it; this test links only the two struct personality, so their
 * hooks are stood in for here. */
int svr3_handle_trap(void *regs) {
    (void)regs;
    return 0;
}

void svr3_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                  void *regs) {
    (void)handler; (void)sig; (void)mask; (void)flags; (void)regs;
}

/* As for SVR4 below: calls arrive as `lcall $7,$0` faults, so there are
 * hooks and a root and no syscall table. */
bool test_svr3_personality_table(void) {
    if (personality_svr3.id != PERS_SVR3) return false;
    if (personality_svr3.syscall_table != NULL) return false;
    if (personality_svr3.syscall_count != 0) return false;
    if (personality_svr3.handle_trap != svr3_handle_trap) return false;
    if (personality_svr3.sendsig != svr3_sendsig) return false;
    if (strcmp(personality_svr3.path_prefix, "/perso/svr3") != 0) return false;
    if (!personality_svr3.native_dev) return false;
    return true;
}

int svr4_handle_trap(void *regs) {
    (void)regs;
    return 0;
}

void svr4_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                  void *regs) {
    (void)handler; (void)sig; (void)mask; (void)flags; (void)regs;
}

/* An SVR4 program never issues `int $0x80`: its calls are `lcall $7,$0`
 * faults taken by the handle_trap hook, so the personality has hooks and
 * a root, and no syscall table. */
bool test_svr4_personality_table(void) {
    if (personality_svr4.id != PERS_SVR4) return false;
    if (personality_svr4.syscall_table != NULL) return false;
    if (personality_svr4.syscall_count != 0) return false;
    if (personality_svr4.handle_trap != svr4_handle_trap) return false;
    if (personality_svr4.sendsig != svr4_sendsig) return false;
    if (strcmp(personality_svr4.path_prefix, "/perso/svr4") != 0) return false;
    /* Its /dev is the kernel's: a vendor's tree has none worth listing. */
    if (!personality_svr4.native_dev) return false;
    return true;
}