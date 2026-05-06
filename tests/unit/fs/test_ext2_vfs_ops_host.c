#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/lock.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

void kprint(const char *msg) { (void)msg; }
void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }
int64_t get_time(void) { return 123456789; }

void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

void mutex_init(mutex_t *m, const char *name) { m->locked = 0; m->name = name; }
void mutex_lock(mutex_t *m) { m->locked = 1; }
void mutex_unlock(mutex_t *m) { m->locked = 0; }

#define vasprintf kernel_vasprintf
#include "../../../sys/fs/ext2/ext2.c"

uma_zone_t *uma_zcreate(const char *name, size_t size, uma_ctor ctor, uma_dtor dtor,
                        uma_init init, uma_fini fini, int align, uint32_t flags)
{
    (void)ctor; (void)dtor; (void)init; (void)fini; (void)align; (void)flags;
    uma_zone_t *zone = calloc(1, sizeof(uma_zone_t));
    if (!zone) return NULL;
    zone->uz_name = name;
    zone->uz_size = size;
    return zone;
}

void uma_zdestroy(uma_zone_t *zone) { free(zone); }
void *uma_zalloc(uma_zone_t *zone, int flags) {
    (void)flags;
    return zone ? calloc(1, zone->uz_size > 0 ? zone->uz_size : 4096) : NULL;
}
void uma_zfree(uma_zone_t *zone, void *item) { (void)zone; free(item); }

#define BLOCKS_COUNT 8192
#define INODES_PER_GROUP 256
#define ROOT_INO 2
#define ROOT_BLOCK 100
#define BLOCK_BITMAP_BLOCK 3
#define INODE_BITMAP_BLOCK 4
#define INODE_TABLE_BLOCK 5

static uint8_t mock_disk[BLOCKS_COUNT * 1024];

static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer)
{
    (void)node;
    if ((size_t)offset + size > sizeof(mock_disk)) return 0;
    memcpy(buffer, mock_disk + offset, size);
    return size;
}

static size_t mock_write(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer)
{
    (void)node;
    if ((size_t)offset + size > sizeof(mock_disk)) return 0;
    memcpy(mock_disk + offset, buffer, size);
    return size;
}

