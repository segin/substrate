/*
 * Substrate syscall() Implementation
 * Wraps _syscall6 for generic syscall invocation.
 */
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <sys_local.h>

long syscall(long number, ...) {
    va_list ap;
    va_start(ap, number);

    long a1 = va_arg(ap, long);
    long a2 = va_arg(ap, long);
    long a3 = va_arg(ap, long);
    long a4 = va_arg(ap, long);
    long a5 = va_arg(ap, long);
    long a6 = va_arg(ap, long); // Substrate handles up to 6 args on stack + sysnum

    va_end(ap);

    // We assume the kernel ignores extra arguments on the stack.
    // _syscall6 pushes 6 args and the syscall number, then interrupts.
    // This is safe for stack balancing provided _syscall6 balances itself.
    return (long)_syscall6((int)number, (int)a1, (int)a2, (int)a3, (int)a4, (int)a5, (int)a6);
}
