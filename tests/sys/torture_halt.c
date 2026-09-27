/*
 * torture_halt.c -- reboot(RB_HALT_SYSTEM) halts; it does not reset.
 *
 * The kernel sent RB_HALT_SYSTEM down the RB_AUTOBOOT path, so "halt"
 * reset the machine.  Run as init by tests/sys/run-torture-halt.sh, which
 * boots QEMU with -no-reboot (a reset makes QEMU exit) and checks that
 * QEMU is still running, halted, after this program asks for a halt.
 */
#include <stdio.h>
#include <sys/reboot.h>
#include <unistd.h>

int main(void) {
    printf("torture_halt: halting\n");
    fflush(stdout);
    sync();
    reboot(RB_HALT_SYSTEM);
    printf("torture_halt: reboot() returned\n");
    return 1;
}
