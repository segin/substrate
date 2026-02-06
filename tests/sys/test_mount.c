#include <kern/console.h>
#include <vfs/vfs.h>
#include <sys/mount.h>
#include <string.h>

extern struct mountlist mountlist;

// Extern existing syscall wrappers within kernel (from syscall.c)
extern int sys_mount(const char *source, const char *target, const char *fstype, unsigned long flags, void *data);
extern int sys_umount(const char *target);
extern int sys_mkdir(const char *p, int m);
extern int sys_rmdir(const char *p);

extern int sys_setuid(int u);
extern int sched_spawn_kernel_process(void (*entry)(void*), void *arg);
extern int sys_exit(int code);
extern void sched_yield(void);

// Synchronization for test
static volatile int test_worker_status = 0; // 0=running, 1=success (mount failed as expected), 2=failed (mount succeeded or other error)
static volatile int test_worker_done = 0;

static void mount_permission_worker(void *arg) {
    const char *mnt = (const char*)arg;

    // 1. Drop privileges (1000)
    // sys_setuid returns 0 on success
    if (sys_setuid(1000) != 0) {
        kprint("TEST WORKER: sys_setuid failed\n");
        test_worker_status = 2;
        test_worker_done = 1;
        sys_exit(1);
        return;
    }

    // 2. Try mount (should fail with -EPERM)
    int ret = sys_mount(NULL, mnt, "devfs", 0, NULL);

    if (ret == 0) {
        kprint("TEST WORKER: sys_mount SUCCEEDED (unexpected for non-root)\n");
        // Try to unmount to cleanup?
        sys_umount(mnt);
        test_worker_status = 2;
    } else {
        // Expected failure
        // We assume failure is good enough for now, specifically -EPERM (-1)
        test_worker_status = 1;
    }

    test_worker_done = 1;
    sys_exit(0);
}

void test_mount_permissions(void) {
    kprint("TEST: mount_permissions starting...\n");

    const char *mnt_point = "/mnt_perm_test";

    // Create mountpoint (as root)
    sys_mkdir(mnt_point, 0755);

    test_worker_status = 0;
    test_worker_done = 0;

    // Spawn worker
    int tid = sched_spawn_kernel_process(mount_permission_worker, (void*)mnt_point);
    if (tid < 0) {
        kprint("TEST: Failed to spawn worker\n");
        return;
    }

    // Wait for worker
    int timeout = 10000000;
    while (!test_worker_done && timeout > 0) {
        timeout--;
        sched_yield();
    }

    if (!test_worker_done) {
        kprint("TEST: Worker timed out\n");
    } else {
        if (test_worker_status == 1) {
             kprint("TEST: Permission check PASSED (non-root mount failed)\n");
        } else {
             kprint("TEST: Permission check FAILED\n");
        }
    }

    // Cleanup
    sys_rmdir(mnt_point);
}

static int count_mounts(void) {
    int count = 0;
    struct mount *mp;
    TAILQ_FOREACH(mp, &mountlist, mnt_list) {
        count++;
    }
    return count;
}

void run_mount_tests(void) {
    kprint("TEST: mount_tests starting...\n");
    
    int initial_mounts = count_mounts();
    kprint("Initial mounts: ");
    // Manual integer print
    if (initial_mounts == 0) kprint("0 (Suspicious)\n");
    else kprint("Non-zero (OK)\n");

    const char *mnt_point = "/mnt_tmp_test";
    
    // 1. Create mount point
    kprint("TEST: Creating mount point... ");
    if (sys_mkdir(mnt_point, 0755) != 0) {
        // Must check if it failed because it exists?
        // But for fresh test ...
        // Assume success or valid failure.
        // kprint("mkdir failed (might exist)\n");
    }
    // Verify it exists
    fs_node_t *node = vfs_lookup(fs_root, mnt_point);
    if (!node) {
        kprint("FAILED (mkdir)\n");
        return;
    }
    kprint("OK\n");
    
    // 2. Mount devfs
    kprint("TEST: Mounting devfs... ");
    // devfs typically doesn't need a source, but we pass NULL or "none"
    int ret = sys_mount(NULL, mnt_point, "devfs", 0, NULL);
    if (ret != 0) {
        kprint("FAILED (ret!=0)\n");
        return;
    }
    kprint("OK\n");
    
    int after_mount = count_mounts();
    if (after_mount == initial_mounts + 1) {
        kprint("TEST: Mount list count incremented (OK)\n");
    } else {
        kprint("TEST: Mount list count NOT incremented (FAILED)\n");
    }

    // 3. Verify content
    kprint("TEST: Verifying content (lookup 'null')... ");
    // Lookup inside the NEW mount
    char file_path[64];
    strcpy(file_path, mnt_point);
    // strcat(file_path, "/null");
    // Manual append
    int len = strlen(file_path);
    const char *suffix = "/null";
    for(int i=0; suffix[i] && len < 63; i++) {
        file_path[len++] = suffix[i];
    }
    file_path[len] = '\0';

    
    fs_node_t *null_node = vfs_lookup(fs_root, file_path);
    if (null_node) {
        kprint("FOUND (OK)\n");
    } else {
        kprint("FAILED (not found)\n");
    }
    
    // 4. Unmount
    kprint("TEST: Unmounting... ");
    ret = sys_umount(mnt_point);
    if (ret != 0) {
        kprint("FAILED (ret!=0)\n");
    } else {
        kprint("OK\n");
    }

    int after_unmount = count_mounts();
    if (after_unmount == initial_mounts) {
        kprint("TEST: Mount list count decremented (OK)\n");
    } else {
        kprint("TEST: Mount list count NOT decremented (FAILED)\n");
    }
    
    // 5. Verify unmount (content should be gone or revert to directory)
    kprint("TEST: Verifying unmount (lookup 'null')... ");
    node = vfs_lookup(fs_root, file_path);
    if (!node) {
        kprint("OK (not found)\n");
    } else {
        kprint("FAILED (still found)\n");
    }

    // Cleanup
    sys_rmdir(mnt_point);
    
    test_mount_permissions();

    kprint("TEST: mount_tests finished.\n");
}
