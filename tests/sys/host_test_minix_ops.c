/*
 * host_test_minix_ops.c — drive the real Minix driver over images made by
 * mkfs.minix, then let fsck.minix judge what it wrote.
 *
 * One pass per on-disk variant (V1/V2 x 14/30-character names).  Each pass:
 *   - mounts, and finds "." and ".." in the root (the directory stride and
 *     name length must match the variant: the magic numbers were swapped, so
 *     a 14-char volume was read with a 32-byte stride and vice versa);
 *   - makes a symlink and reads it back (the op took (name, target) where the
 *     VFS passes (target, name));
 *   - makes a FIFO and a character device and checks their types (the type
 *     bits were tested as flags, so a FIFO was written as a regular file and
 *     a symlink as a directory);
 *   - makes a name longer than 14 characters on a 30-char volume;
 *   - looks the same name up twice and expects one node (lookups used to
 *     allocate a node every time and nothing freed them);
 *   - assembles a subdirectory, refuses to unlink it, and rmdirs it (rmdir
 *     went through unlink, which refuses every directory);
 *   - unmounts, saves the image and runs fsck.minix -f on it.
 *
 * Needs mkfs.minix and fsck.minix (util-linux); skips if they are missing.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>

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

/* See host_test_minix_vuln.c: substrate's <sys/mount.h> must win over the
 * host's. */
#include "../../sys/include/sys/mount.h"
#include <vfs/vfs.h>

static uint8_t *disk;
static size_t disk_size;

ssize_t read_fs(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset >= disk_size) return 0;
    if (size > disk_size - (size_t)offset) size = disk_size - (size_t)offset;
    memcpy(buffer, disk + offset, size);
    return (ssize_t)size;
}

ssize_t write_fs(fs_node_t *node, off_t offset, size_t size,
                 const uint8_t *buffer) {
    (void)node;
    if (offset < 0 || (size_t)offset >= disk_size) return 0;
    if (size > disk_size - (size_t)offset) size = disk_size - (size_t)offset;
    memcpy(disk + offset, buffer, size);
    return (ssize_t)size;
}

void vfs_register_filesystem(filesystem_t *fs) { (void)fs; }

#include <sys/lock.h>
void spinlock_acquire(spinlock_t *lock) { (void)lock; }
void spinlock_release(spinlock_t *lock) { (void)lock; }

#include "../../sys/fs/minix/minix.c"

int64_t get_time(void) {
    return 1234567890;
}

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            printf("  FAIL: " __VA_ARGS__);                     \
            printf("\n");                                       \
            failures++;                                         \
        }                                                       \
    } while (0)

static int has_entry(fs_node_t *dir, const char *name) {
    for (uint64_t i = 0;; i++) {
        struct dirent *d = minix_readdir(dir, i);
        if (!d) return 0;
        if (strcmp(d->d_name, name) == 0) return 1;
    }
}

/* Link count of a node's cached raw inode, for either variant. */
static void add_links(minix_fs_t *fs, fs_node_t *node, int delta) {
    struct minix_inode_wrapper *w = (struct minix_inode_wrapper *)node->ptr;
    if (fs->sb.s_magic == MINIX_V2_Magic || fs->sb.s_magic == MINIX_V2_Magic_14)
        w->inode.v2.i_nlinks += delta;
    else
        w->inode.v1.i_nlinks += delta;
    minix_write_inode(fs, node);
}

static int run_fsck(const char *img) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "fsck.minix -f %s >/dev/null 2>&1", img);
    return system(cmd);
}

