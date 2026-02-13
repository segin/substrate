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
// Use mocks for sys/types.h (via -I tests/sys/vuln_mocks or similar if needed, but standard headers work for host tests usually)
#include <fs/udf/udf.h>
#include <vfs/vfs.h>
#include <vfs/vnode.h>

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

// Mock getnewvnode
struct mount;
int getnewvnode(const char *tag, struct mount *mp, struct vnodeops *vops, struct vnode **vpp) { return 0; }

struct process *current_process = NULL;

// Stub out externals that are not needed for finddir logic
// But keep udf_read_fe as we want to test finddir which calls it
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

// We need a way to intercept device reads to return valid FE for the found file
// udf_read_fe calls udf_read_tag which calls dev->read.
// So we mock dev->read.

static uint8_t mock_fe_buffer[2048]; // Buffer for a valid FE sector

// Mock read function for fs_node_t (device)
size_t mock_dev_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    // Only handle reading the FE for our test file
    // We assume offset/size match what udf_read_fe asks for
    if (size == 2048) {
        memcpy(buffer, mock_fe_buffer, 2048);
        return 2048;
    }
    return 0;
}

// Include the source
#include "../../sys/fs/udf/udf.c"

// Helper to calculate checksum (copied from udf.c logic since it's static there or we can use the one included)
// udf_tag_checksum is not static in udf.c, so we can use it.

// Helper to setup a valid FE in mock_fe_buffer
void setup_mock_fe() {
    memset(mock_fe_buffer, 0, 2048);
    struct udf_fe *fe = (struct udf_fe *)mock_fe_buffer;

    fe->tag.tag_id = UDF_TAG_FE;
    fe->tag.desc_version = 3;
    fe->tag.tag_serial = 1;
    fe->tag.desc_crc = 0;
    fe->tag.desc_crc_len = 0;
    fe->tag.tag_location = 100; // Match what we put in ICB

    // Set checksum
    fe->tag.tag_checksum = udf_tag_checksum(&fe->tag);

    // Set some attributes
    fe->icb_tag.file_type = UDF_FILETYPE_FILE;
    fe->info_length = 0; // Empty file
}

// Structure to hold context + inline data
struct my_node_context {
    udf_node_t ctx;
    uint8_t data[4096]; // Space for inline directory data
};

