#ifndef _SYS_COPY_H
#define _SYS_COPY_H

#include <stddef.h>
#include <sys/compiler.h>

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

/*
 * Copy the NUL-terminated user string at uaddr (a path or a file name) into
 * the kernel array kbuf, or return from the calling system call with
 * -EFAULT for a bad address or -ENAMETOOLONG if it does not fit.
 */
#define COPYIN_STR(uaddr, kbuf) do {                                    \
        int copyin_str_err_ = copyinstr((uaddr), (kbuf), sizeof(kbuf), NULL); \
        if (copyin_str_err_ != 0)                                       \
            return -copyin_str_err_;                                    \
    } while (0)

#endif
