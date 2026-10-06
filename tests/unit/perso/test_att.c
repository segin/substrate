#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <exec/perso/personality.h>
#include <exec/perso/svr4/svr4.h>
#include <exec/perso/compat.h>
#include <arch/i386/syscall.h>

extern int sys_exit(int);
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);

bool test_svr3_personality_table(void) {
    if (personality_svr3.syscall_table[1] != &sys_exit) return false;
    if (personality_svr3.syscall_table[3] != &sys_read) return false;
    if (personality_svr3.syscall_table[18] == NULL) return false; // stat
    return true;
}

/* The SVR4 calls are svr4/svr4_calls.c, which needs the whole kernel behind
 * it; this test links only the struct personality, so its two hooks are
 * stood in for here. */
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
    return true;
}