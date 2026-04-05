#ifndef _LINUX_BLKIO_H
#define _LINUX_BLKIO_H

#include <sys/ioctl.h>

/* Linux Block I/O ioctls (standard i386) */
#define LINUX_BLKROSET    0x125D
#define LINUX_BLKROGET    0x125E
#define LINUX_BLKRRPART   0x125F
#define LINUX_BLKGETSIZE  0x1260
#define LINUX_BLKFLSBUF   0x1261
#define LINUX_BLKRASET    0x1262
#define LINUX_BLKRAGET    0x1263
#define LINUX_BLKFRASET   0x1264
#define LINUX_BLKFRAGET   0x1265
#define LINUX_BLKSECTSET  0x1266
#define LINUX_BLKSECTGET  0x1267
#define LINUX_BLKSSZGET   0x1268
#define LINUX_BLKPG       0x1269

/* 64-bit variants */
#define LINUX_BLKGETSIZE64 0x80041272

#endif /* _LINUX_BLKIO_H */
