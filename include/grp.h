#ifndef _GRP_H
#define _GRP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stddef.h>

struct group {
    char   *gr_name;       /* group name */
    char   *gr_passwd;     /* "x" or hash */
    gid_t   gr_gid;        /* numeric group ID */
    char  **gr_mem;        /* NULL-terminated array of member names */
};

struct group *getgrgid(gid_t gid);
struct group *getgrnam(const char *name);

void          setgrent(void);
struct group *getgrent(void);
void          endgrent(void);

int getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
               struct group **result);
int getgrnam_r(const char *name, struct group *grp, char *buf, size_t buflen,
               struct group **result);

/*
 * Compute the group list for `user`.  `group` is the primary group
 * (always included).  On entry `*ngroups` is the size of `groups`;
 * on return it's the number of gids that would have been written
 * (so >`*ngroups` on entry means buffer was too small).  Returns
 * the number actually written, or -1 if the buffer is too small.
 */
int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups);

/*
 * Look up `user`'s groups and call setgroups() with the result.
 * Requires root.  Returns 0 / -1.
 */
int initgroups(const char *user, gid_t group);

#ifdef __cplusplus
}
#endif
#endif
