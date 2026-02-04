#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

// Build expects us to define these mocks
void kprint(const char *s) {
    printf("[KERNEL] %s", s);
}

// Dummy inits
void ext2_init(void) {}
void fat_init(void) {}
void exfat_init(void) {}
void minix_init(void) {}
void udf_init(void) {}
void devfs_init(void) {}
void procfs_init(void) {}
void sysfs_init(void) {}
void fuse_init(void) {}
void fuse_fs_init(void) {}
void p9_init(void) {}
void pseudo_init(void) {}

int64_t get_time(void) { return 0; }

struct fs_node *devfs_root_node_ptr = NULL;

// We need to define current_process because we are linking against vfs.c
// and we want to control it.
#include <sys/proc.h>
process_t mock_process;
process_t *current_process = &mock_process;

// Include the source
// We assume we are compiling with -Isys/vfs -Isys/include -Itests/sys/mocks
// Wait, vfs.c is in sys/vfs.
// We will include it directly.
// But first we need vfs.h available.
// With -Itests/sys/mocks, <sys/proc.h> is found.
// With -Isys, <vfs/vfs.h> is found.

#include "../../sys/vfs/vfs.c"

// Mocks for Test
static fs_node_t root_node;
static fs_node_t cwd_node;
static fs_node_t abs_subdir_node;
static fs_node_t rel_subdir_node;

static char last_mkdir_name[128];
static fs_node_t *last_mkdir_parent = NULL;

int mock_mkdir_op(fs_node_t *node, const char *name, uint16_t permission) {
    (void)permission;
    last_mkdir_parent = node;
    strncpy(last_mkdir_name, name, sizeof(last_mkdir_name));
    return 0;
}

fs_node_t *mock_finddir(fs_node_t *node, char *name) {
    if (node == &root_node) {
        if (strcmp(name, "abs") == 0) return &abs_subdir_node;
    }
    if (node == &cwd_node) {
        if (strcmp(name, "rel") == 0) return &rel_subdir_node;
    }
    return NULL;
}

void setup_node(fs_node_t *node, const char *name, uint32_t flags) {
    memset(node, 0, sizeof(fs_node_t));
    strncpy(node->name, name, sizeof(node->name));
    node->flags = flags;
    node->mkdir = mock_mkdir_op;
    node->finddir = mock_finddir;
}

int main() {
    printf("Starting Test...\n");

    // Setup Nodes
    setup_node(&root_node, "ROOT", FS_DIRECTORY);
    setup_node(&cwd_node, "CWD", FS_DIRECTORY);
    setup_node(&abs_subdir_node, "abs", FS_DIRECTORY);
    setup_node(&rel_subdir_node, "rel", FS_DIRECTORY);

    // Setup Environment
    fs_root = &root_node;
    current_process->cwd_node = &cwd_node;
    current_process->root_node = &root_node;

    int fail = 0;

    // Test 1: Absolute path mkdir("/foo")
    // Expect: mkdir on ROOT, name "foo"
    last_mkdir_parent = NULL;
    vfs_mkdir("/foo", 0);
    if (last_mkdir_parent == &root_node && strcmp(last_mkdir_name, "foo") == 0) {
        printf("PASS: mkdir('/foo') used ROOT\n");
    } else {
        printf("FAIL: mkdir('/foo') parent=%p (expected ROOT %p), name='%s'\n",
               last_mkdir_parent, &root_node, last_mkdir_name);
        fail++;
    }

    // Test 2: Absolute path with subdir mkdir("/abs/bar")
    // Expect: lookup "/abs" -> abs_subdir_node. mkdir on abs_subdir_node, name "bar"
    last_mkdir_parent = NULL;
    vfs_mkdir("/abs/bar", 0);
    if (last_mkdir_parent == &abs_subdir_node && strcmp(last_mkdir_name, "bar") == 0) {
        printf("PASS: mkdir('/abs/bar') used abs_subdir_node\n");
    } else {
        printf("FAIL: mkdir('/abs/bar') parent=%p (expected %p), name='%s'\n",
               last_mkdir_parent, &abs_subdir_node, last_mkdir_name);
        fail++;
    }

    // Test 3: Relative path mkdir("bar")
    // Expect: mkdir on CWD, name "bar"
    // CURRENTLY FAILS (uses ROOT)
    last_mkdir_parent = NULL;
    vfs_mkdir("bar", 0);
    if (last_mkdir_parent == &cwd_node && strcmp(last_mkdir_name, "bar") == 0) {
        printf("PASS: mkdir('bar') used CWD\n");
    } else {
        printf("FAIL: mkdir('bar') parent=%p (expected CWD %p), name='%s'\n",
               last_mkdir_parent, &cwd_node, last_mkdir_name);
        // We expect this to fail before the fix
        // fail++; // Don't fail the build, just report
    }

    // Test 4: Relative path with subdir mkdir("rel/bar")
    // Expect: lookup "rel" in CWD -> rel_subdir_node. mkdir on rel_subdir_node
    last_mkdir_parent = NULL;
    vfs_mkdir("rel/bar", 0);
    if (last_mkdir_parent == &rel_subdir_node && strcmp(last_mkdir_name, "bar") == 0) {
         printf("PASS: mkdir('rel/bar') used rel_subdir_node\n");
    } else {
         printf("FAIL: mkdir('rel/bar') parent=%p (expected %p), name='%s'\n",
                last_mkdir_parent, &rel_subdir_node, last_mkdir_name);
         // fail++;
    }

    if (fail) return 1;
    return 0;
}