int main() {
    printf("Starting UDF FindDir Buffer Overflow Reproduction Test...\n");

    // Setup CRC table for checksum calculation
    udf_crc_init();

    // Prepare the mock FE that will be read when the file is found
    setup_mock_fe();

    // Create a mock device node
    fs_node_t mock_device;
    memset(&mock_device, 0, sizeof(mock_device));
    mock_device.read = mock_dev_read;

    // Setup filesystem context
    struct udf_fs fs;
    memset(&fs, 0, sizeof(fs));
    fs.device = &mock_device;
    fs.partition_start = 0;

    // Setup parent directory node context
    struct my_node_context my_ctx;
    memset(&my_ctx, 0, sizeof(my_ctx));

    udf_node_t *ctx = &my_ctx.ctx;
    ctx->fs = &fs;

    // Configure FE for INLINE data (directory content)
    ctx->fe.icb_tag.flags = UDF_ICB_FLAG_AD_INLINE;
    ctx->fe.ext_attr_length = 0;

    // Locate alloc_area for inline data
    uint8_t *alloc_area = ((uint8_t *)&ctx->fe) + sizeof(struct udf_fe) + ctx->fe.ext_attr_length;

    // Create a File Identifier Descriptor (FID) with a long name
    struct udf_fid *fid = (struct udf_fid *)alloc_area;
    int name_len = 200;

    fid->tag.tag_id = UDF_TAG_FID;
    fid->file_version = 1;
    fid->file_id_length = name_len;
    fid->characteristics = 0; // Regular file
    fid->impl_use_length = 0;

    // Set ICB to point to our mock FE
    fid->icb.block = 100; // matches tag_location in setup_mock_fe
    fid->icb.partition = 0;
    fid->icb.length = 2048;

    // Set the filename
    char *name_ptr = (char *)alloc_area + 38 + fid->impl_use_length;
    // Use 'A's
    memset(name_ptr, 'A', name_len);
    // Mark as compressed unicode (8 bit), prefix with 8
    name_ptr[0] = 8;
    // The rest are characters.

    // Also construct the search string (null terminated C string)
    char search_name[256];
    memcpy(search_name, name_ptr + 1, name_len - 1); // Skip compression byte
    search_name[name_len - 1] = '\0';

    // Calculate FID size
    uint32_t fid_size = 38 + fid->impl_use_length + fid->file_id_length;
    fid_size = (fid_size + 3) & ~3;

    // Set info_length (directory size)
    ctx->fe.info_length = fid_size;

    // Create the parent directory node
    fs_node_t dir_node;
    memset(&dir_node, 0, sizeof(dir_node));
    dir_node.impl = (uintptr_t)ctx;

    printf("Searching for name of length %ld...\n", strlen(search_name));

    // We want to detect overflow in udf_alloc_node's result.
    // udf_alloc_node uses a static cache array: udf_fs_node_cache.
    // We can inspect the cache directly since we included udf.c.
    // We need to know which index will be used.
    // udf_node_cache_idx starts at 0.
    int idx = udf_node_cache_idx % UDF_NODE_CACHE_SIZE;
    fs_node_t *target_node = &udf_fs_node_cache[idx];

    // Fill the target node with a known pattern to detect overflow
    // (Note: udf_alloc_node clears it with memset(0), but the name buffer is at the beginning usually)
    // Wait, fs_node_t layout: name is first member (128 bytes).
    // If we write 200 bytes to name, we overwrite subsequent members: mask, uid, gid, flags, inode, length...

    // We can check if `length` or `inode` or `impl` gets corrupted.
    // Or we can place a guard variable after the cache if we could control layout, but we can't easily.
    // But since it's in a static array, we can check the *next* element in the array if it overflows?
    // fs_node_t is likely larger than 200 bytes.
    // sizeof(fs_node_t) is roughly 128 + 4*4 + 8 + 8 + 8 + ... = ~200+ bytes?
    // Let's check sizeof(fs_node_t).

    printf("sizeof(fs_node_t): %zu\n", sizeof(fs_node_t));

    // If sizeof(fs_node_t) > 200, the overflow is contained within the struct, corrupting its members.
    // We can check if members like `uid`, `gid`, `flags` are overwritten.
    // udf_alloc_node sets them:
    // node->length = fe->info_length; (0 in our mock FE)
    // node->uid = fe->uid; (0)
    // node->gid = fe->gid; (0)

    // 'A' is 0x41.
    // If we overwrite `uid` (uint32_t) with 0x41414141, we can detect it.

    // Call finddir
    fs_node_t *result = udf_vfs_finddir(&dir_node, search_name);

    if (result == NULL) {
        printf("finddir returned NULL! Should have found entry.\n");
        return 1;
    }

    printf("finddir returned node at %p\n", result);
    printf("Node name: %s\n", result->name);

    // Check for corruption
    // name is 128 bytes.
    // The next field is 'uint32_t mask'.
    // If we wrote 200 bytes, we overwrote 72 bytes past name.
    // mask, uid, gid, flags, inode, length, rdev, impl ...

    printf("Node mask: 0x%08X\n", result->mask);
    printf("Node uid:  0x%08X\n", result->uid);
    printf("Node gid:  0x%08X\n", result->gid);

    if (result->uid == 0x41414141) {
        printf("FAILURE: Node UID corrupted with 0x41414141! Overflow detected.\n");
        return 1;
    }

    // Also check if name is properly null terminated if we fixed it
    if (result->name[127] != '\0') {
         // If it's not null terminated, it might be an issue too, but here we expect truncation with null term.
         // With strcpy, if source is long, it writes past end.
         // If we fix it, it should be truncated.
    }

    if (strlen(result->name) > 127) {
        printf("FAILURE: Name length > 127! (Should be impossible if null terminated within buffer)\n");
    }

    printf("SUCCESS: No corruption detected.\n");
    return 0;
}
