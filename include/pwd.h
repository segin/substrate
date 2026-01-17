#ifndef _PWD_H
#define _PWD_H

#include <sys/types.h>

struct passwd {
    char   *pw_name;       /* username */
    char   *pw_passwd;     /* user password */
    uid_t   pw_uid;        /* user ID */
    gid_t   pw_gid;        /* group ID */
    char   *pw_gecos;      /* user information */
    char   *pw_dir;        /* home directory */
    char   *pw_shell;      /* shell_program */
};

struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

#endif
