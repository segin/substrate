#include "../../vfs/vfs.h"
#include "../input/keyboard.h"
#include "../video/vga.h"
#include <string.h>

// /dev/null
static uint32_t null_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0; // EOF
}

static uint32_t null_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size; // Discarded
}

// /dev/zero
static uint32_t zero_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

// /dev/full
static uint32_t full_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer; (void)size;
    return 0; // Simulate ENOSPC (no bytes written)
}

// /dev/random (very simple PRNG)
static uint32_t random_state = 123456789;
static uint32_t random_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        random_state = random_state * 1103515245 + 12345;
        buffer[i] = (uint8_t)((random_state / 65536) % 256);
    }
    return size;
}

// /dev/tty
static uint32_t tty_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    uint32_t count = 0;
    while (count < size) {
        char c = keyboard_getc();
        if (c == 0) break;
        buffer[count++] = (uint8_t)c;
    }
    return count;
}

static uint32_t tty_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    vga_write((const char *)buffer, size);
    return size;
}

static fs_node_t null_node;

static fs_node_t zero_node;

static fs_node_t full_node;

static fs_node_t random_node;

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



    memset(&random_node, 0, sizeof(fs_node_t));

    strcpy(random_node.name, "random");

    random_node.flags = FS_CHARDEVICE;

    random_node.read = &random_read;

    random_node.write = &null_write;

    devfs_register_device(&random_node);



    memset(&tty_node, 0, sizeof(fs_node_t));

    strcpy(tty_node.name, "tty");

    tty_node.flags = FS_CHARDEVICE;

    tty_node.read = &tty_read;

    tty_node.write = &tty_write;

    devfs_register_device(&tty_node);

}
