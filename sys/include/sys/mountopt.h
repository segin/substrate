/*
 * mountopt.h — generic mount-option parser.
 *
 * The kernel splits a comma-separated "key1=value1,key2,key3=value3"
 * options string passed to sys_mount() into a list of key/value
 * pairs, then offers type-safe accessors.  Filesystem mount handlers
 * call mountopt_get_int/_string/_bool to extract their tunables
 * uniformly; bare keys (no '=') are treated as boolean true.
 *
 * Generic options (ro, rw, nosuid, nodev, noexec, sync, async,
 * atime, noatime) are recognised by mountopt_apply_generic() which
 * folds them into the mount's MNT_* flag bitmask before the
 * filesystem-specific handler runs.
 *
 * The parsed list is owned by the caller; build it with
 * mountopt_parse() and tear it down with mountopt_free().
 */
#ifndef _SYS_MOUNTOPT_H
#define _SYS_MOUNTOPT_H

#include <stdint.h>
#include <stddef.h>

typedef struct mountopt {
    char            *key;       /* heap-allocated, NUL-terminated */
    char            *value;     /* NULL for bare keys */
    struct mountopt *next;
} mountopt_t;

/*
 * Parse `opts` (comma-separated, may be NULL or empty) into a
 * list.  Returns a pointer to the head; on parse error returns NULL
 * and sets *err to -errno.  On success *err is 0.
 */
mountopt_t *mountopt_parse(const char *opts, int *err);

/* Free a list returned by mountopt_parse(). */
void mountopt_free(mountopt_t *head);

/*
 * Lookup helpers.  Each returns 0 if the key was found and parsed
 * successfully, -ENOENT if the key isn't present, or -EINVAL on a
 * conversion error.  If `default_val` is supplied and the key is
 * absent, the value is written through and 0 is returned.
 */
int mountopt_get_int   (mountopt_t *head, const char *key, long *out);
int mountopt_get_uint  (mountopt_t *head, const char *key, unsigned long *out);
int mountopt_get_bool  (mountopt_t *head, const char *key, int *out);
int mountopt_get_string(mountopt_t *head, const char *key, const char **out);

/* Returns non-zero if `key` is present with any value (including bare). */
int mountopt_has(mountopt_t *head, const char *key);

/*
 * Apply standard generic options to *flags.  Recognised:
 *   ro/rw, nosuid/suid, nodev/dev, noexec/exec, sync/async,
 *   atime/noatime, relatime, noatime
 * Unknown options are left alone for the filesystem handler.
 * Returns 0 on success, -EINVAL on conflicts (e.g. ro and rw both set).
 */
int mountopt_apply_generic(mountopt_t *head, uint32_t *flags);

#endif /* _SYS_MOUNTOPT_H */
