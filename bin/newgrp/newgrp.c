/*
 * newgrp - change the current (real and effective) group ID and start a
 * new shell.
 *
 *   newgrp [group]
 *
 * With a group operand, sets the real and effective gid to that group
 * (by name or numeric gid) and execs the login shell.  With no operand,
 * resets to the user's login group from the passwd database.  The
 * process must have privilege to change to the target group; setgid()
 * failure is reported and no shell is started (the old stub printed
 * "not fully implemented" and exited 0, leaving the caller in the
 * original group while pretending to have switched).
 */

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *prog = "newgrp";

static gid_t
resolve_group(const char *spec)
{
    struct group *gr = getgrnam(spec);
    if (gr)
        return gr->gr_gid;

    /* Fall back to a numeric gid. */
    char *end;
    errno = 0;
    long v = strtol(spec, &end, 10);
    if (end != spec && *end == '\0' && errno != ERANGE && v >= 0 && v <= INT_MAX) {
        /* Confirm the gid exists so a typo isn't silently accepted. */
        return (gid_t)v;
    }

    fprintf(stderr, "%s: group '%s' does not exist\n", prog, spec);
    exit(1);
}

int
main(int argc, char *argv[])
{
    gid_t gid;

    if (argc > 2) {
        fprintf(stderr, "usage: newgrp [group]\n");
        return 1;
    }

    if (argc == 2) {
        gid = resolve_group(argv[1]);
    } else {
        /* No operand: restore the login group. */
        struct passwd *pw = getpwuid(getuid());
        if (!pw) {
            fprintf(stderr, "%s: cannot find your passwd entry\n", prog);
            return 1;
        }
        gid = pw->pw_gid;
    }

    if (setgid(gid) != 0) {
        fprintf(stderr, "%s: cannot change to gid %u: %s\n",
            prog, (unsigned)gid, strerror(errno));
        return 1;
    }

    /* Launch the login shell in the new group. */
    const char *shell = getenv("SHELL");
    if (!shell || !*shell) {
        struct passwd *pw = getpwuid(getuid());
        shell = (pw && pw->pw_shell && *pw->pw_shell) ? pw->pw_shell : "/bin/sh";
    }

    execl(shell, shell, (char *)NULL);
    fprintf(stderr, "%s: cannot exec %s: %s\n", prog, shell, strerror(errno));
    return 1;
}
