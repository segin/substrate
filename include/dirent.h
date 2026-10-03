#ifndef _DIRENT_H
#define _DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdint.h>

#if defined(__x86_64__)
/* The amd64 ABI's directory record (docs/specs/abi-amd64.md, section 5):
 * FreeBSD's, and what getdents returns to a 64-bit process. */
struct dirent {
    uint64_t       d_fileno;
    int64_t        d_off;
    uint16_t       d_reclen;
    uint8_t        d_type;
    uint8_t        d_pad0;
    uint16_t       d_namlen;
    uint16_t       d_pad1;
    char           d_name[256];
};
#define d_ino d_fileno

/* File types in d_type (the kernel's, sys/include/sys/dirent.h).  Only
 * the 64-bit record has the member; _DIRENT_HAVE_D_TYPE says so. */
#define _DIRENT_HAVE_D_TYPE
#define DT_UNKNOWN       0
#define DT_FIFO          1
#define DT_CHR           2
#define DT_DIR           4
#define DT_BLK           6
#define DT_REG           8
#define DT_LNK          10
#define DT_SOCK         12
#define DT_WHT          14
#else
struct dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[256];
};
#endif

typedef struct {
    int fd;
    char buf[1024];
    int buf_pos;
    int buf_end;
} DIR;

DIR *opendir(const char *name);
DIR *fdopendir(int fd);
struct dirent *readdir(DIR *dirp);
int readdir_r(DIR *dirp, struct dirent *entry, struct dirent **result);
int closedir(DIR *dirp);
int dirfd(DIR *dirp);
void rewinddir(DIR *dirp);
long telldir(DIR *dirp);
void seekdir(DIR *dirp, long loc);
int  scandir(const char *dir, struct dirent ***namelist,
             int (*filter)(const struct dirent *),
             int (*cmp)(const struct dirent **, const struct dirent **));
int  alphasort(const struct dirent **a, const struct dirent **b);

#ifdef __cplusplus
}
#endif
#endif
