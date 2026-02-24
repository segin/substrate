#ifndef _SYS_COPY_H
#define _SYS_COPY_H

#include <stddef.h>

/*
 * User-Kernel Copy Functions
 *
 * These functions safely copy data between user and kernel space,
 * handling page faults and address validation.
 *
 * Returns:
 *   0 on success
 *   EFAULT on invalid address (positive errno)
 *   ENAMETOOLONG on string buffer overflow (copyinstr)
 */

int validate_user_addr(const void *addr, size_t size);
int copyin(const void *src, void *dst, size_t size);
int copyout(const void *src, void *dst, size_t size);
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len);

#endif
