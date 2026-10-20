/*
 * host_test_fat_lfn_truncation - a long filename spread over several LFN
 * entries comes back from fat_readdir() whole, and fat_parse_lfn() stops
 * at the caller's limit for an entry that straddles it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include <vfs/vfs.h>

void kprint(const char *s) {
    (void)s;
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

#include "../../sys/fs/fat/fat.c"
#include "fat_host_stubs.h"

size_t blkdev_read_bytes(blkdev_t *dev, uint64_t offset, size_t size, void *buffer) {
    (void)dev; (void)offset; (void)size; (void)buffer;
    return 0;
}

/* One 512-byte cluster of directory entries, served at device offset 0. */
static uint8_t mock_disk[4096];

static size_t device_read_mock(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset >= sizeof(mock_disk)) return 0;
    if ((size_t)offset + size > sizeof(mock_disk)) size = sizeof(mock_disk) - (size_t)offset;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static void set_lfn_chars(fat_lfn_t *lfn, const char *s) {
    for (int i = 0; i < 5; i++) lfn->name1[i] = (uint8_t)s[i];
    for (int i = 0; i < 6; i++) lfn->name2[i] = (uint8_t)s[5 + i];
    for (int i = 0; i < 2; i++) lfn->name3[i] = (uint8_t)s[11 + i];
}

int main(void) {
    printf("Running FAT LFN Truncation Test...\n");

    memset(mock_disk, 0, sizeof(mock_disk));

    /*
     * "ThisIsAVeryLongFilename.txt" (27 chars) takes three LFN entries,
     * stored last-first ahead of the short entry:
     *   order 0x43: "t"
     *   order 0x02: "ngFilename.tx"
     *   order 0x01: "ThisIsAVeryLo"
     */
    fat_dirent_t *entries = (fat_dirent_t *)mock_disk;

    fat_lfn_t *lfn3 = (fat_lfn_t *)&entries[0];
    lfn3->order = 0x40 | 0x03;
    lfn3->attr = FAT_ATTR_LFN;
    lfn3->name1[0] = 't';
    lfn3->name1[1] = 0;                 /* terminator, then 0xFFFF padding */
    for (int i = 2; i < 5; i++) lfn3->name1[i] = 0xFFFF;
    for (int i = 0; i < 6; i++) lfn3->name2[i] = 0xFFFF;
    for (int i = 0; i < 2; i++) lfn3->name3[i] = 0xFFFF;
    lfn3->checksum = 0x11;

    fat_lfn_t *lfn2 = (fat_lfn_t *)&entries[1];
    lfn2->order = 0x02;
    lfn2->attr = FAT_ATTR_LFN;
    set_lfn_chars(lfn2, "ngFilename.tx");
    lfn2->checksum = 0x11;

    fat_lfn_t *lfn1 = (fat_lfn_t *)&entries[2];
    lfn1->order = 0x01;
    lfn1->attr = FAT_ATTR_LFN;
    set_lfn_chars(lfn1, "ThisIsAVeryLo");
    lfn1->checksum = 0x11;

    fat_dirent_t *short_entry = &entries[3];
    memcpy(short_entry->name, "THISIS~1TXT", 11);
    short_entry->attr = FAT_ATTR_ARCHIVE;
    short_entry->file_size = 1234;
    short_entry->cluster_low = 100;

    /* entries[4] stays zero: end of directory. */

    /* A FAT32 volume whose data area starts at sector 0, so cluster 2 is
     * device offset 0. */
    static fat_fs_t fs;
    fs.bpb.bytes_per_sector = 512;
    fs.bpb.sectors_per_cluster = 1;
    fs.cluster_size = 512;
    fs.fat_type = 32;
    fs.first_data_sector = 0;
    fs.total_clusters = 100;

    static fs_node_t device_node;
    device_node.read = device_read_mock;
    fs.device = &device_node;

    static fat_node_t node_ctx;
    node_ctx.fs = &fs;
    node_ctx.first_cluster = 2;
    node_ctx.size = 512;

    static fs_node_t dir_node;
    dir_node.impl = (uintptr_t)&node_ctx;
    dir_node.readdir = fat_readdir;

    struct dirent *d = fat_readdir(&dir_node, 0);
    if (!d) {
        printf("FAIL: fat_readdir returned NULL\n");
        return 1;
    }

    const char *expected = "ThisIsAVeryLongFilename.txt";
    if (strcmp(d->d_name, expected) != 0) {
        printf("FAIL: name '%s' does not match expected '%s'%s\n", d->d_name, expected,
               strncmp(d->d_name, expected, strlen(d->d_name)) == 0 ? " (truncated)" : "");
        return 1;
    }

    /*
     * Order 20, the highest the spec allows, starts at index 19 * 13 = 247.
     * With max_len 255 only 247..254 fit: the parser must write those eight
     * characters and then report the overflow without touching index 255.
     */
    char test_buf[300];
    memset(test_buf, 0, sizeof(test_buf));

    fat_lfn_t test_lfn;
    memset(&test_lfn, 0, sizeof(test_lfn));
    test_lfn.order = 20;
    set_lfn_chars(&test_lfn, "XXXXXXXXXXXXX");

    int ret = fat_parse_lfn(&test_lfn, test_buf, 255);
    if (ret != -1) {
        printf("FAIL: order 20 against max_len 255 returned %d, expected -1\n", ret);
        return 1;
    }
    if (test_buf[255] != 0) {
        printf("FAIL: wrote past the limit (buf[255]=%d)\n", test_buf[255]);
        return 1;
    }
    if (test_buf[247] != 'X' || test_buf[254] != 'X') {
        printf("FAIL: the part that fits (247..254) was not written\n");
        return 1;
    }

    printf("host_test_fat_lfn_truncation: PASS\n");
    return 0;
}
