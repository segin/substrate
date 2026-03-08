#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void kprint(const char *msg) {
    printf("KPRINT: %s", msg);
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

#include <fs/fat/fat.h>

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

#include "../../sys/fs/fat/fat.c"

int main(void) {
    fat_fs_t fs;
    uint64_t root_slot_a;
    uint64_t root_slot_b;
    uint64_t cluster_inode_a;
    uint64_t cluster_inode_b;
    fs_node_t *node_a;
    fs_node_t *node_b;

    memset(&fs, 0, sizeof(fs));
    fs.fat_type = 16;
    fs.root_dir_first_sector = 32;
    fs.first_data_sector = 128;
    fs.bpb.bytes_per_sector = 512;
    fs.bpb.sectors_per_cluster = 1;

    root_slot_a = fat_make_synth_inode(&fs, 0, 2, 64, 0);
    root_slot_b = fat_make_synth_inode(&fs, 0, 2, 64, 0);
    assert(root_slot_a == root_slot_b);

    assert(root_slot_a != fat_make_synth_inode(&fs, 0, 2, 96, 0));
    assert(root_slot_a != fat_make_synth_inode(&fs, 0, 3, 64, 0));

    cluster_inode_a = fat_make_synth_inode(&fs, 9, 1, 32, 0);
    cluster_inode_b = fat_make_synth_inode(&fs, 9, 1, 32, 0);
    assert(cluster_inode_a == cluster_inode_b);
    assert(cluster_inode_a != fat_make_synth_inode(&fs, 9, 1, 64, 0));

    assert(fat_make_synth_inode(&fs, 9, 1, 32, 77) == 77);

    node_a = fat_alloc_node(&fs, "FILE.TXT", root_slot_a, 0, 123, 0);
    node_b = fat_alloc_node(&fs, "FILE.TXT", root_slot_a, 0, 123, 0);
    assert(node_a != NULL);
    assert(node_b != NULL);
    assert(node_a->inode == root_slot_a);
    assert(node_b->inode == root_slot_a);

    puts("host_test_fat_inode_identity: PASS");
    return 0;
}