static void one_variant(const char *label, const char *mkfs_args,
                        int long_names) {
    char img[] = "/tmp/minix_ops_XXXXXX";
    char cmd[512];
    int fd = mkstemp(img);
    if (fd < 0) { perror("mkstemp"); exit(1); }
    close(fd);

    printf("%s\n", label);
    snprintf(cmd, sizeof(cmd),
             "dd if=/dev/zero of=%s bs=1024 count=1440 2>/dev/null && "
             "mkfs.minix %s %s >/dev/null 2>&1", img, mkfs_args, img);
    if (system(cmd) != 0) {
        printf("  FAIL: mkfs.minix %s\n", mkfs_args);
        failures++;
        unlink(img);
        return;
    }

    FILE *f = fopen(img, "rb");
    fseek(f, 0, SEEK_END);
    disk_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    disk = malloc(disk_size);
    if (fread(disk, 1, disk_size, f) != disk_size) { perror("fread"); exit(1); }
    fclose(f);

    fs_node_t blk;
    memset(&blk, 0, sizeof(blk));
    fs_node_t *root = minix_mount("disk", 0, &blk);
    CHECK(root != NULL, "mount");
    if (!root) goto out;
    minix_fs_t *fs = (minix_fs_t *)(uintptr_t)root->impl;

    CHECK(has_entry(root, ".") && has_entry(root, ".."),
          "root has no \".\"/\"..\" (directory stride wrong for the variant)");

    /* Symlink: name and target the right way round. */
    CHECK(minix_symlink(root, "/some/where/else", "lnk") == 0, "symlink");
    fs_node_t *ln = minix_finddir(root, "lnk");
    CHECK(ln != NULL, "symlink not found under its name");
    CHECK(minix_finddir(root, "/some/where/else") == NULL,
          "symlink created under its target's name");
    if (ln) {
        char buf[64];
        memset(buf, 0, sizeof(buf));
        CHECK((ln->flags & 0x7) == FS_SYMLINK, "symlink type is %u",
              ln->flags & 0x7);
        int n = minix_readlink(ln, buf, sizeof(buf) - 1);
        CHECK(n == 16 && strcmp(buf, "/some/where/else") == 0,
              "readlink gave %d \"%s\"", n, buf);
    }

    /* Node types are values, not flags. */
    CHECK(minix_mknod(root, "fifo", S_IFIFO | 0644, 0) == 0, "mknod fifo");
    CHECK(minix_mknod(root, "chr", S_IFCHR | 0600, (1 << 8) | 3) == 0,
          "mknod chr");
    fs_node_t *fifo = minix_finddir(root, "fifo");
    fs_node_t *chr = minix_finddir(root, "chr");
    CHECK(fifo && (fifo->flags & 0x7) == FS_PIPE, "fifo type is %u",
          fifo ? fifo->flags & 0x7 : 99);
    CHECK(chr && (chr->flags & 0x7) == FS_CHARDEVICE, "chr type is %u",
          chr ? chr->flags & 0x7 : 99);

    /* One node per inode, however often it is looked up. */
    CHECK(minix_finddir(root, "fifo") == fifo, "second lookup made a new node");

    if (long_names) {
        const char *longname = "a_name_of_twenty_six_chars";
        CHECK(minix_mknod(root, longname, S_IFIFO | 0644, 0) == 0,
              "mknod long name");
        CHECK(has_entry(root, longname), "long name cut short");
    }

    /* A subdirectory, assembled as mkdir would: entry, ".", "..", links. */
    CHECK(minix_mknod(root, "sub", S_IFDIR | 0755, 0) == 0, "mknod dir");
    fs_node_t *sub = minix_finddir(root, "sub");
    CHECK(sub != NULL, "dir not found");
    if (sub) {
        CHECK(minix_dir_add(sub, ".", (uint32_t)sub->inode) == 0, "add .");
        CHECK(minix_dir_add(sub, "..", (uint32_t)root->inode) == 0, "add ..");
        add_links(fs, sub, 1);
        add_links(fs, root, 1);
    }

    /* Unmount and check what is on disk so far. */
    minix_unmount(root);
    f = fopen(img, "wb");
    fwrite(disk, 1, disk_size, f);
    fclose(f);
    CHECK(run_fsck(img) == 0, "fsck.minix after create");

    root = minix_mount("disk", 0, &blk);
    CHECK(root != NULL, "remount");
    if (!root) goto out;
    CHECK(minix_unlink(root, "sub") == -EISDIR, "unlink of a directory");
    CHECK(minix_unlink(root, "lnk") == 0, "unlink symlink");
    CHECK(minix_rmdir(root, "fifo") == -ENOTDIR, "rmdir of a fifo");
    CHECK(minix_rmdir(root, "sub") == 0, "rmdir");
    CHECK(!has_entry(root, "sub"), "sub still listed");
    minix_unmount(root);

    f = fopen(img, "wb");
    fwrite(disk, 1, disk_size, f);
    fclose(f);
    CHECK(run_fsck(img) == 0, "fsck.minix after remove");

out:
    free(disk);
    disk = NULL;
    unlink(img);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (system("command -v mkfs.minix >/dev/null && "
               "command -v fsck.minix >/dev/null") != 0) {
        printf("SKIP: mkfs.minix/fsck.minix not installed\n");
        return 0;
    }
    one_variant("V1, 14-char names", "-1 -n 14", 0);
    one_variant("V1, 30-char names", "-1 -n 30", 1);
    one_variant("V2, 14-char names", "-2 -n 14", 0);
    one_variant("V2, 30-char names", "-2 -n 30", 1);
    if (failures) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
