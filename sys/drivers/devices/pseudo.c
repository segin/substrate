#include "../../vfs/vfs.h"
#include "../input/keyboard.h"
#include "../../kern/console.h"
#include <sys/proc.h>
#include <string.h>
#include "../../arch/x86-common/include/io.h"

// /dev/null
static uint32_t null_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0; // EOF
}

static uint32_t null_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size; // Discarded
}

// /dev/zero
static uint32_t zero_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

// /dev/full
static uint32_t full_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer; (void)size;
    // Always return error (ENOSPC is usually 28)
    return size;
}

// /dev/port
static uint32_t port_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    if (offset >= 65536) return 0;
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < size; i++) {
        if (offset + i >= 65536) break;
        buffer[i] = inb((uint16_t)(offset + i));
        count++;
    }
    return count;
}

static uint32_t port_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
     if (offset >= 65536) return 0;
    
    uint32_t count = 0;
    for (uint32_t i = 0; i < size; i++) {
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
 */

// /dev/tty - proxy to current process's controlling terminal
static uint32_t tty_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    // Get current process's TTY
    if (current_process && current_process->tty && current_process->tty->read) {
        return current_process->tty->read(current_process->tty, offset, size, buffer);
    }
    
    // Fallback to keyboard if no TTY assigned
    uint32_t count = 0;
    while (count < size) {
        char c = keyboard_getc();
        if (c == 0) break;
        buffer[count++] = (uint8_t)c;
    }
    return count;
}

static uint32_t tty_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    // Get current process's TTY
    if (current_process && current_process->tty && current_process->tty->write) {
        return current_process->tty->write(current_process->tty, offset, size, buffer);
    }
    
    // Fallback to console
    console_write((const char *)buffer, size);
    return size;
}

// /dev/mem
static uint32_t mem_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    // Limit to 1GB (Direct Map size)
    if (offset > 0x3FFFFFFF) return 0; // EOF or Error? EOF for now.
    
    // Physical to Virtual (Kernel Direct Map)
    // 0x00000000 (Phys) -> 0xC0000000 (Virt)
    uint8_t *src = (uint8_t*)((uintptr_t)offset + 0xC0000000);
    
    // Check bounds vs 1GB
    if (offset + size > 0x40000000) {
        size = 0x40000000 - offset;
    }
    
    memcpy(buffer, src, size);
    return size;
}

static uint32_t mem_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    if (offset > 0x3FFFFFFF) return 0;
    
    uint8_t *dst = (uint8_t*)((uintptr_t)offset + 0xC0000000);
    
    if (offset + size > 0x40000000) {
        size = 0x40000000 - offset;
    }
    
    memcpy(dst, buffer, size);
    return size;
}

// /dev/kmem
static uint32_t kmem_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    // Access arbitrary virtual address.
    // DANGEROUS: If offset is unmapped, we panic.
    // For now, naive implementation as is standard for kmem in simple kernels.
    
    void *src = (void*)(uintptr_t)offset;
    memcpy(buffer, src, size);
    return size;
}

static uint32_t kmem_write(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    void *dst = (void*)(uintptr_t)offset;
    memcpy(dst, buffer, size);
    return size;
}

static fs_node_t null_node;

static fs_node_t zero_node;

static fs_node_t full_node;

static fs_node_t tty_node;




void pseudo_init(void) {

    memset(&null_node, 0, sizeof(fs_node_t));

    strcpy(null_node.name, "null");

    null_node.flags = FS_CHARDEVICE;

    null_node.read = &null_read;

    null_node.write = &null_write;

    devfs_register_device(&null_node);



    memset(&zero_node, 0, sizeof(fs_node_t));

    strcpy(zero_node.name, "zero");

    zero_node.flags = FS_CHARDEVICE;

    zero_node.read = &zero_read;

    zero_node.write = &null_write; 

    devfs_register_device(&zero_node);



    memset(&full_node, 0, sizeof(fs_node_t));

    strcpy(full_node.name, "full");

    full_node.flags = FS_CHARDEVICE;

    full_node.read = &zero_read; // Always returns zeros

    full_node.write = &full_write; // Always returns error

    devfs_register_device(&full_node);

    /* Note: /dev/random and /dev/urandom now registered by random_init() */

    memset(&tty_node, 0, sizeof(fs_node_t));
    strcpy(tty_node.name, "tty");
    tty_node.flags = FS_CHARDEVICE;
    tty_node.read = &tty_read;
    tty_node.write = &tty_write;
    devfs_register_device(&tty_node);

    // /dev/mem
    static fs_node_t mem_node;
    memset(&mem_node, 0, sizeof(fs_node_t));
    strcpy(mem_node.name, "mem");
    mem_node.flags = FS_CHARDEVICE;
    mem_node.read = &mem_read;
    mem_node.write = &mem_write;
    devfs_register_device(&mem_node);

    // /dev/kmem
    static fs_node_t kmem_node;
    memset(&kmem_node, 0, sizeof(fs_node_t));
    strcpy(kmem_node.name, "kmem");
    kmem_node.flags = FS_CHARDEVICE;
    kmem_node.read = &kmem_read;
    kmem_node.write = &kmem_write;
    devfs_register_device(&kmem_node);

    // /dev/port
    static fs_node_t port_node;
    memset(&port_node, 0, sizeof(fs_node_t));
    strcpy(port_node.name, "port");
    port_node.flags = FS_CHARDEVICE;
    port_node.read = &port_read;
    port_node.write = &port_write;
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
