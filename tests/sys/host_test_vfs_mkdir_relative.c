
#include <stddef.h>
#include <stdint.h>

// -----------------------------------------------------------------------------
// Host Libc Declarations (to avoid header conflicts)
// -----------------------------------------------------------------------------
extern int printf(const char *format, ...);
extern void *calloc(size_t nmemb, size_t size);
extern void free(void *ptr);
extern int strcmp(const char *s1, const char *s2);
extern char *strncpy(char *dest, const char *src, size_t n);
extern void *memset(void *s, int c, size_t n);

// -----------------------------------------------------------------------------
// Mocks for Kernel Environment
// -----------------------------------------------------------------------------

// Used by vfs_mount in vfs.c
#include <vfs/vfs.h>
fs_node_t *devfs_root_node_ptr = NULL;

void kprint(const char *s) {
    printf("%s", s);
}

void *kmalloc(size_t size) {
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    free(ptr);
}

// Mock other dependencies called by vfs.c
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

// To avoid including the entire kernel, we mock types and include vfs.c
// But vfs.c includes vfs.h, mount.h etc. We rely on include paths.

// We need to define current_process and its type.
#include <sys/proc.h>

process_t *current_process = NULL;
process_t mock_process;

// We need to define fs_root (it's defined in vfs.c, so we don't define it here, but we need to initialize it)

// -----------------------------------------------------------------------------
// Helper Mocks for FS Nodes
// -----------------------------------------------------------------------------

// We need to implement finddir and mkdir for our mock nodes.

typedef struct mock_fs_entry {
    char name[64];
    fs_node_t *node;
    struct mock_fs_entry *next;
} mock_fs_entry_t;

typedef struct mock_node_data {
    mock_fs_entry_t *children;
    char last_mkdir_name[64];
} mock_node_data_t;

fs_node_t *create_mock_node(int is_dir) {
    fs_node_t *node = calloc(1, sizeof(fs_node_t));
    node->flags = is_dir ? FS_DIRECTORY : FS_FILE;
    node->impl = (uintptr_t)calloc(1, sizeof(mock_node_data_t));
    return node;
}

fs_node_t *mock_finddir(fs_node_t *node, char *name) {
    mock_node_data_t *data = (mock_node_data_t *)(uintptr_t)node->impl;
    mock_fs_entry_t *entry = data->children;
    while (entry) {
        if (strcmp(entry->name, name) == 0) {
            return entry->node;
        }
        entry = entry->next;
    }
    return NULL;
}

int mock_mkdir(fs_node_t *node, const char *name, uint16_t permission) {
    mock_node_data_t *data = (mock_node_data_t *)(uintptr_t)node->impl;
    strncpy(data->last_mkdir_name, name, 63);
    // Determine if we should succeed? Always succeed for test verification.
    return 0;
}

void add_child(fs_node_t *parent, const char *name, fs_node_t *child) {
    mock_node_data_t *data = (mock_node_data_t *)(uintptr_t)parent->impl;
    mock_fs_entry_t *entry = calloc(1, sizeof(mock_fs_entry_t));
    strncpy(entry->name, name, 63);
    entry->node = child;
    entry->next = data->children;
    data->children = entry;
}

// -----------------------------------------------------------------------------
// Include Target
// -----------------------------------------------------------------------------

// We need to mock get_time for read_fs/write_fs in vfs.c (though we won't trigger them likely)
int64_t get_time(void) { return 0; }

// define weak symbols or stubs for other things if needed?
// vfs.c calls externs like devfs_root_node_ptr in vfs_mount. We won't call vfs_mount.

#include "../../sys/vfs/vfs.c"

// -----------------------------------------------------------------------------
// Test Logic
// -----------------------------------------------------------------------------

