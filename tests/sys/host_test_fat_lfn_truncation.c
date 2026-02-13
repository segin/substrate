#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Mocks for kernel functions
void kprint(const char *s) {
    // printf("[KERNEL] %s", s);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// Minimal VFS definitions to satisfy fat.c includes
#include <vfs/vfs.h>

void vfs_register_filesystem(filesystem_t *fs) {
    // No-op
}

// Include the source file directly
// We need to compile this with -Isys
#include "../../sys/fs/fat/fat.c"

// Mock device read function
// We will serve directory entries from a static buffer
static uint8_t mock_disk[4096]; // 1 sector cluster, plenty for dir

static size_t device_read_mock(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (offset >= sizeof(mock_disk)) return 0;
    if (offset + size > sizeof(mock_disk)) size = sizeof(mock_disk) - offset;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

int main() {
    printf("Running FAT LFN Truncation Reproduction Test...\n");

    // Clear mock disk
    memset(mock_disk, 0, sizeof(mock_disk));

    // Setup a directory in the first cluster (offset 0 relative to data area, but we'll cheat)
    // fat_read_sectors uses: offset = sector * bytes_per_sector
    // We will set up fs such that cluster 2 (first data cluster) maps to offset 0 of our mock disk?
    // Actually, let's just make read return from mock_disk regardless of offset for simplicity,
    // provided we only read one sector.

    // Construct LFN entries for "ThisIsAVeryLongFilename.txt" (27 chars)
    // LFN1: "ThisIsAVeryLo" (13 chars)
    // LFN2: "ngFilename.tx" (13 chars)
    // LFN3: "t" (1 char)

    // Order on disk: LFN3, LFN2, LFN1, ShortEntry

    fat_dirent_t *entries = (fat_dirent_t *)mock_disk;

    // Entry 0: LFN3 (Order 0x43 - Last | 3)
    // Chars 26..27: "t"
    fat_lfn_t *lfn3 = (fat_lfn_t *)&entries[0];
    memset(lfn3, 0, sizeof(fat_lfn_t));
    lfn3->order = 0x40 | 0x03; // Last logical, 3rd physical
    lfn3->attr = FAT_ATTR_LFN;
    lfn3->name1[0] = 't';
    lfn3->name1[1] = 0; // Null terminator
    lfn3->name1[2] = 0xFFFF; // Padding
    lfn3->name1[3] = 0xFFFF;
    lfn3->name1[4] = 0xFFFF;
    // name2, name3 zeroed/padded
    for(int i=0; i<6; i++) lfn3->name2[i] = 0xFFFF;
    for(int i=0; i<2; i++) lfn3->name3[i] = 0xFFFF;
    lfn3->checksum = 0x11; // Dummy checksum

    // Entry 1: LFN2 (Order 0x02)
    // Chars 13..25: "ngFilename.tx"
    fat_lfn_t *lfn2 = (fat_lfn_t *)&entries[1];
    memset(lfn2, 0, sizeof(fat_lfn_t));
    lfn2->order = 0x02;
    lfn2->attr = FAT_ATTR_LFN;
    const char *s2 = "ngFilename.tx";
    for(int i=0; i<5; i++) lfn2->name1[i] = s2[i];
    for(int i=0; i<6; i++) lfn2->name2[i] = s2[5+i];
    for(int i=0; i<2; i++) lfn2->name3[i] = s2[11+i];
    lfn2->checksum = 0x11;

    // Entry 2: LFN1 (Order 0x01)
    // Chars 0..12: "ThisIsAVeryLo"
    fat_lfn_t *lfn1 = (fat_lfn_t *)&entries[2];
    memset(lfn1, 0, sizeof(fat_lfn_t));
    lfn1->order = 0x01;
    lfn1->attr = FAT_ATTR_LFN;
    const char *s1 = "ThisIsAVeryLo";
    for(int i=0; i<5; i++) lfn1->name1[i] = s1[i];
    for(int i=0; i<6; i++) lfn1->name2[i] = s1[5+i];
    for(int i=0; i<2; i++) lfn1->name3[i] = s1[11+i];
    lfn1->checksum = 0x11;

    // Entry 3: Short Entry
    fat_dirent_t *short_entry = &entries[3];
    memset(short_entry, 0, sizeof(fat_dirent_t));
    memcpy(short_entry->name, "THISIS~1TXT", 11);
    short_entry->attr = FAT_ATTR_ARCHIVE;
    short_entry->file_size = 1234;
    short_entry->cluster_low = 100;

    // Entry 4: End of directory
    entries[4].name[0] = 0x00;

    // Setup FS structures
    fat_fs_t fs;
    memset(&fs, 0, sizeof(fs));
    fs.bpb.bytes_per_sector = 512;
    fs.bpb.sectors_per_cluster = 1;
    fs.fat_type = 32;

    // Setup Node
    fs_node_t device_node;
    memset(&device_node, 0, sizeof(device_node));
    device_node.read = device_read_mock;
    fs.device = &device_node;

    fat_node_t node_ctx;
    memset(&node_ctx, 0, sizeof(node_ctx));
    node_ctx.fs = &fs;
    node_ctx.first_cluster = 2; // Arbitrary
    node_ctx.size = 4096;

    fs_node_t dir_node;
    memset(&dir_node, 0, sizeof(dir_node));
    dir_node.impl = (uintptr_t)&node_ctx;
    dir_node.readdir = fat_readdir;

    // Test fat_readdir
    // We expect "ThisIsAVeryLongFilename.txt"
    // With the bug, we might get truncated name.

    // Read index 0 (the file)
    printf("Reading directory index 0...\n");
    struct dirent *d = fat_readdir(&dir_node, 0);

    if (!d) {
        printf("FAILURE: fat_readdir returned NULL\n");
        return 1;
    }

    printf("Returned name: '%s'\n", d->name);

    const char *expected = "ThisIsAVeryLongFilename.txt";
    if (strcmp(d->name, expected) == 0) {
        printf("SUCCESS: Name matches expected.\n");
    } else {
        printf("FAILURE: Name '%s' does not match expected '%s'\n", d->name, expected);

        // Analyze failure
        if (strncmp(d->name, expected, strlen(d->name)) == 0 && strlen(d->name) < strlen(expected)) {
            printf("ANALYSIS: Name was TRUNCATED.\n");
        }
        return 1;
    }

    // Additional Test: Verify overflow edge case
    printf("\nTesting overflow edge case (idx near max_len)...\n");
    char test_buf[300];
    memset(test_buf, 0, sizeof(test_buf));

    fat_lfn_t test_lfn;
    memset(&test_lfn, 0, sizeof(test_lfn));
    test_lfn.order = 1; // dummy
    // Fill with 'X'
    for(int i=0; i<5; i++) test_lfn.name1[i] = 'X';
    for(int i=0; i<6; i++) test_lfn.name2[i] = 'X';
    for(int i=0; i<2; i++) test_lfn.name3[i] = 'X';

    // We want idx to be max_len - 1.
    // idx = ((order & 0x3F) - 1) * 13
    // We can't control idx precisely with order unless we modify fat_parse_lfn temporarily or use a specific order.
    // But order gives multiples of 13.
    // (1-1)*13 = 0.
    // (20-1)*13 = 19*13 = 247.
    // max_len = 255.
    // 247 is close. 247 + 13 = 260 > 255.
    // So if we pass order=20, it writes at 247..259.
    // It should write 247..254 (8 chars), then fail at 255.

    test_lfn.order = 20;
    int ret = fat_parse_lfn(&test_lfn, test_buf, 255);

    printf("Order 20 (idx 247) -> ret: %d\n", ret);

    if (ret == -1) {
        printf("SUCCESS: Overflow detected and handled.\n");
    } else {
        printf("FAILURE: Overflow NOT detected! ret=%d\n", ret);
        return 1;
    }

    // Verify it didn't write past 255
    if (test_buf[255] != 0) {
        printf("FAILURE: Wrote past buffer limit! buf[255]=%d\n", test_buf[255]);
        return 1;
    }

    // Verify it wrote up to 254
    if (test_buf[254] == 'X') {
        printf("SUCCESS: Wrote valid part up to 254.\n");
    } else {
        printf("FAILURE: Did not write expected part.\n");
    }

    return 0;
}
