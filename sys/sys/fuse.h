#ifndef _SYS_FUSE_H
#define _SYS_FUSE_H

#include <stdint.h>

#define FUSE_LOOKUP	   1
#define FUSE_FORGET	   2
#define FUSE_GETATTR	   3
#define FUSE_SETATTR	   4
#define FUSE_READLINK	   5
#define FUSE_SYMLINK	   6
#define FUSE_MKNOD	   8
#define FUSE_MKDIR	   9
#define FUSE_UNLINK	   10
#define FUSE_RMDIR	   11
#define FUSE_RENAME	   12
#define FUSE_LINK	   13
#define FUSE_OPEN	   14
#define FUSE_READ	   15
#define FUSE_WRITE	   16
#define FUSE_STATFS	   17
#define FUSE_RELEASE	   18
#define FUSE_FSYNC	   20
#define FUSE_SETXATTR	   21
#define FUSE_GETXATTR	   22
#define FUSE_LISTXATTR	   23
#define FUSE_REMOVEXATTR   24
#define FUSE_FLUSH	   25
#define FUSE_INIT	   26
#define FUSE_OPENDIR	   27
#define FUSE_READDIR	   28
#define FUSE_RELEASEDIR    29
#define FUSE_FSYNCDIR	   30

struct fuse_in_header {
	uint32_t len;
	uint32_t opcode;
	uint64_t unique;
	uint64_t nodeid;
	uint32_t uid;
	uint32_t gid;
	uint32_t pid;
	uint32_t padding;
};

struct fuse_out_header {
	uint32_t len;
	int32_t	 error;
	uint64_t unique;
};

#endif
