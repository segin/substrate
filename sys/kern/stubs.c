#include <stdint.h>
#include <stddef.h>

int sys_waitpid(int pid, int *status, int options) { (void)pid; (void)status; (void)options; return -1; }
int sys_link(const char *old, const char *new) { (void)old; (void)new; return -1; }
int sys_unlink(const char *path) { (void)path; return -1; }
int sys_chdir(const char *path) { (void)path; return -1; }
int sys_chmod(const char *path, int mode) { (void)path; (void)mode; return -1; }
int sys_lchown(const char *path, int uid, int gid) { (void)path; (void)uid; (void)gid; return -1; }
int sys_nice(int inc) { (void)inc; return -1; }
int sys_ioctl(int fd, int cmd, int arg) { (void)fd; (void)cmd; (void)arg; return -1; }
int sys_fcntl(int fd, int cmd, int arg) { (void)fd; (void)cmd; (void)arg; return -1; }
int sys_mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return -1; }
int sys_sigret(void) { return -1; }
int sys_creat(const char *path, int mode) { (void)path; (void)mode; return -1; }
int sys_signal(int sig, void *handler) { (void)sig; (void)handler; return -1; }
