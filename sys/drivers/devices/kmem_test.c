/*
 * sys/drivers/devices/kmem_test.c
 *
 * Kernel Test Helper for /dev/kmem
 *
 * Allocates a safe kernel buffer and exports its address to userland
 * for regression testing of /dev/kmem.
 */

#include <stdio.h>
#include <string.h>

#include <kern/console.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <vm/vm_kmem.h>

static void *kmem_test_buffer = NULL;
static int kmem_test_size = 4096;

/*
 * Sysctl Registration
 * On 32-bit systems, void* fits in int.
 */
#define DEBUG_KMEM_ADDR 1
#define DEBUG_KMEM_SIZE 2
SYSCTL_INT(debug, DEBUG_KMEM_ADDR, kmem_test_addr, CTLFLAG_RD, &kmem_test_buffer, 0, "Address of KMEM test buffer");
SYSCTL_INT(debug, DEBUG_KMEM_SIZE, kmem_test_size, CTLFLAG_RD, &kmem_test_size, 0, "Size of KMEM test buffer");

void kmem_test_init(void) {
    kmem_test_buffer = kmalloc(kmem_test_size);
    if (!kmem_test_buffer) {
        kprint("kmem_test: Failed to allocate test buffer\n");
        return;
    }

    /* Fill with known pattern */
    memset(kmem_test_buffer, 0xAA, kmem_test_size);

    if (kmem_test_size > 0) {
        snprintf((char*)kmem_test_buffer, kmem_test_size, "KMEM_TEST_START: This is a safe kernel buffer for testing.");

        const char *end_msg = "KMEM_TEST_END.\n";
        size_t end_msg_len = strlen(end_msg);

        if (kmem_test_size > (int)end_msg_len) {
            snprintf((char*)kmem_test_buffer + kmem_test_size - end_msg_len - 1, end_msg_len + 1, "%s", end_msg);
        }
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "kmem_test: buffer allocated at 0x%08x (size %d)\n", (unsigned int)kmem_test_buffer, kmem_test_size);
    kprint(buf);
}
