#define HOST_TEST

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Mocks
void kprint(const char *s) {
    printf("[KERNEL] %s", s);
}

void *kmalloc(size_t size) {
    return malloc(size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
    free(ptr);
}

// We need to define vfs_register_filesystem
#include <vfs/vfs.h> // This will use the kernel header from -I../../sys/include or similar

void vfs_register_filesystem(filesystem_t *fs) {
    printf("Registered filesystem: %s\n", fs->name);
}

// Include the source file to test
// We need to make sure includes inside fat.c work.
// fat.c includes <fs/fat/fat.h>, <kern/console.h>, <string.h>
// We should compile with -I../../sys -I../../sys/include

#include "../../sys/fs/fat/fat.c"

int main() {
    printf("Running FAT LFN Overflow Test...\n");

    // Canary buffer
    // lfn_buffer in fat.c callers is 256 bytes.
    // We will test fat_parse_lfn directly.

    char buffer[300];
    memset(buffer, 'A', sizeof(buffer));

    // Set canary at offset 256
    buffer[256] = 'Z';
    buffer[257] = 'Z';
    buffer[258] = 'Z';

    fat_lfn_t lfn;
    memset(&lfn, 0, sizeof(lfn));

    // 0x40 | 0x3F = 0x7F (Last entry, order 63)
    // index = ((63) - 1) * 13 = 62 * 13 = 806.
    lfn.order = 0x40 | 63;

    // Fill names with 'B'
    for(int i=0; i<5; i++) lfn.name1[i] = 'B';
    for(int i=0; i<6; i++) lfn.name2[i] = 'B';
    for(int i=0; i<2; i++) lfn.name3[i] = 'B';

    printf("Calling fat_parse_lfn with order=63 (idx ~806)...\n");
    // This should write to buffer[806]... buffer[818]
    // Our buffer is only 300 bytes.
    // However, if we pass a pointer to buffer, it will write relative to it.
    // If the function calculates idx=806, it writes to buffer[806].
    // Since buffer is on stack (or global), writing to buffer[806] will overwrite stack/other data.
    // To safely detect overflow without crashing (hopefully), we can use a larger buffer but check if it wrote past 256.

    // Actually, to prove vulnerability as described in the issue:
    // "The destination buffer is 256 bytes (in caller)."
    // So if I pass a buffer of 1024 bytes, and check if bytes > 256 are written, I confirm the bug.

    char big_buffer[2048];
    memset(big_buffer, 0, sizeof(big_buffer));

    // In actual code, we pass sizeof(buffer) - 1 which is 255.
    int idx = fat_parse_lfn(&lfn, big_buffer, 255);

    printf("Returned idx: %d\n", idx);

    if (idx == -1) {
        printf("SUCCESS: Detected overflow and returned error.\n");
    } else if (idx > 256) {
        printf("FAILURE: idx (%d) > 256, vulnerability still exists!\n", idx);
    } else {
         printf("Returned idx: %d (safe?)\n", idx);
    }

    // Check where it wrote
    if (big_buffer[806] == 'B') {
        printf("FAILURE: Wrote to index 806!\n");
    } else {
        printf("SUCCESS: Did not write to index 806.\n");
    }

    // Normal case test
    printf("\nTesting normal case (order=1)...\n");
    memset(big_buffer, 0, sizeof(big_buffer));
    lfn.order = 1;
    // Fill names with 'A'
    for(int i=0; i<5; i++) lfn.name1[i] = 'A';
    for(int i=0; i<6; i++) lfn.name2[i] = 'A';
    for(int i=0; i<2; i++) lfn.name3[i] = 'A';

    idx = fat_parse_lfn(&lfn, big_buffer, 255);
    printf("Returned idx: %d\n", idx);
    if (idx == 13) {
        printf("SUCCESS: Normal case returned correct index.\n");
    } else {
        printf("FAILURE: Normal case returned %d (expected 13)\n", idx);
    }

    return 0;
}
