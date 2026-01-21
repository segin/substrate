#include "../kern/console.h"
#include "../drivers/input/ps2.h"
#include "../arch/x86-common/include/io.h"
#include <stdio.h>

void run_ps2_tests(void) {
    kprint("\n=== RUNNING PS/2 TESTS ===\n");
    
    /* 
     * Test 1: Check Status Register
     * Read from Command/Status port (0x64)
     */
    uint8_t status = inb(PS2_COMMAND_PORT);
    char buf[64];
    sprintf(buf, "PS/2 Status Register: 0x%02X\n", status);
    kprint(buf);
    
    /* 
     * Bit 2 (System Flag) should be checked.
     * In real hardware, this is set after successful POST.
     * In QEMU, it might depend on initialization sequence.
     */
    if (status & PS2_STATUS_SYSTEM) {
         kprint("PASS: System Flag set (Controller Initialized)\n");
    } else {
         kprint("INFO: System Flag not set (Normal for some QEMU configs or cold boot)\n");
    }
    
    /*
     * Test 2: Check input buffer status
     * Should be empty (0) if we are not typing furiously
     */
    if (status & PS2_STATUS_INPUT_BUFFER_FULL) {
         kprint("WARN: Input buffer full (CPU to Controller)\n");
    } else {
         kprint("PASS: Input buffer clear (Ready for commands)\n");
    }

    kprint("=== PS/2 TESTS COMPLETE ===\n");
}
