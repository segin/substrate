#include <arch/x86-common/io.h>
#include <drivers/input/keyboard.h>
#include <drivers/console/uart/uart.h>
#include <kern/console.h>
#include <string.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <sys/tty.h>
#include "null.h"
#include "zero.h"
#include "lpt.h"
#include "mem.h"
#include "kmem.h"

// /dev/null
// Implemented in null.c

// /dev/zero - Now implemented in zero.c
// /dev/full - Now implemented in full.c

// /dev/port
static size_t port_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (!current_process || current_process->euid != 0) return 0;
    if (offset >= 65536) return 0;
    
    size_t count = 0;
    for (size_t i = 0; i < size; i++) {
        if (offset + i >= 65536) break;
        buffer[i] = inb((uint16_t)(offset + i));
        count++;
    }
    return count;
}

static size_t port_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node;
    if (!current_process || current_process->euid != 0) return 0;
    if (offset >= 65536) return 0;
    
    size_t count = 0;
    for (size_t i = 0; i < size; i++) {
        if (offset + i >= 65536) break;
        outb((uint16_t)(offset + i), buffer[i]);
        count++;
    }
    return count;
}

// /dev/stdin -> /proc/self/fd/0
static int stdin_readlink(fs_node_t *node, char *buf, size_t size) {
    (void)node;
    const char *target = "/proc/self/fd/0";
    size_t len = strlen(target);
    if (len > size) len = size;
    memcpy(buf, target, len);
    return len;
}

// /dev/stdout -> /proc/self/fd/1
static int stdout_readlink(fs_node_t *node, char *buf, size_t size) {
    (void)node;
    const char *target = "/proc/self/fd/1";
    size_t len = strlen(target);
    if (len > size) len = size;
    memcpy(buf, target, len);
    return len;
}

// /dev/stderr -> /proc/self/fd/2
static int stderr_readlink(fs_node_t *node, char *buf, size_t size) {
    (void)node;
    const char *target = "/proc/self/fd/2";
    size_t len = strlen(target);
    if (len > size) len = size;
    memcpy(buf, target, len);
    return len;
}

/*
 * Note: /dev/random and /dev/urandom are now registered in sys/kern/random.c
 * with a proper ChaCha20-based CSPRNG implementation.
 *
 * /dev/tty is registered by devfs_init() so the proxy keeps tty ioctl support.
 */

// /dev/mem - Now implemented in mem.c
// /dev/kmem - Implemented in kmem.c

void pseudo_init(void) {
    // Initialize /dev/null (from null.c)
    null_init();

    // Initialize /dev/zero (from zero.c)
    zero_init();

    /* Note: /dev/random and /dev/urandom now registered by random_init() */

    /* Register communication character devices under /dev/comm/. */
    uart_devfs_init();
    lpt_init();

    /* Initialize /dev/mem (handled by mem.c) */
    mem_init();

    /* Initialize /dev/mem_test (handled by mem_test_helper.c) */
    mem_test_init();

    // /dev/kmem
    kmem_dev_init();

    // /dev/port
    static fs_node_t port_node;
    memset(&port_node, 0, sizeof(fs_node_t));
    strcpy(port_node.name, "port");
    port_node.flags = FS_CHARDEVICE;
    port_node.mask = 0600;
    port_node.uid = 0;
    port_node.gid = 0;
    port_node.read = &port_read;
    port_node.write = &port_write;
    port_node.rdev = (1 << 8) | 4;
    devfs_register_device(&port_node);

    // /dev/stdin
    static fs_node_t stdin_node;
    memset(&stdin_node, 0, sizeof(fs_node_t));
    strcpy(stdin_node.name, "stdin");
    stdin_node.flags = FS_SYMLINK;
    stdin_node.readlink = &stdin_readlink;
    devfs_register_device(&stdin_node);

    // /dev/stdout
    static fs_node_t stdout_node;
    memset(&stdout_node, 0, sizeof(fs_node_t));
    strcpy(stdout_node.name, "stdout");
    stdout_node.flags = FS_SYMLINK;
    stdout_node.readlink = &stdout_readlink;
    devfs_register_device(&stdout_node);

    // /dev/stderr
    static fs_node_t stderr_node;
    memset(&stderr_node, 0, sizeof(fs_node_t));
    strcpy(stderr_node.name, "stderr");
    stderr_node.flags = FS_SYMLINK;
    stderr_node.readlink = &stderr_readlink;
    devfs_register_device(&stderr_node);
}
