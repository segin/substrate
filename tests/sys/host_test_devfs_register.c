#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Mock kmalloc family - wrappers to match prototypes in vm/vm_kmem.h
void *kmalloc(size_t size) {
    return malloc(size);
}

void *kzalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void kmem_get_stats(uint64_t *allocs, uint64_t *frees, uint64_t *bytes) {
    if(allocs) *allocs = 0;
    if(frees) *frees = 0;
    if(bytes) *bytes = 0;
}

// Define process_t and current_process
// We include sys/proc.h to get the struct definition.
#include <sys/proc.h>

process_t *current_process = NULL;

// Mock console_get_node
struct fs_node;
struct fs_node *console_get_node(void) { return NULL; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

// Mock vfs_register_filesystem
struct filesystem;
void vfs_register_filesystem(struct filesystem *fs) { (void)fs; }

// Mock tty functions called by devfs.c
struct tty;
int tty_read(struct tty *tty, char *buf, int len) { return 0; }
int tty_write(struct tty *tty, const char *buf, int len) { return 0; }
int tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) { return 0; }

// Include the source under test
#include "../../sys/fs/devfs.c"

// Helpers for testing
void print_tree(devfs_entry_t *entry, int depth) {
    if (!entry) return;
    for (int i=0; i<depth; i++) printf("  ");
    printf("- %s\n", entry->name);
    if (entry->child) print_tree(entry->child, depth+1);
    if (entry->next) print_tree(entry->next, depth);
}

int main() {
    printf("Initializing devfs...\n");
    devfs_init();

    // Verify root entry exists
    if (!root_entry) {
        printf("FAIL: root_entry is NULL\n");
        return 1;
    }
    printf("root_entry: %s\n", root_entry->name);

    // Test 1: Block Device
    printf("Registering block device 'sda'...\n");
    fs_node_t *block_node = malloc(sizeof(fs_node_t));
    memset(block_node, 0, sizeof(fs_node_t));
    strcpy(block_node->name, "sda");
    block_node->flags = FS_BLOCKDEVICE;

    devfs_register_device(block_node);

    // Verify it is under storage/sda
    // devfs_add_entry adds to root_entry.
    // So we expect root_entry -> child "storage" -> child "sda"

    devfs_entry_t *storage = devfs_find_child(root_entry, "storage");
    if (!storage) {
        printf("FAIL: 'storage' directory not found\n");
        print_tree(root_entry, 0);
        return 1;
    }

    devfs_entry_t *sda = devfs_find_child(storage, "sda");
    if (!sda) {
        printf("FAIL: 'sda' not found in 'storage'\n");
        print_tree(root_entry, 0);
        return 1;
    }

    if (sda->node != block_node) {
        printf("FAIL: 'sda' node does not match registered node\n");
        return 1;
    }
    printf("PASS: Block device 'sda' found in 'storage/sda'\n");

    // Test 2: Character Device
    printf("Registering character device 'ttyUSB0'...\n");
    fs_node_t *char_node = malloc(sizeof(fs_node_t));
    memset(char_node, 0, sizeof(fs_node_t));
    strcpy(char_node->name, "ttyUSB0");
    char_node->flags = FS_CHARDEVICE;

    devfs_register_device(char_node);

    devfs_entry_t *ttyUSB0 = devfs_find_child(root_entry, "ttyUSB0");
    if (!ttyUSB0) {
        printf("FAIL: 'ttyUSB0' not found in root\n");
        print_tree(root_entry, 0);
        return 1;
    }
    printf("PASS: Character device 'ttyUSB0' found in root\n");

    // Test 3: Subdirectory device (simulating nested path in name)
    // Note: The current implementation of devfs_register_device for CHARDEVICE uses node->name directly.
    // devfs_add_entry handles slashes.

    printf("Registering character device 'input/mouse0'...\n");
    fs_node_t *mouse_node = malloc(sizeof(fs_node_t));
    memset(mouse_node, 0, sizeof(fs_node_t));
    strcpy(mouse_node->name, "input/mouse0");
    mouse_node->flags = FS_CHARDEVICE;

    devfs_register_device(mouse_node);

    devfs_entry_t *input = devfs_find_child(root_entry, "input");
    if (!input) {
        printf("FAIL: 'input' directory not found\n");
        return 1;
    }

    devfs_entry_t *mouse0 = devfs_find_child(input, "mouse0");
    if (!mouse0) {
        printf("FAIL: 'mouse0' not found in 'input'\n");
        return 1;
    }
    printf("PASS: Character device 'input/mouse0' found\n");

    if (devfs_register_alias("by-id/test-mouse", "/dev/input/mouse0") != 0) {
        printf("FAIL: alias registration failed\n");
        return 1;
    }
    devfs_entry_t *by_id = devfs_find_child(root_entry, "by-id");
    devfs_entry_t *alias = by_id ? devfs_find_child(by_id, "test-mouse") : NULL;
    if (!alias || !alias->node || alias->node->readlink == NULL) {
        printf("FAIL: alias node missing or invalid\n");
        return 1;
    }
    char linkbuf[128];
    int linklen = alias->node->readlink(alias->node, linkbuf, sizeof(linkbuf));
    if (linklen <= 0 || strncmp(linkbuf, "/dev/input/mouse0", (size_t)linklen) != 0) {
        printf("FAIL: alias target incorrect\n");
        return 1;
    }
    printf("PASS: Alias /dev/by-id/test-mouse points to /dev/input/mouse0\n");

    devfs_unregister_alias("by-id/test-mouse");
    if (devfs_find_child(by_id, "test-mouse") != NULL) {
        printf("FAIL: alias removal failed\n");
        return 1;
    }
    printf("PASS: Alias removal works\n");

    devfs_unregister_device(mouse_node);
    if (devfs_find_child(input, "mouse0") != NULL) {
        printf("FAIL: device removal failed\n");
        return 1;
    }
    printf("PASS: Device removal works\n");

    printf("All tests passed!\n");
    return 0;
}
