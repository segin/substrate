/*
 * <sys/file.h> — file-system advisory locking (4.3BSD flock).
 *
 * flock(2) takes a file descriptor and an operation; the operation
 * is the OR of LOCK_SH/LOCK_EX/LOCK_UN and an optional LOCK_NB.
 * Substrate's implementation honors the basic semantics: LOCK_SH
 * permits multiple readers, LOCK_EX is exclusive, LOCK_UN releases.
 * Locks are tied to the open-file-description, not the fd — they
 * survive dup() / fork() the same way POSIX fcntl(F_SETLK) ones
 * don't.
 */
#ifndef _SYS_FILE_H
#define _SYS_FILE_H

#include <fcntl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOCK_SH 1   /* shared lock */
#define LOCK_EX 2   /* exclusive lock */
#define LOCK_NB 4   /* don't block when locking */
#define LOCK_UN 8   /* unlock */

int flock(int fd, int operation);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_FILE_H */
