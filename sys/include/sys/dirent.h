/*
 * sys/dirent.h - Kernel directory entry definitions
 */

#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>
#include <sys/abi32.h>

/* The i386 layout on both kernels (<sys/abi32.h>): getdents copies these
 * out whole. */
struct dirent {
    abi_uint64_t d_ino;   /* File serial number */
    abi_uint64_t d_off;   /* Opaque cursor for the NEXT entry.  A filesystem
                           * that has a deletion-stable position (ext2 uses a
                           * byte offset) reports it here so a reader that
                           * unlink()s entries mid-scan never skips surviving
                           * names; index-based filesystems leave it 0 and the
                           * getdents layer falls back to a +1 entry counter. */
    uint16_t d_reclen;    /* Length of this record */
    uint8_t  d_type;      /* File type, see below */
    uint8_t  d_namlen;    /* Length of string in d_name */
    char     d_name[256]; /* Entry name (null-terminated) */
};
ABI32_ASSERT_SIZE(struct dirent, 276);

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
