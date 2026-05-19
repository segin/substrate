/*
 * bin/groups/groups.c — print group memberships for a user.
 *
 * Usage:  groups [user...]
 *
 * With no argument, prints the calling process's groups (primary +
 * supplementary) by way of getgroups(2) / getgid(2) / getegid(2).
 * Output is one space-separated line per user when arguments are
 * given, prefixed with "<user> : " for the multi-user form.
 *
 * With one or more usernames, walks /etc/group to find every group
 * the user is a member of (including their /etc/passwd-named
 * primary group).  No assumption that the user is the caller.
 */

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int
print_groups_self(void)
{
    /* Primary first, supplementary set after. */
    gid_t            primary = getegid();
    int              n;
    gid_t           *list;
    struct group    *gr;
    int              first = 1;

    /* Probe the supplementary set size, then allocate exactly. */
    n = getgroups(0, NULL);
    if (n < 0) {
        fprintf(stderr, "groups: getgroups: %s\n", strerror(errno));
        return 1;
    }
    list = calloc((size_t)n + 1, sizeof(gid_t));
    if (list == NULL) {
        fprintf(stderr, "groups: out of memory\n");
        return 1;
    }
    if (n > 0 && getgroups(n, list) < 0) {
        fprintf(stderr, "groups: getgroups: %s\n", strerror(errno));
        free(list);
        return 1;
    }

    /* Primary first if not already in the supplementary list. */
    gr = getgrgid(primary);
    if (gr != NULL) {
        printf("%s", gr->gr_name);
    } else {
        printf("%u", (unsigned)primary);
    }
    first = 0;

    for (int i = 0; i < n; i++) {
        if (list[i] == primary) {
            continue;
        }
        gr = getgrgid(list[i]);
        if (gr != NULL) {
            printf("%s%s", first ? "" : " ", gr->gr_name);
        } else {
            printf("%s%u", first ? "" : " ", (unsigned)list[i]);
        }
        first = 0;
    }
    putchar('\n');
    free(list);
    return 0;
}

/*
 * Walk /etc/group and print every group that lists `user` in its
 * member roll OR whose gid matches `pw_gid`.  The order matches the
 * traditional shadow-utils output: primary group first, then the
 * /etc/group entries in file order.
 */
static int
print_groups_for_user(const char *user)
{
    struct passwd   *pw;
    struct group    *gr;
    int              first = 1;
    char            *primary_name = NULL;
    gid_t            primary_gid;

    pw = getpwnam(user);
    if (pw == NULL) {
        fprintf(stderr, "groups: '%s': no such user\n", user);
        return 1;
    }
    primary_gid = pw->pw_gid;

    /* Cache the primary group name before iterating /etc/group —
     * getgrent will clobber the static struct getgrgid uses. */
    gr = getgrgid(primary_gid);
    if (gr != NULL) {
        primary_name = strdup(gr->gr_name);
    }

    if (primary_name != NULL) {
        printf("%s", primary_name);
        first = 0;
    } else {
        printf("%u", (unsigned)primary_gid);
        first = 0;
    }

    setgrent();
    while ((gr = getgrent()) != NULL) {
        int is_member = 0;
        if (gr->gr_gid == primary_gid) {
            /* Already printed as primary. */
            continue;
        }
        if (gr->gr_mem != NULL) {
            for (char **mp = gr->gr_mem; *mp != NULL; mp++) {
                if (strcmp(*mp, user) == 0) {
                    is_member = 1;
                    break;
                }
            }
        }
        if (is_member) {
            printf("%s%s", first ? "" : " ", gr->gr_name);
            first = 0;
        }
    }
    endgrent();
    putchar('\n');
    free(primary_name);
    return 0;
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        return print_groups_self();
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (argc > 2) {
            printf("%s : ", argv[i]);
        }
        if (print_groups_for_user(argv[i]) != 0) {
            rc = 1;
        }
    }
    return rc;
}
