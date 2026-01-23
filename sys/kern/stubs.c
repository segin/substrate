#include <stdint.h>
#include <stddef.h>

int sys_nice(int inc) { (void)inc; return -1; }
int sys_mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return -1; }
int sys_sigret(void) { return -1; }
int sys_lchown(const char *path, int owner, int group) { (void)path; (void)owner; (void)group; return -1; }
int sys_stime(uint32_t *t) { (void)t; return -1; }
int sys_ptrace(int req, int pid, int addr, int data) { (void)req; (void)pid; (void)addr; (void)data; return -1; }
int sys_alarm(unsigned int sec) { (void)sec; return 0; }
int sys_pause(void) { return -1; }
int sys_utime(const char *path, void *times) { (void)path; (void)times; return -1; }
int sys_statfs(const char *path, void *buf) { (void)path; (void)buf; return -1; }
int sys_fstatfs(int fd, void *buf) { (void)fd; (void)buf; return -1; }
int sys_ulimit(int cmd, long limit) { (void)cmd; (void)limit; return -1; }
int sys_prof(void *buf, size_t size, unsigned long offset, unsigned int scale) { (void)buf; (void)size; (void)offset; (void)scale; return -1; }

/* SVR-specific multiplexer stubs */
int sys_pgrpsys(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return -1; }
int sys_sigsys(int a, void *b) { (void)a; (void)b; return -1; }
int sys_msgsys(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return -1; }
int sys_sysi86(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return -1; }
int sys_shmsys(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return -1; }
int sys_semsys(int a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -1; }
int sys_uadmin(int a, int b, int c) { (void)a; (void)b; (void)c; return -1; }
int sys_utssys(void *a, int b, int c) { (void)a; (void)b; (void)c; return -1; }