int main() {
    printf("Starting VFS mkdir relative path test...\n");

    // Setup FS Root
    fs_root = create_mock_node(1);
    fs_root->finddir = mock_finddir;
    fs_root->mkdir = mock_mkdir;

    // Setup CWD
    fs_node_t *cwd = create_mock_node(1);
    cwd->finddir = mock_finddir;
    cwd->mkdir = mock_mkdir;

    // Setup Process
    memset(&mock_process, 0, sizeof(mock_process));
    mock_process.cwd_node = cwd;
    current_process = &mock_process;

    // Create structure:
    // /root_only_dir
    // /cwd (not strictly in root for this test, detached graph is fine)
    //    /cwd_only_dir

    fs_node_t *root_only_dir = create_mock_node(1);
    root_only_dir->finddir = mock_finddir;
    root_only_dir->mkdir = mock_mkdir;
    add_child(fs_root, "root_only_dir", root_only_dir);

    fs_node_t *cwd_only_dir = create_mock_node(1);
    cwd_only_dir->finddir = mock_finddir;
    cwd_only_dir->mkdir = mock_mkdir;
    add_child(cwd, "cwd_only_dir", cwd_only_dir);

    // -------------------------------------------------------------------------
    // Test 1: Absolute Path (Should always succeed using fs_root)
    // -------------------------------------------------------------------------
    // mkdir("/root_only_dir/sub")
    printf("Test 1: Absolute Path...\n");
    int res = vfs_mkdir("/root_only_dir/sub", 0755);
    if (res == 0) {
        mock_node_data_t *data = (mock_node_data_t *)(uintptr_t)root_only_dir->impl;
        if (strcmp(data->last_mkdir_name, "sub") == 0) {
            printf("PASS: Absolute path creation succeeded.\n");
        } else {
            printf("FAIL: Absolute path creation name mismatch: %s\n", data->last_mkdir_name);
        }
    } else {
        printf("FAIL: Absolute path creation failed.\n");
    }

    // Clear state
    ((mock_node_data_t *)(uintptr_t)root_only_dir->impl)->last_mkdir_name[0] = '\0';

    // -------------------------------------------------------------------------
    // Test 2: Relative Path (Should use CWD)
    // -------------------------------------------------------------------------
    // mkdir("cwd_only_dir/sub")
    // Before fix: This uses fs_root. fs_root does NOT have "cwd_only_dir". Should fail.
    // After fix: This uses cwd. cwd HAS "cwd_only_dir". Should succeed.

    printf("Test 2: Relative Path (cwd_only_dir/sub)...\n");
    res = vfs_mkdir("cwd_only_dir/sub", 0755);
    if (res == 0) {
        mock_node_data_t *data = (mock_node_data_t *)(uintptr_t)cwd_only_dir->impl;
        if (strcmp(data->last_mkdir_name, "sub") == 0) {
            printf("PASS: Relative path creation succeeded (Correct behavior after fix).\n");
        } else {
            printf("FAIL: Relative path creation name mismatch: %s\n", data->last_mkdir_name);
        }
    } else {
        printf("FAIL: Relative path creation failed (Expected behavior BEFORE fix).\n");
    }

    // -------------------------------------------------------------------------
    // Test 3: Relative Path in Root (Should fail if using CWD)
    // -------------------------------------------------------------------------
    // mkdir("root_only_dir/sub2")
    // Before fix: Succeeds (uses fs_root).
    // After fix: Fails (uses cwd, which doesn't have root_only_dir).

    printf("Test 3: Relative Path but only in Root (root_only_dir/sub2)...\n");
    res = vfs_mkdir("root_only_dir/sub2", 0755);
    if (res == 0) {
        printf("FAIL: Creation succeeded (Expected behavior BEFORE fix, or if CWD accidentally has it).\n");
    } else {
        printf("PASS: Creation failed (Correct behavior after fix - relative path looked in CWD).\n");
    }

    // -------------------------------------------------------------------------
    // Test 4: Simple Name (No slashes)
    // -------------------------------------------------------------------------
    // mkdir("simple")
    // Before fix: Creates in fs_root.
    // After fix: Creates in CWD.

    printf("Test 4: Simple Name (mkdir 'simple')...\n");
    res = vfs_mkdir("simple", 0755);
    if (res == 0) {
        // Check where it was created
        mock_node_data_t *root_data = (mock_node_data_t *)(uintptr_t)fs_root->impl;
        mock_node_data_t *cwd_data = (mock_node_data_t *)(uintptr_t)cwd->impl;

        if (strcmp(root_data->last_mkdir_name, "simple") == 0) {
            printf("FAIL: Created in ROOT (Expected behavior BEFORE fix).\n");
        } else if (strcmp(cwd_data->last_mkdir_name, "simple") == 0) {
            printf("PASS: Created in CWD (Correct behavior after fix).\n");
        } else {
            printf("FAIL: Not created in root nor CWD?\n");
        }
    } else {
        printf("FAIL: mkdir 'simple' failed.\n");
    }

    return 0;
}
