#ifndef _DIRENT_H
#define _DIRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <stdint.h>

struct dirent {
    unsigned long  d_ino;
    unsigned long  d_off;
    unsigned short d_reclen;
    char           d_name[256];
};

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
