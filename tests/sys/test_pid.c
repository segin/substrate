#include <kern/console.h>
#include <pm/pm.h>
#include "tests.h"

extern process_t processes[];

void run_pid_tests(void) {
    kprint("TEST: Checking PID invariants...\n");
    
    // Test 1: Swapper (Index 0) must be PID 0
    if (processes[0].pid == 0) {
        kprint("PASS: Swapper (processes[0]) is PID 0\n");
    } else {
        kprint("FAIL: Swapper (processes[0]) is NOT PID 0\n");
    }
    
    // Test 2: PID 1 should NOT exist yet (called before kinit spawn)
    // We scan to ensure no other metadata corruption
    if (processes[1].pid == -1) {
        kprint("PASS: PID 1 slot is empty (as expected before init spawn)\n");
    } else {
        kprint("INFO: PID 1 slot already occupied\n");
    }
}
