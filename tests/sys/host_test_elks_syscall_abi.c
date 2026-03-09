#include <arch/i386/syscall_abi.h>
#include <arch/i386/idt.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct personality p;
    registers_t regs;
    uint32_t args[8];

    memset(&p, 0, sizeof(p));
    memset(&regs, 0, sizeof(regs));
    memset(args, 0xA5, sizeof(args));

    p.id = PERS_ELKS;
    regs.ebx = 1;
    regs.ecx = 2;
    regs.edx = 3;
    regs.edi = 4;
    regs.esi = 5;
    regs.ebp = 6;

    i386_extract_syscall_args(&p, &regs, args);

    if (args[0] != 1 || args[1] != 2 || args[2] != 3 || args[3] != 4 || args[4] != 5) {
        fprintf(stderr, "FAIL: ELKS register ABI ordering wrong\n");
        return 1;
    }
    if (args[5] != 0 || args[6] != 0 || args[7] != 0) {
        fprintf(stderr, "FAIL: ELKS ABI leaked unexpected extra arguments\n");
        return 1;
    }
    if (IDT_FLAG_USER_INT_GATE != 0xEE) {
        fprintf(stderr, "FAIL: user syscall gate flags changed\n");
        return 1;
    }

    puts("host_test_elks_syscall_abi: ok");
    return 0;
}
