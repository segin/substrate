/*
 * sys/drivers/devices/kmem_test.c
 *
 * Kernel Test Helper for /dev/kmem
 *
 * Allocates a safe kernel buffer and exports its address to userland
 * for regression testing of /dev/kmem.
 */

#include <sys/types.h>
#include <sys/sysctl.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

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

    /* Write recognizable strings for verification */
    strcpy((char*)kmem_test_buffer, "KMEM_TEST_START: This is a safe kernel buffer for testing.");
    char *end_msg = "KMEM_TEST_END: End of buffer.";
    strcpy((char*)kmem_test_buffer + kmem_test_size - strlen(end_msg) - 1, end_msg);

    char buf[128];
    sprintf(buf, "kmem_test: buffer allocated at 0x%08x (size %d)\n", (unsigned int)kmem_test_buffer, kmem_test_size);
    kprint(buf);
}
