/*
 * seclevel — kern.securelevel only rises once it is above zero.
 *
 * kern.securelevel used the plain integer handler, so root could raise it
 * and then simply write 0 (or -1) back.  Raises it to 1 and 2, then tries
 * 0: that must fail with EPERM and leave 2 in place.  Run as root, last:
 * the level stays up until the next boot.  Substrate only (raw sysctl).
 */
#include <errno.h>
#include <stdio.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static long set(int val)
{
    int mib[2] = { CTL_KERN, KERN_SECURELVL };
    return syscall(SYS_SYSCTL, mib, 2, NULL, NULL, &val, sizeof(val));
}

static int get(void)
{
    int mib[2] = { CTL_KERN, KERN_SECURELVL };
    int val = -100;
    size_t len = sizeof(val);
    syscall(SYS_SYSCTL, mib, 2, &val, &len, NULL, 0);
    return val;
}

int main(void)
{
    check(set(1) == 0, "raise to 1");
    check(set(2) == 0, "raise to 2");
    /* syscall() hands back the kernel's -errno as is. */
    check(set(0) == -EPERM, "lowering to 0 fails with EPERM");
    check(get() == 2, "the level is still 2");
    printf("seclevel: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
