#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <sys/file.h"
#include <kern/sched.h"

extern int sys_chroot(const char *path);
extern int sys_open(const char *path, int flags, int mode);
extern int sys_close(int fd);
extern void vfs_init_mock_root(void);

bool test_vfs_chroot_basic(void) {
    sched_init();
    vfs_init_mock_root();
    
    // In our mock root, "init" is a file. Let's imagine we have a dir.
    // Since mock root is limited, we'll just test if chroot changes the pointer.
    
    // chroot to "/" (should be no-op or match current)
    if (sys_chroot("/") != 0) return false;
    
    // chroot to a non-existent path
    if (sys_chroot("/nonexistent") == 0) return false;
    
    return true;
}

bool test_vfs_chroot_effect(void) {
    sched_init();
    vfs_init_mock_root();
    
    // Let's manually set up a fake directory node
    fs_node_t fake_dir;
    memset(&fake_dir, 0, sizeof(fake_dir));
    strcpy(fake_dir.name, "fake");
    fake_dir.flags = FS_DIRECTORY;
    
    // Set it as root
    current_process->root_node = &fake_dir;
    
    // sys_open("/") should now return fake_dir
    int fd = sys_open("/", 0, 0);
    if (fd < 0) return false;
    
    if (current_process->fds[fd]->node != &fake_dir) return false;
    
    sys_close(fd);
    return true;
}
