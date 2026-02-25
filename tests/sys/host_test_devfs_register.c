#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>

// Mock kmalloc/kfree
void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Mock tty functions
struct tty;
typedef struct tty tty_t;

int tty_read(tty_t *tty, char *buf, int len) { return 0; }
int tty_write(tty_t *tty, const char *buf, int len) { return 0; }
int tty_ioctl(tty_t *tty, uint32_t cmd, unsigned long arg) { return 0; }

// Mock console
struct fs_node;
struct fs_node *console_get_node(void) { return NULL; }

// Mock VFS
typedef struct filesystem filesystem_t;
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Mock process
struct process;
typedef struct process process_t;
process_t *current_process = NULL;

// Include the source file
#include "../../sys/fs/devfs.c"

// Test cases
void test_block_device_registration() {
    printf("Testing Block Device Registration...\n");

    // Reset state if needed (devfs_init called once in main)

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    strcpy(node.name, "sda1");
    node.flags = FS_BLOCKDEVICE;

    devfs_register_device(&node);

    // Verification
    // We expect "storage/sda1" to be created.
    // devfs implementation is hierarchical.
    // root -> "storage" (dir) -> "sda1" (node)

    // Find "storage" directory
    fs_node_t *storage_node = devfs_dir_finddir(&devfs_root_node, "storage");
    assert(storage_node != NULL);
    assert(storage_node->flags == FS_DIRECTORY);

    // Find "sda1" inside storage
    fs_node_t *sda1_node = storage_node->finddir(storage_node, "sda1");
    assert(sda1_node != NULL);
    assert(sda1_node == &node); // Should be the same node pointer

    printf("PASS: Block device registered under storage/\n");
}

void test_char_device_registration() {
    printf("Testing Char Device Registration...\n");

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    strcpy(node.name, "mychar");
    node.flags = FS_CHARDEVICE;

    devfs_register_device(&node);

    // Verification
    // We expect "mychar" under root

    fs_node_t *found = devfs_dir_finddir(&devfs_root_node, "mychar");
    assert(found != NULL);
    assert(found == &node);

    printf("PASS: Char device registered under root\n");
}

void test_nested_path_registration() {
    printf("Testing Nested Path Registration...\n");

    fs_node_t node;
    memset(&node, 0, sizeof(node));
    strcpy(node.name, "input/mouse0");
    node.flags = FS_CHARDEVICE;

    devfs_register_device(&node);

    // Verification
    fs_node_t *input_node = devfs_dir_finddir(&devfs_root_node, "input");
    assert(input_node != NULL);
    assert(input_node->flags == FS_DIRECTORY);

    fs_node_t *mouse_node = input_node->finddir(input_node, "mouse0");
    assert(mouse_node != NULL);
    assert(mouse_node == &node);

    printf("PASS: Device registered with nested path\n");
}

int main() {
    devfs_init();

    test_block_device_registration();
    test_char_device_registration();
    test_nested_path_registration();

    printf("All tests passed!\n");
    return 0;
}
