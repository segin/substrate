#ifndef _SYS_KERN_SYSCALLS_H
#define _SYS_KERN_SYSCALLS_H

// Forward declarations
int kern_acct(const char *path);
int sys_acct(const char *path);
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len);

#endif
