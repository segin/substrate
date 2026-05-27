/* nosegvhandler.c — LD_PRELOAD shim that swallows sigaction()
 * installs for the fatal-signal set (SIGSEGV/SIGBUS/SIGILL/SIGFPE).
 *
 * Why: X server installs OsSigHandler for these signals and that
 * handler does enough work in the sig context that it crashes
 * RECURSIVELY when the original bug fires, hiding the true call
 * site behind a vpnprintf/ErrorFSigSafe stack.  Disabling the
 * handler makes the kernel see the FIRST SIGSEGV directly and
 * print TRAP/CORE.
 *
 * Build: $(CROSS)gcc -shared -fPIC -o nosegvhandler.so nosegvhandler.c
 * Use:   LD_PRELOAD=/path/nosegvhandler.so Xfbdev ...
 */

#include <signal.h>
#include <stddef.h>

extern int dlsym_dummy_so_ld_so_loads_us(void);

/* On substrate's ld.so, weak override of a libc symbol works
 * because the loader resolves to the first definition in scope.
 * LD_PRELOAD'd lib is searched first, so our sigaction wins. */

int sigaction(int signum, const struct sigaction *act,
              struct sigaction *oldact)
{
    (void)act;
    if (oldact) {
        struct sigaction zero = { 0 };
        *oldact = zero;
    }
    /* Swallow the install for fatal signals — let the kernel
     * default-action handle them so we get TRAP/CORE. */
    if (signum == SIGSEGV || signum == SIGBUS ||
        signum == SIGILL  || signum == SIGFPE)
    {
        return 0;
    }
    /* Other signals: also no-op for simplicity.  This means
     * SIGALRM, SIGCHLD, etc. won't be installed either.  For
     * a one-shot diagnostic run that's fine — SmartScheduleTimer
     * just doesn't get its tick. */
    return 0;
}
