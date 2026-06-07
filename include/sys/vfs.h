#ifndef _SYS_VFS_H
#define _SYS_VFS_H 1

/*
 * Linux's <sys/vfs.h> is the historical home of statfs(2) and struct statfs;
 * glibc forwards it to <sys/statfs.h>.  Substrate keeps the definitions in
 * <sys/statfs.h>; mirror glibc's forwarder for code (CDE's DtMmdb) that
 * includes <sys/vfs.h> on the Linux path.
 */
#include <sys/statfs.h>

#endif /* _SYS_VFS_H */
