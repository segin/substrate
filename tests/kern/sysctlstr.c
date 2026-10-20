/*
 * sysctlstr — writing a string sysctl, and reading it back through libc.
 *
 * sysctl_handle_string terminated the new value at str[newlen - 1]: a
 * value given without its NUL ("abc", length 3) lost its last character,
 * and an empty one (length 0) wrote a NUL before the buffer.  And libc's
 * sysctl() used its own SYS_SYSCTL of 243, which is SYS_PROC_LIST: every
 * libc sysctl() call ran proc_list and "succeeded" without reading
 * anything.  Uses kern.domainname, restoring it afterwards.  Run as root;
 * substrate only.
 */
#include <stdio.h>
#include <string.h>
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

/* Through libc. */
static int get(char *buf, size_t size)
{
    int mib[2] = { CTL_KERN, KERN_DOMAINNAME };
    size_t len = size;

    memset(buf, 0, size);
    return sysctl(mib, 2, buf, &len, NULL, 0);
}

/* The raw call: libc's sysctl() refuses a zero-length new value itself. */
static int set(const char *val, size_t len)
{
    int mib[2] = { CTL_KERN, KERN_DOMAINNAME };
    return (int)syscall(SYS_SYSCTL, mib, 2, NULL, NULL, val, len);
}

int main(void)
{
    char saved[256], buf[256];

    check(get(saved, sizeof(saved)) == 0, "read kern.domainname");
    check(set("abc", 3) == 0, "set \"abc\" without its NUL");
    check(get(buf, sizeof(buf)) == 0 && strcmp(buf, "abc") == 0,
          "libc reads it back as \"abc\"");
    check(set("xyz", 0) == 0, "set an empty value");
    check(get(buf, sizeof(buf)) == 0 && buf[0] == '\0', "it reads back empty");
    set(saved, strlen(saved) + 1);
    printf("sysctlstr: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
