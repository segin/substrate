#ifndef _DIRENT_H
#define _DIRENT_H

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
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);

#endif
