#include <stdbool.h>
#include <stddef.h>
#include <sys/file.h>
#include <kern/sched.h>

extern int sys_open(const char *path, int flags, int mode);
extern int sys_close(int fd);
extern int sys_dup(int oldfd);
extern int sys_dup2(int oldfd, int newfd);
extern void vfs_init_mock_root(void);

bool test_fd_ref_counting(void) {
    sched_init();
    vfs_init_mock_root();
    
    int fd1 = sys_open("/init", 0, 0);
    if (fd1 < 0) return false;
    
    file_t *f1 = current_process->fds[fd1];
    if (f1->f_count != 1) return false;
    
    int fd2 = sys_dup(fd1);
    if (fd2 < 0) return false;
    if (current_process->fds[fd2] != f1) return false;
    if (f1->f_count != 2) return false;
    
    sys_close(fd1);
    if (f1->f_count != 1) return false;
    if (current_process->fds[fd1] != NULL) return false;
    
    sys_close(fd2);
    // f1 might be freed now, don't access it
    if (current_process->fds[fd2] != NULL) return false;
    
    return true;
}

bool test_fd_dup2(void) {
    sched_init();
    vfs_init_mock_root();
    
    int fd1 = sys_open("/init", 0, 0);
    int fd2 = 10;
    
    if (sys_dup2(fd1, fd2) != fd2) return false;
    if (current_process->fds[fd2] != current_process->fds[fd1]) return false;
    if (current_process->fds[fd1]->f_count != 2) return false;
    
    return true;
}
