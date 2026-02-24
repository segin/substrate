/*
 * sys/kern/subr_copy.c
 *
 * Centralized user-kernel copy functions.
 */

#include <sys/copy.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <string.h>

/*
 * validate_user_addr - Check if address range is valid user-space
 *
 * Returns 0 if the address range [addr, addr+size) is entirely within
 * valid user address space, EFAULT otherwise.
 */
int validate_user_addr(const void *addr, size_t size) {
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;

    /* Check for overflow */
    if (end < start)
        return EFAULT;

    /* Must be above minimum user address (avoid NULL region) */
    if (start < USER_STACK_MIN)
        return EFAULT;

    /* Must be below kernel space */
    if (end > KERN_BASE)
        return EFAULT;

    return 0;
}

/*
 * copyout - Copy data from kernel space to user space
 *
 * Safely copies 'size' bytes from kernel buffer 'src' to user-space
 * address 'dst'. Uses on_fault handler to catch page faults.
 *
 * Returns:
 *   0 on success
 *   EFAULT on invalid address
 */
int copyout(const void *src, void *dst, size_t size) {
    /* Validate destination is in user space (basic bounds check) */
    if (validate_user_addr(dst, size) != 0) {
        return EFAULT;
    }

    /* Set up fault handler */
    current_thread->on_fault = (uintptr_t)&&fault;

    /* Perform copy using inline assembly (rep movsb) */
    /* If a page fault occurs, the IDT handler will redirect us to 'fault' */
    __asm__ volatile (
        "cld; rep movsb"
        : "+S"(src), "+D"(dst), "+c"(size)
        :
        : "memory"
    );

    /* Success - clear fault handler */
    current_thread->on_fault = 0;
    return 0;

fault:
    /* Fault occurred - clear handler and return error */
    current_thread->on_fault = 0;
    return EFAULT;
}

/*
 * copyin - Copy data from user space to kernel space
 *
 * Safely copies 'size' bytes from user-space address 'src' to kernel
 * buffer 'dst'. Uses on_fault handler to catch page faults.
 *
 * Returns:
 *   0 on success
 *   EFAULT on invalid address
 */
int copyin(const void *src, void *dst, size_t size) {
    /* Validate source is in user space */
    if (validate_user_addr(src, size) != 0) {
        return EFAULT;
    }

    /* Set up fault handler */
    current_thread->on_fault = (uintptr_t)&&fault;

    /* Perform copy using inline assembly (rep movsb) */
    __asm__ volatile (
        "cld; rep movsb"
        : "+S"(src), "+D"(dst), "+c"(size)
        :
        : "memory"
    );

    current_thread->on_fault = 0;
    return 0;

fault:
    current_thread->on_fault = 0;
    return EFAULT;
}

/*
 * copyinstr - Copy string from user space to kernel space
 *
 * Safely copies a null-terminated string from 'src' in user space
 * to kernel buffer 'dst'. Uses on_fault handler to catch page faults.
 *
 * Returns:
 *    0 on success
 *    EFAULT on invalid address
 *    ENAMETOOLONG on buffer too small
 */
int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    const char *s = (const char *)src;
    char *d = (char *)dst;
    size_t i;

    /* Set up fault handler */
    current_thread->on_fault = (uintptr_t)&&fault;

    for (i = 0; i < maxlen; i++) {
        /* This access may trigger a page fault */
        char c = *s;
        if (d) *d = c;
        if (c == '\0') {
            if (len) *len = i + 1;
            current_thread->on_fault = 0;
            return 0;
        }
        s++;
        if (d) d++;
    }

    /* Buffer full but no null terminator found */
    current_thread->on_fault = 0;
    if (len) *len = maxlen;
    return ENAMETOOLONG;

fault:
    current_thread->on_fault = 0;
    return EFAULT;
}
