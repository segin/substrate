/*
 * sys/sys/ucred.h - Process credentials
 */

#ifndef _SYS_UCRED_H
#define _SYS_UCRED_H

#include <sys/types.h>

struct ucred {
    uint32_t    cr_ref;         /* reference count */
    uid_t       cr_uid;         /* effective user id */
    uid_t       cr_ruid;        /* real user id */
    uid_t       cr_svuid;       /* saved effective user id */
    gid_t       cr_gid;         /* effective group id */
    gid_t       cr_rgid;        /* real group id */
    gid_t       cr_svgid;       /* saved effective group id */
    int         cr_ngroups;     /* number of groups */
    gid_t       cr_groups[16];  /* groups */
};

#endif /* _SYS_UCRED_H */
