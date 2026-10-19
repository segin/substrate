/*
 * setredrop — setreuid/setregid drop the saved set-ID.
 *
 * A set-user-ID-root program run by uid 1000 has (real, effective,
 * saved) = (1000, 0, 0).  setreuid(getuid(), getuid()) must leave all
 * three at 1000: the real ID was set, so the saved ID follows the new
 * effective ID.  The kernel only did that when the real ID *changed*,
 * left the saved ID at 0, and seteuid(0) then got root back.
 *
 * Must start as root (it plays the set-ID program with setresuid).
 * Runs on the host too: sudo ./setredrop
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#define USER 1000

static int fails;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

int main(void) {
    uid_t r, e, s;
    gid_t rg, eg, sg;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (geteuid() != 0) {
        printf("setredrop: SKIP (needs root)\n");
        return 0;
    }

    /* Group IDs first, while the euid is still 0. */
    check(setresgid(USER, 0, 0) == 0, "setresgid(1000, 0, 0)");
    check(setregid(USER, USER) == 0, "setregid(1000, 1000)");
    getresgid(&rg, &eg, &sg);
    printf("      gids now %u %u %u\n", (unsigned)rg, (unsigned)eg,
           (unsigned)sg);
    check(sg == USER, "saved gid dropped to 1000");

    check(setresuid(USER, 0, 0) == 0, "setresuid(1000, 0, 0)");
    check(setreuid(USER, USER) == 0, "setreuid(1000, 1000)");
    getresuid(&r, &e, &s);
    printf("      uids now %u %u %u\n", (unsigned)r, (unsigned)e,
           (unsigned)s);
    check(s == USER, "saved uid dropped to 1000");

    errno = 0;
    check(seteuid(0) == -1 && errno == EPERM, "seteuid(0) refused");
    errno = 0;
    check(setegid(0) == -1 && errno == EPERM, "setegid(0) refused");
    check(geteuid() == USER && getegid() == USER, "still unprivileged");

    printf("setredrop: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
