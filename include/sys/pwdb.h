/*
 * <sys/pwdb.h> — shared interfaces used by substrate's
 * useradd / usermod / userdel / groupadd / groupmod / groupdel
 * admin tools.  Implementations live in lib/pwdb/, surfaced as
 * libpwdb.a / libpwdb.so.0 and linked via -lpwdb.
 *
 * Conventions:
 *
 *   - Atomic rewrite.  pwdb_atomic_rewrite(path, mode, cb, arg)
 *     builds the new contents at <path>.new via the callback, then
 *     fsync()s and rename()s on top of <path>.  A crash mid-rewrite
 *     leaves the old file intact.
 *
 *   - File locking.  pwdb_lock() takes an exclusive flock(LOCK_EX)
 *     on /etc/.pwd.lock so concurrent admin tools serialise.
 *     pwdb_unlock() releases.
 *
 *   - Names.  pwdb_valid_name() rejects empty, ":"-bearing, and
 *     leading-non-alpha names (POSIX-portable rule, no $-trailing
 *     samba interop).
 *
 *   - UID/GID space.  SYSTEM_ID_MIN..SYSTEM_ID_MAX (1..999) for
 *     -r/--system; USER_ID_MIN..USER_ID_MAX (1000..60000) otherwise.
 *     pwdb_next_free_id() scans /etc/passwd or /etc/group for the
 *     next unused number in the requested range.
 */

#ifndef _SYS_PWDB_H
#define _SYS_PWDB_H

#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWDB_PASSWD      "/etc/passwd"
#define PWDB_GROUP       "/etc/group"
#define PWDB_SHADOW      "/etc/shadow"
#define PWDB_GSHADOW     "/etc/gshadow"
#define PWDB_LOCK        "/etc/.pwd.lock"

#define SYSTEM_ID_MIN    1
#define SYSTEM_ID_MAX    999
#define USER_ID_MIN      1000
#define USER_ID_MAX      60000

/*
 * Error helper.  Prints "<progname>: <fmt>\n" to stderr and
 * exit(1)s.  Convention: callers pass argv[0]-like progname.
 */
void pwdb_die(const char *progname, const char *fmt, ...);

/*
 * Username / group name validation.  Returns nonzero if `name`
 * is a valid POSIX-portable account name, zero otherwise.
 */
int  pwdb_valid_name(const char *name);

/*
 * Acquire / release the global passwd-database lock at
 * /etc/.pwd.lock.  pwdb_lock() returns the open file descriptor
 * on success (caller passes it back to pwdb_unlock), or -1 on
 * failure with errno set.
 */
int  pwdb_lock(void);
void pwdb_unlock(int fd);

/*
 * Atomic-rewrite callback.  Invoked with an FILE* for the temp
 * file (currently <path>.new); the callback writes new contents
 * and returns 0 on success or -1 on failure.
 */
typedef int (*pwdb_write_cb)(FILE *out, void *arg);

/*
 * Run cb() to fill <path>.new, fsync, then rename over <path>.
 * Preserves the old file's ownership and mode where it existed,
 * or uses `default_mode` when creating a new file.
 *
 * Returns 0 on success, -1 on failure with errno set; the temp
 * file is unlinked on failure so a retry can succeed.
 */
int  pwdb_atomic_rewrite(const char *path, mode_t default_mode,
                         pwdb_write_cb cb, void *arg);

/*
 * Lowest unused id in [min, max] within /etc/passwd (when
 * is_group == 0) or /etc/group (when is_group != 0).  Returns -1
 * when the range is exhausted.
 */
long pwdb_next_free_id(int is_group, long min, long max);

/*
 * Days since 1970-01-01 UTC — the unit /etc/shadow's lastchange,
 * inactive, and expire fields use.
 */
long pwdb_today_days(void);

/*
 * In-place split of `line` on `delim`.  Writes up to `out_max`
 * NUL-terminated field pointers into out[]; trailing newline is
 * stripped from the last field.  Returns the field count actually
 * produced.
 */
int  pwdb_split(char *line, char delim, char **out, int out_max);

/*
 * Refuse to continue if the calling process is not running as
 * root.  Calls pwdb_die() (which exits) on failure.
 */
void pwdb_require_root(const char *progname);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_PWDB_H */
