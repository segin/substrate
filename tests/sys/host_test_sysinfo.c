#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>

// Mock Data
unsigned long mock_free_pages = 100;
unsigned long mock_used_pages = 200;
unsigned long mock_uptime = 12345;
int mock_procs = 5;

// Mock Implementations
unsigned long vm_phys_get_free(void) {
    return mock_free_pages;
}

unsigned long vm_phys_get_used(void) {
    return mock_used_pages;
}

unsigned long get_uptime(void) {
    return mock_uptime;
}

uint32_t sched_get_system_load(void) {
    return 1; // 1.0 load
}

// Mock processes array
// pm/pm.h declares 'extern process_t processes[MAX_PROCS];'
// We mocked pm.h to define process_t with just pid field.
#include <pm/pm.h>
process_t processes[MAX_PROCS];

// Include source file directly
// We need to define HOST_TEST to avoid conflicts if sysinfo.c has guards (it doesn't seem to)
// sysinfo.c includes headers we mocked.
#include "../../sys/kern/sysinfo.c"

int main() {
    printf("Running sysinfo host tests...\n");

    // Initialize mock processes
    memset(processes, 0, sizeof(processes));
    // Set up some dummy processes
    // sysinfo.c checks: if (processes[i].pid != -1) procs++;
    // So we should initialize others to -1 first.
    for(int i=0; i<MAX_PROCS; i++) processes[i].pid = -1;

    // Set first 5 processes
    for(int i=0; i<mock_procs; i++) processes[i].pid = i + 1;

    // Test 1: do_sysinfo with valid pointer
    struct sysinfo info;
    memset(&info, 0, sizeof(info));
    int ret = do_sysinfo(&info);
    if (ret != 0) {
        printf("FAIL: do_sysinfo returned %d\n", ret);
        return 1;
    }

    // Verify fields
    if (info.uptime != mock_uptime) {
        printf("FAIL: uptime %ld != %ld\n", info.uptime, mock_uptime);
        return 1;
    }
    if (info.totalram != (mock_free_pages + mock_used_pages) * PAGE_SIZE) {
        printf("FAIL: totalram mismatch\n");
        return 1;
    }
    if (info.freeram != mock_free_pages * PAGE_SIZE) {
        printf("FAIL: freeram mismatch\n");
        return 1;
    }
    if (info.procs != mock_procs) {
        printf("FAIL: procs %d != %d\n", info.procs, mock_procs);
        return 1;
    }

    printf("PASS: do_sysinfo valid\n");

    // Test 2: do_sysinfo with NULL pointer
    ret = do_sysinfo(NULL);
    // sysinfo.c returns -14 (EFAULT)
    if (ret != -14) {
        printf("FAIL: do_sysinfo(NULL) returned %d, expected -14\n", ret);
        return 1;
    }
    printf("PASS: do_sysinfo(NULL)\n");

    // Test 3: sys_sysinfo with valid pointer
    // Mock user pointer check: sys_sysinfo checks (uintptr_t)info >= 0xC0000000
    // In host test, pointers are usually low addresses (on 32-bit) or high (on 64-bit).
    // We assume address space allows valid pointers below 3GB.

    if ((uintptr_t)&info >= 0xC0000000) {
        printf("WARN: stack address %p >= 0xC0000000, skipping sys_sysinfo test\n", &info);
    } else {
        ret = sys_sysinfo(&info);
        if (ret != 0) {
             printf("FAIL: sys_sysinfo returned %d\n", ret);
             return 1;
        }
        printf("PASS: sys_sysinfo valid\n");
    }

    // Test 4: sys_sysinfo with NULL pointer
    // NULL is 0. 0 < 0xC0000000.
    // Should call do_sysinfo(NULL) -> returns -14.
    ret = sys_sysinfo(NULL);
    if (ret != -14) {
        printf("FAIL: sys_sysinfo(NULL) returned %d, expected -14\n", ret);
        return 1;
    }
    printf("PASS: sys_sysinfo(NULL)\n");

    // Test 5: sys_sysinfo with invalid range pointer
    // Pass (struct sysinfo *)0xC0000000
    struct sysinfo *bad_ptr = (struct sysinfo *)0xC0000000;
    ret = sys_sysinfo(bad_ptr);
    if (ret != -14) {
        printf("FAIL: sys_sysinfo(0xC0000000) returned %d, expected -14\n", ret);
        return 1;
    }
    printf("PASS: sys_sysinfo(0xC0000000)\n");

    return 0;
}
