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

    /* Must be above minimum user address (avoid NULL region).  Exception:
     * Linux a.out ZMAGIC/OMAGIC images map their text at virtual address 0,
     * so the first page is legitimately mapped and low pointers (e.g. a string
     * constant in .text) are valid.  For those processes, skip the up-front
     * NULL-region rejection; a genuinely unmapped low access still faults and
     * is caught by the on_fault handler. */
    if (start < USER_STACK_MIN &&
        !(current_process && current_process->low_va_valid))
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

    /*
     * The fault recovery target lives inside the asm block so the
     * compiler cannot fold its address back into the success path
     * (when the only difference is the return value, GCC will happily
     * place `fault:` at the same instruction as the post-validate code,
     * making the trap handler resume into a re-arm of on_fault and
     * a re-execution of rep movsb — instant kernel hang).
     *
     * The asm sets on_fault, runs rep movsb, then clears on_fault.
     * On fault, the IDT handler jumps eip to 1f, which falls through
     * to clearing on_fault and setting result = EFAULT.  The dual-exit
     * is encoded entirely in the asm so there is no C-level dead code
     * for the optimiser to fold.
     */
    int result;
    __asm__ volatile (
        /* Set on_fault = label 1f */
        "movl $1f, (%[on_fault])\n\t"
        "cld\n\t"
        "rep movsb\n\t"
        /* Success: clear on_fault, result = 0, jump past fault path */
        "movl $0, (%[on_fault])\n\t"
        "xorl %[result], %[result]\n\t"
        "jmp 2f\n"
        "1:\n\t"
        /* Fault: clear on_fault, result = EFAULT */
        "movl $0, (%[on_fault])\n\t"
        "movl %[efault], %[result]\n"
        "2:\n\t"
        : "+S"(src), "+D"(dst), "+c"(size), [result] "=&r"(result)
        : [on_fault] "r"(&current_thread->on_fault),
          [efault] "i"(EFAULT)
        : "memory"
    );
    return result;
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

    /* See copyout() for why the fault label is embedded in the asm. */
    int result;
    __asm__ volatile (
        "movl $1f, (%[on_fault])\n\t"
        "cld\n\t"
        "rep movsb\n\t"
        "movl $0, (%[on_fault])\n\t"
        "xorl %[result], %[result]\n\t"
        "jmp 2f\n"
        "1:\n\t"
        "movl $0, (%[on_fault])\n\t"
        "movl %[efault], %[result]\n"
        "2:\n\t"
        : "+S"(src), "+D"(dst), "+c"(size), [result] "=&r"(result)
        : [on_fault] "r"(&current_thread->on_fault),
          [efault] "i"(EFAULT)
        : "memory"
    );
    return result;
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

    if (maxlen == 0) {
        if (len) *len = 0;
        return ENAMETOOLONG;
    }

    /* Set up fault handler */
    current_thread->on_fault = (uintptr_t)&&fault;

    for (i = 0; i < maxlen; i++) {
        if (validate_user_addr(s, 1) != 0) {
            current_thread->on_fault = 0;
            return EFAULT;
        }

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
