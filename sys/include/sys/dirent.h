/*
 * sys/dirent.h - Kernel directory entry definitions
 */

#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

struct dirent {
    uint64_t d_ino;       /* File serial number */
    uint16_t d_reclen;    /* Length of this record */
    uint8_t  d_type;      /* File type, see below */
    uint8_t  d_namlen;    /* Length of string in d_name */
    char     d_name[256]; /* Entry name (null-terminated) */
};

/*
 * File types (d_type)
 */
#define DT_UNKNOWN       0
#define DT_FIFO          1
#define DT_CHR           2
#define DT_DIR           4
#define DT_BLK           6
#define DT_REG           8
#define DT_LNK          10
#define DT_SOCK         12
#define DT_WHT          14

#endif /* _SYS_DIRENT_H */
