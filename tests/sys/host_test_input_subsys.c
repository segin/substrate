#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/resource.h>

// Forward declarations
struct fs_node;

// Mocks
int devfs_register_called = 0;
void devfs_register_device(struct fs_node *node) {
    (void)node;
    devfs_register_called = 1;
}

void sched_wakeup(void *chan) { (void)chan; }
void sched_sleep(void *chan) { (void)chan; }

// Use correct types as defined in kernel headers
// We just need to define struct timezone if host doesn't
struct timezone;
int sys_gettimeofday(struct timeval *tv, struct timezone *tz) { (void)tv; (void)tz; return 0; }
void kprint(const char *fmt) { (void)fmt; }

// define AC_COMM_LEN for sys/proc.h when compiling on host
#define AC_COMM_LEN 16

// Include the source file directly for testing
#include "../../sys/drivers/input/input_subsys.c"

void test_input_init() {
    printf("Testing input_init...\n");

    // Reset state
    input_devices = (input_dev_t *)0xDEADBEEF; // Set to garbage
    devfs_register_called = 0;

    // Call function
    input_init();

    // Verify
    assert(input_devices == NULL);
    assert(devfs_register_called == 1);

    printf("PASS\n");
}

int main() {
    printf("Running input_subsys tests...\n");
    test_input_init();
    printf("All tests passed.\n");
    return 0;
}
