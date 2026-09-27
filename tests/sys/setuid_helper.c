/*
 * setuid_helper.c -- report the credentials an exec gave this process.
 *
 * Installed under various owners and modes by torture_setuid.  Prints one
 * line:
 *
 *   CREDS uid=U euid=E gid=G egid=EG secure=S saved=V
 *
 * where secure is the AT_SECURE auxv entry and saved is 1 when the
 * effective uid can be dropped to the real one and taken back (the saved
 * set-user-ID holds it), 0 when it cannot, - when euid == uid.
 *
 *   setuid_helper drop    also: setuid(getuid()) then setuid(0) -- prints
 *                         REGAIN=1 if root came back (it must not)
 *   setuid_helper sleep   sleep 5 s (a target for ptrace)
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

static long at_secure(void) {
    char **e = environ;
    while (*e)
        e++;
    for (uint32_t *a = (uint32_t *)(e + 1); a[0] != 0; a += 2)
        if (a[0] == 23)                         /* AT_SECURE */
            return (long)a[1];
    return -1;
}

int main(int argc, char **argv) {
    long secure = at_secure();
    uid_t ruid = getuid(), euid = geteuid();
    const char *saved = "-";
    if (euid != ruid) {
        int down = seteuid(ruid) == 0 && geteuid() == ruid;
        int up = seteuid(euid) == 0 && geteuid() == euid;
        saved = down && up ? "1" : "0";
    }
    printf("CREDS uid=%d euid=%d gid=%d egid=%d secure=%ld saved=%s\n",
           (int)ruid, (int)geteuid(), (int)getgid(), (int)getegid(), secure, saved);
    if (argc > 1 && strcmp(argv[1], "drop") == 0) {
        setuid(ruid);
        printf("REGAIN=%d\n", setuid(0) == 0 ? 1 : 0);
    }
    fflush(stdout);
    if (argc > 1 && strcmp(argv[1], "sleep") == 0)
        sleep(5);
    return 0;
}
