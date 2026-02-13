#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>

// Mock kprint
void kprint(const char *str) {
    // printf("kprint: %s", str);
}

// Mock kmalloc/free
void *kmalloc(size_t size) { return calloc(1, size); }
void *kzalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

// Include UDF and VFS headers to get types
// Use mocks for sys/types.h (via -I tests/sys/vuln_mocks)
#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Mock getnewvnode
struct mount;
int getnewvnode(const char *tag, struct mount *mp, struct vnodeops *vops, struct vnode **vpp) { return 0; }

struct process *current_process = NULL;

// Stub out externals
#define udf_read_space_bitmap mock_udf_read_space_bitmap
#define udf_alloc_block mock_udf_alloc_block
#define udf_free_block mock_udf_free_block
#define udf_create_fe mock_udf_create_fe
#define udf_add_fid mock_udf_add_fid

int mock_udf_read_space_bitmap(fs_node_t *dev, uint32_t partition_start, uint32_t bitmap_loc, uint32_t bitmap_len) { return 0; }
uint32_t mock_udf_alloc_block(void) { return 0; }
void mock_udf_free_block(uint32_t block) { }
int mock_udf_create_fe(fs_node_t *dev, uint32_t block, uint8_t file_type, uint32_t uid, uint32_t gid, uint32_t permissions) { return 0; }
int mock_udf_add_fid(fs_node_t *dev, struct udf_fe *dir_fe, uint32_t dir_block, const char *name, struct udf_long_ad *icb, uint8_t characteristics) { return 0; }

// Include the source
#include "../../sys/fs/udf/udf.c"

// Structure to hold context + inline data
struct my_node_context {
    udf_node_t ctx;
    uint8_t data[4096];
};

int main() {
    printf("Starting UDF Buffer Overflow Reproduction Test...\n");

    // Initialize the root node with a pattern to detect corruption
    memset(&udf_root, 0x55, sizeof(fs_node_t));
    unsigned char *root_bytes = (unsigned char *)&udf_root;

    printf("Address of udf_dirent.name: %p\n", udf_dirent.name);
    printf("Address of udf_root:        %p\n", &udf_root);
    printf("Offset:                     %ld\n", (long)((char*)&udf_root - (char*)udf_dirent.name));

    // Setup node context
    struct my_node_context my_ctx;
    memset(&my_ctx, 0, sizeof(my_ctx));

    udf_node_t *ctx = &my_ctx.ctx;

    // Configure FE for INLINE data
    ctx->fe.icb_tag.flags = UDF_ICB_FLAG_AD_INLINE;
    ctx->fe.ext_attr_length = 0;

    // Locate alloc_area
    uint8_t *alloc_area = ((uint8_t *)&ctx->fe) + sizeof(struct udf_fe) + ctx->fe.ext_attr_length;

    // Create the malicious FID in the alloc area
    struct udf_fid *fid = (struct udf_fid *)alloc_area;
    int name_len = 200;

    fid->tag.tag_id = UDF_TAG_FID;
    fid->file_version = 1;
    fid->file_id_length = name_len;
    fid->characteristics = 0;
    fid->impl_use_length = 0;

    char *name_ptr = (char *)alloc_area + 38 + fid->impl_use_length;
    memset(name_ptr, 'A', name_len);
    // Mark compressed unicode (8)
    name_ptr[0] = 8;

    uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
    fid_size = (fid_size + 3) & ~3;

    // Set info_length (directory size)
    ctx->fe.info_length = fid_size;

    printf("FID Size: %d\n", fid_size);
    printf("Name Length: %d\n", name_len);

    // Setup dummy fs_node to pass to readdir
    fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.impl = (uintptr_t)ctx;

    printf("udf_root byte 0 before: %02X\n", root_bytes[0]);

    // Call readdir
    printf("Calling udf_vfs_readdir...\n");
    struct dirent *d = udf_vfs_readdir(&node, 0);

    if (d == NULL) {
        printf("readdir returned NULL! Directory empty or error.\n");
    } else {
        printf("readdir returned entry: %s\n", d->name);
    }

    // Check for corruption
    printf("udf_root byte 0 after:  %02X\n", root_bytes[0]);

    if (root_bytes[0] == 0x41) { // 'A'
        printf("FAILURE: udf_root was corrupted! Vulnerability reproduced.\n");
        return 1;
    } else if (root_bytes[0] == 0x55) {
        printf("SUCCESS: udf_root was NOT corrupted.\n");
        return 0;
    } else {
        printf("UNKNOWN: udf_root changed to %02X\n", root_bytes[0]);
        return 1;
    }
}
