/* <sys/xattr.h> — Linux-compatible extended-attribute API.
 *
 * Read-side is implemented for ext2/4 (one-block xattr storage at
 * inode->i_file_acl).  Write-side is a stub at -ENOSYS until a
 * writer ports — call sites can still link.
 *
 * Two-call convention for *getxattr / *listxattr:
 *   - value==NULL or size==0: return required size, don't write.
 *   - Otherwise: write up to `size` bytes; if more would be needed
 *     return -1 with errno=ERANGE.
 */
#ifndef _SYS_XATTR_H
#define _SYS_XATTR_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XATTR_CREATE   1   /* fail if attribute already exists */
#define XATTR_REPLACE  2   /* fail if attribute doesn't exist  */

ssize_t getxattr(const char *path, const char *name, void *value, size_t size);
ssize_t lgetxattr(const char *path, const char *name, void *value, size_t size);
ssize_t fgetxattr(int fd, const char *name, void *value, size_t size);

ssize_t listxattr(const char *path, char *list, size_t size);
ssize_t llistxattr(const char *path, char *list, size_t size);
ssize_t flistxattr(int fd, char *list, size_t size);

int setxattr(const char *path, const char *name, const void *value, size_t size, int flags);
int lsetxattr(const char *path, const char *name, const void *value, size_t size, int flags);
int fsetxattr(int fd, const char *name, const void *value, size_t size, int flags);

int removexattr(const char *path, const char *name);
int lremovexattr(const char *path, const char *name);
int fremovexattr(int fd, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_XATTR_H */
