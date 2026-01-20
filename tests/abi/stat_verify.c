#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stddef.h>

#define EXPECT_SIZE(type, size) \
    if (sizeof(type) != size) { \
        printf("FAIL: sizeof(" #type ") = %d, expected %d\n", (int)sizeof(type), size); \
        failed++; \
    } else { \
        printf("PASS: sizeof(" #type ") = %d\n", (int)sizeof(type)); \
    }

#define EXPECT_OFFSET(struct_type, field, offset) \
    if (offsetof(struct_type, field) != offset) { \
        printf("FAIL: offsetof(" #struct_type ", " #field ") = %d, expected %d\n", (int)offsetof(struct_type, field), offset); \
        failed++; \
    } else { \
        printf("PASS: offsetof(" #struct_type ", " #field ") = %d\n", (int)offsetof(struct_type, field)); \
    }

int main() {
    int failed = 0;

    printf("Verifying Native 64-bit Stat ABI...\n");

    // Verify Type Sizes
    EXPECT_SIZE(off_t, 8);
    EXPECT_SIZE(time_t, 8);
    EXPECT_SIZE(blkcnt_t, 8);
    EXPECT_SIZE(struct stat, 92);

    // Verify Offsets (based on ABI doc)
    EXPECT_OFFSET(struct stat, st_dev, 0);
    EXPECT_OFFSET(struct stat, st_ino, 4);
    EXPECT_OFFSET(struct stat, st_mode, 8);
    EXPECT_OFFSET(struct stat, st_nlink, 10);
    EXPECT_OFFSET(struct stat, st_uid, 12);
    EXPECT_OFFSET(struct stat, st_gid, 14);
    EXPECT_OFFSET(struct stat, st_rdev, 16);
    EXPECT_OFFSET(struct stat, st_size, 20);
    EXPECT_OFFSET(struct stat, st_blksize, 28);
    EXPECT_OFFSET(struct stat, st_blocks, 36);
    EXPECT_OFFSET(struct stat, st_atime, 44);
    EXPECT_OFFSET(struct stat, st_mtime, 60);
    EXPECT_OFFSET(struct stat, st_ctime, 76);

    if (failed) {
        printf("\nFAILED: %d discrepancies found.\n", failed);
        return 1;
    }

    printf("\nSUCCESS: ABI layout matches expectations.\n");
    return 0;
}
