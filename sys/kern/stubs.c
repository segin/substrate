#include <stdint.h>
#include <stddef.h>

int sys_nice(int inc) { (void)inc; return -1; }
int sys_mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return -1; }
int sys_sigret(void) { return -1; }
