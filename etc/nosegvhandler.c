/* nosegvhandler.c — selective LD_PRELOAD shim for the X-server
 * crash bisect.  Suppresses sigaction() installs for one subset of
 * signals; for everything else, calls the real syscall directly
 * (no dlsym needed).
 *
 * Discriminator builds (one of):
 *   -DBLOCK_ALL      suppress every sigaction install
 *   -DBLOCK_ALRM     suppress only SIGALRM install
 *   -DBLOCK_FATALS   suppress only SIGSEGV/SIGBUS/SIGILL/SIGFPE
 *
 * Result if X stays up with `-DBLOCK_X`: signal X (or its handler
 * install path) is the trigger.  Result if X still crashes:
 * keep bisecting.
 */

#include <signal.h>
#include <stddef.h>

#define SYS_SIGACTION 67   /* substrate i386 syscall number */

/* Raw syscall — 3 args in ebx/ecx/edx, eax = syscall number.
 * In PIC shared-library code, %ebx is the GOT register.  We must
 * save it across the syscall (the kernel only preserves it as
 * input but gcc otherwise relies on ebx for PIC accesses).  Push
 * + pop around the asm — using an output constraint for ebx would
 * be cleaner but gcc < 5 sometimes refuses to allocate ebx as a
 * temp under PIC. */
static int raw_sigaction(int signum, const struct sigaction *act,
                         struct sigaction *oldact)
{
    int ret;
    __asm__ volatile (
        "pushl %%ebx\n\t"
        "movl  %2, %%ebx\n\t"
        "int   $0x80\n\t"
        "popl  %%ebx"
        : "=a"(ret)
        : "0"(SYS_SIGACTION),
          "r"(signum), "c"(act), "d"(oldact)
        : "memory");
    if (ret < 0)
        return -1;
    return ret;
}

static int should_swallow(int signum)
{
#if defined(BLOCK_ALL)
    (void)signum;
    return 1;
#elif defined(BLOCK_ALRM)
    return signum == SIGALRM;
#elif defined(BLOCK_FATALS)
    return signum == SIGSEGV || signum == SIGBUS ||
           signum == SIGILL  || signum == SIGFPE;
#else
    (void)signum;
    return 1;
#endif
}

int sigaction(int signum, const struct sigaction *act,
              struct sigaction *oldact)
{
    if (should_swallow(signum)) {
        if (oldact) {
            struct sigaction zero = { 0 };
            *oldact = zero;
        }
        return 0;
    }
    return raw_sigaction(signum, act, oldact);
}
