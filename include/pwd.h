#ifndef _PWD_H
#define _PWD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stddef.h>

struct passwd {
    char   *pw_name;       /* username */
    char   *pw_passwd;     /* "x" (real hash in /etc/shadow) or plaintext */
    uid_t   pw_uid;        /* user ID */
    gid_t   pw_gid;        /* primary group ID */
    char   *pw_gecos;      /* user information / full name */
    char   *pw_dir;        /* home directory */
    char   *pw_shell;      /* login shell */
};

/*
 * Lookup by id / name.  Both return a pointer to a static struct
 * whose string fields point into a separate static line buffer —
 * the result is overwritten by the next call to any of the three
 * lookup functions or to getpwent().  POSIX semantics.
 *
 * Returns NULL on not-found or I/O error.
 */
struct passwd *getpwuid(uid_t uid);
struct passwd *getpwnam(const char *name);

/*
 * Iterator.  setpwent() opens (or rewinds) the database, getpwent()
 * walks it line-by-line returning the same shared static struct,
 * endpwent() closes it.
 */
void           setpwent(void);
struct passwd *getpwent(void);
void           endpwent(void);

/*
 * Reentrant variants — caller supplies the destination struct, a
 * working line buffer, and a result pointer that ends up either
 * pointing at `pwd` (success) or NULL (not found / buffer too
 * small).  Returns 0 on success or not-found, ERANGE if buflen is
 * too small, or some other errno on failure.
 */
int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result);
int getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result);

#ifdef __cplusplus
}
#endif
#endif
