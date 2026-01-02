#include <stdint.h>
#include <stddef.h>

int sys_link(const char *old, const char *new) { (void)old; (void)new; return -1; }
int sys_unlink(const char *path) { (void)path; return -1; }
int sys_nice(int inc) { (void)inc; return -1; }
int sys_mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return -1; }
int sys_sigret(void) { return -1; }