static void mark_block_used(uint32_t block_num)
{
    uint8_t *bitmap = mock_disk + BLOCK_BITMAP_BLOCK * 1024;
    uint32_t bit = block_num - 1;
    bitmap[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static void init_empty_directory_block(uint32_t self_ino, uint32_t parent_ino)
{
    uint8_t *block = mock_disk + ROOT_BLOCK * 1024;
    uint16_t dot_len = (uint16_t)(((8 + 1 + 3) / 4) * 4);
    ext2_dirent_t *dot;
    ext2_dirent_t *dotdot;

    memset(block, 0, 1024);
    dot = (ext2_dirent_t *)block;
    dotdot = (ext2_dirent_t *)(block + dot_len);

    dot->inode = self_ino;
    dot->rec_len = dot_len;
    dot->name_len = 1;
    dot->file_type = EXT2_FT_DIR;
    dot->name[0] = '.';

    dotdot->inode = parent_ino;
    dotdot->rec_len = (uint16_t)(1024 - dot_len);
    dotdot->name_len = 2;
    dotdot->file_type = EXT2_FT_DIR;
    dotdot->name[0] = '.';
    dotdot->name[1] = '.';
}

static void setup_fs(ext2_fs_t *fs, fs_node_t *dev_node, ext2_node_t *root_ctx, fs_node_t *root)
{
    ext2_superblock_t *sb = (ext2_superblock_t *)(mock_disk + 1024);
    ext2_group_desc_t *bgd_disk = (ext2_group_desc_t *)(mock_disk + 2048);
    ext2_inode_t root_inode;

    memset(mock_disk, 0, sizeof(mock_disk));
    memset(fs, 0, sizeof(*fs));
    memset(dev_node, 0, sizeof(*dev_node));
    memset(root_ctx, 0, sizeof(*root_ctx));
    memset(root, 0, sizeof(*root));

    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_blocks_count = BLOCKS_COUNT;
    sb->s_log_block_size = 0;
    sb->s_blocks_per_group = BLOCKS_COUNT;
    sb->s_inodes_per_group = INODES_PER_GROUP;
    sb->s_first_data_block = 1;
    sb->s_rev_level = 1;
    sb->s_inode_size = 128;
    sb->s_first_ino = 11;
    sb->s_free_blocks_count = BLOCKS_COUNT - 128;
    sb->s_free_inodes_count = INODES_PER_GROUP - 1;

    bgd_disk->bg_block_bitmap = BLOCK_BITMAP_BLOCK;
    bgd_disk->bg_inode_bitmap = INODE_BITMAP_BLOCK;
    bgd_disk->bg_inode_table = INODE_TABLE_BLOCK;
    bgd_disk->bg_free_blocks_count = BLOCKS_COUNT - 128;
    bgd_disk->bg_free_inodes_count = INODES_PER_GROUP - 1;
    bgd_disk->bg_used_dirs_count = 1;

    for (uint32_t block = 1; block < 128; block++) {
        mark_block_used(block);
    }

    dev_node->read = mock_read;
    dev_node->write = mock_write;

    fs->device = dev_node;
    fs->sb = *sb;
    fs->block_size = 1024;
    fs->group_count = 1;
    fs->blocks_per_group = BLOCKS_COUNT;
    fs->inodes_per_group = INODES_PER_GROUP;
    fs->inode_size = 128;
    fs->bgd = calloc(1, sizeof(ext2_group_desc_t));
    fs->bgd[0] = *bgd_disk;
    fs->active_bg_bitmap = calloc(1, fs->block_size);
    fs->active_inode_bg_bitmap = calloc(1, fs->block_size);
    fs->active_bg_group = (uint32_t)-1;
    fs->active_inode_bg_group = (uint32_t)-1;

    memset(ext2_node_cache, 0, sizeof(ext2_node_cache));
    memset(ext2_fs_node_cache, 0, sizeof(ext2_fs_node_cache));
    ext2_node_cache_idx = 0;

    memset(&root_inode, 0, sizeof(root_inode));
    root_inode.i_mode = EXT2_S_IFDIR | 0755;
    root_inode.i_uid = 0;
    root_inode.i_gid = 0;
    root_inode.i_size = 1024;
    root_inode.i_links_count = 2;
    root_inode.i_blocks = 2;
    root_inode.i_block[0] = ROOT_BLOCK;
    root_inode.i_atime = root_inode.i_mtime = root_inode.i_ctime = (uint32_t)get_time();

    init_empty_directory_block(ROOT_INO, ROOT_INO);
    ext2_write_inode(fs, ROOT_INO, &root_inode);

    root_ctx->fs = fs;
    root_ctx->inode_num = ROOT_INO;
    root_ctx->inode = root_inode;
    mutex_init(&root_ctx->lock, "ext2_root_lock");

    root->flags = FS_DIRECTORY;
    root->mask = 0755;
    root->uid = 0;
    root->gid = 0;
    root->impl = (uintptr_t)root_ctx;
    root->finddir = ext2_finddir;
    root->mkdir = ext2_mkdir;
    root->unlink = ext2_unlink;
    root->rmdir = ext2_rmdir;

    ext2_init();
}

static bool test_create_unlink_round_trip(void)
{
    ext2_fs_t fs;
    fs_node_t dev_node;
    ext2_node_t root_ctx;
    fs_node_t root;
    fs_node_t *created;

    setup_fs(&fs, &dev_node, &root_ctx, &root);

    if (ext2_mknod(&root, "file", S_IFREG | 0644, 0) != 0)
        return false;

    created = ext2_finddir(&root, "file");
    if (!created || (created->flags & 0x7) != FS_FILE)
        return false;

    if (ext2_unlink(&root, "file") != 0)
        return false;

    if (ext2_finddir(&root, "file") != NULL)
        return false;

    free(fs.active_bg_bitmap);
    free(fs.active_inode_bg_bitmap);
    free(fs.bgd);
    return true;
}

static bool test_mkdir_rmdir_round_trip(void)
{
    ext2_fs_t fs;
    fs_node_t dev_node;
    ext2_node_t root_ctx;
    fs_node_t root;
    fs_node_t *created_dir;

    setup_fs(&fs, &dev_node, &root_ctx, &root);

    if (ext2_mkdir(&root, "dir", 0755) != 0)
        return false;

    created_dir = ext2_finddir(&root, "dir");
    if (!created_dir || (created_dir->flags & 0x7) != FS_DIRECTORY)
        return false;

    if (ext2_rmdir(&root, "dir") != 0)
        return false;

    if (ext2_finddir(&root, "dir") != NULL)
        return false;

    free(fs.active_bg_bitmap);
    free(fs.active_inode_bg_bitmap);
    free(fs.bgd);
    return true;
}

int main(void)
{
    if (!test_create_unlink_round_trip()) {
        printf("FAIL: ext2 create/unlink round-trip\n");
        return 1;
    }

    if (!test_mkdir_rmdir_round_trip()) {
        printf("FAIL: ext2 mkdir/rmdir round-trip\n");
        return 1;
    }

    printf("PASS: ext2 create/unlink/mkdir/rmdir round-trip\n");
    return 0;
}