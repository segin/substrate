#ifndef _GRP_H
#define _GRP_H

#include <sys/types.h>

struct group {
    char   *gr_name;       /* group name */
    char   *gr_passwd;     /* group password */
    gid_t   gr_gid;        /* group ID */
    char  **gr_mem;        /* group members */
};

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);
int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups);

#endif
