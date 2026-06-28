#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <sys/errno.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <vfs/vfs.h>

process_t *current_process;
thread_t *current_thread;
fs_node_t *fs_root;

static process_t proc;
static fs_node_t root_node;
static fs_node_t cwd_node;
static fs_node_t found_node;
static int vfs_perm_result;
static const char *last_lookup_path;
static fs_node_t *last_lookup_root;

int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    size_t n = strlen((const char *)src) + 1;

    if (n > maxlen) {
        return -1;
    }

    memcpy(dst, src, n);
    if (len) {
        *len = n;
    }
    return 0;
}

fs_node_t *vfs_lookup(fs_node_t *root, const char *path) {
    last_lookup_root = root;
    last_lookup_path = path;
    if (strcmp(path, "/missing") == 0 || strcmp(path, "missing") == 0) {
        return NULL;
    }
    return &found_node;
}

fs_node_t *vfs_lookup_lstat(fs_node_t *root, const char *path) {
    (void)root;
    (void)path;
    return &found_node;
}

/*
 * kern_access now resolves through vfs_perso_lookup() (which falls back to
 * vfs_lookup when the process has no personality prefix) and checks access
 * via vfs_check_permissions_groups() instead of the old
 * vfs_check_permissions().  Mock the new entry points; provide link stubs
 * for the personality/debug helpers vfs_perso_lookup references.
 */
int vfs_check_permissions_groups(fs_node_t *node, uint32_t uid, uint32_t gid,
                                 const uint32_t *groups, int ngroups, int mode) {
    (void)node;
    (void)uid;
    (void)gid;
    (void)groups;
    (void)ngroups;
    (void)mode;
    return vfs_perm_result;
}

struct personality *perso_lookup(int id) { (void)id; return NULL; }
int cmdline_debug_enabled(const char *channel) { (void)channel; return 0; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

extern int kern_access(const char *path, int mode);
extern int sys_access(const char *path, int mode);

static void reset_env(void) {
    memset(&proc, 0, sizeof(proc));
    memset(&root_node, 0, sizeof(root_node));
    memset(&cwd_node, 0, sizeof(cwd_node));
    memset(&found_node, 0, sizeof(found_node));
    current_process = &proc;
    fs_root = &root_node;
    proc.root_node = &root_node;
    proc.cwd_node = &cwd_node;
    last_lookup_path = NULL;
    last_lookup_root = NULL;
    vfs_perm_result = 0;
}

int main(void) {
    reset_env();

    assert(kern_access(NULL, F_OK) == -EFAULT);
    assert(kern_access("/missing", F_OK) == -ENOENT);

    vfs_perm_result = -1;
    assert(kern_access("/denied", X_OK) == -EACCES);
    assert(sys_access("/denied", X_OK) == -EACCES);

    vfs_perm_result = 0;
    assert(kern_access("/present", F_OK) == 0);
    assert(kern_access("/present", R_OK) == 0);

    assert(kern_access("relative", R_OK) == 0);
    assert(last_lookup_root == &cwd_node);
    assert(strcmp(last_lookup_path, "relative") == 0);

    assert(kern_access("/absolute", R_OK) == 0);
    assert(last_lookup_root == &root_node);
    assert(strcmp(last_lookup_path, "/absolute") == 0);

    puts("host_test_access: ok");
    return 0;
}
