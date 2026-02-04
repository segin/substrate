/*
 * test_udf_mkdir_leak.c - Reproduction test for UDF mkdir memory leak
 *
 * Compile with:
 * gcc -I. -Itests/sys/test_udf_mkdir_mocks -Isys tests/sys/test_udf_mkdir_leak.c -o test_leak
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

// Mock console
void kprint(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// Mock process
#include <sys/proc.h>
struct process *current_process = NULL;

// Mock dependencies
uint32_t mock_alloc_block_ret = 0;
int mock_create_fe_ret = 0;
int mock_add_fid_ret = 0;
int mock_free_block_called = 0;
uint32_t mock_free_block_arg = 0;

// Need vfs.h for types
#include <vfs/vfs.h>
#include <fs/udf/udf.h>

// Functions expected by udf.c (normally in udf_write.c)
uint32_t udf_alloc_block(void) {
    return mock_alloc_block_ret;
}

void udf_free_block(uint32_t block) {
    mock_free_block_called++;
    mock_free_block_arg = block;
    printf("MOCK: udf_free_block called with %d\n", block);
}

int udf_create_fe(fs_node_t *dev, uint32_t block, uint8_t file_type, uint32_t uid, uint32_t gid, uint32_t permissions) {
    printf("MOCK: udf_create_fe called. Returning %d\n", mock_create_fe_ret);
    return mock_create_fe_ret;
}

int udf_add_fid(fs_node_t *dev, struct udf_fe *dir_fe, uint32_t dir_block, const char *name, struct udf_long_ad *icb, uint8_t characteristics) {
    printf("MOCK: udf_add_fid called. Returning %d\n", mock_add_fid_ret);
    return mock_add_fid_ret;
}

int udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, uint32_t bitmap_loc, uint32_t bitmap_len) {
    return 0;
}

void vfs_register_filesystem(filesystem_t *fs) {
    (void)fs;
}

// Include source under test
#include "../../sys/fs/udf/udf.c"

void test_mkdir_leak_create_fe(void) {
    printf("Running test_mkdir_leak_create_fe...\n");

    mock_alloc_block_ret = 123;
    mock_create_fe_ret = -1; // Fail
    mock_add_fid_ret = 0;
    mock_free_block_called = 0;

    // Setup
    static udf_node_t parent_ctx;
    static struct udf_fs fs_ctx;

    memset(&parent_ctx, 0, sizeof(parent_ctx));
    memset(&fs_ctx, 0, sizeof(fs_ctx));

    parent_ctx.fs = &fs_ctx;

    fs_node_t parent;
    memset(&parent, 0, sizeof(parent));
    parent.impl = (uintptr_t)&parent_ctx;

    int ret = udf_vfs_mkdir(&parent, "test", 0755);

    if (ret != -1) {
        printf("FAILED: mkdir should return -1\n");
        exit(1);
    }

    if (mock_free_block_called == 0) {
        printf("FAILED: Leak detected. udf_free_block not called.\n");
        exit(1);
    } else {
        printf("SUCCESS: udf_free_block called.\n");
    }
}

void test_mkdir_leak_add_fid(void) {
    printf("Running test_mkdir_leak_add_fid...\n");

    mock_alloc_block_ret = 456;
    mock_create_fe_ret = 0; // Success
    mock_add_fid_ret = -1; // Fail
    mock_free_block_called = 0;

    // Setup
    static udf_node_t parent_ctx;
    static struct udf_fs fs_ctx;

    memset(&parent_ctx, 0, sizeof(parent_ctx));
    memset(&fs_ctx, 0, sizeof(fs_ctx));

    parent_ctx.fs = &fs_ctx;

    fs_node_t parent;
    memset(&parent, 0, sizeof(parent));
    parent.impl = (uintptr_t)&parent_ctx;

    int ret = udf_vfs_mkdir(&parent, "test", 0755);

    if (ret != -1) {
        printf("FAILED: mkdir should return -1\n");
        exit(1);
    }

    if (mock_free_block_called == 0) {
        printf("FAILED: Leak detected. udf_free_block not called.\n");
        exit(1);
    } else if (mock_free_block_arg != 456) {
        printf("FAILED: Wrong block freed. Expected 456, got %d\n", mock_free_block_arg);
        exit(1);
    } else {
        printf("SUCCESS: udf_free_block called.\n");
    }
}

int main(void) {
    test_mkdir_leak_create_fe();
    test_mkdir_leak_add_fid();
    return 0;
}
