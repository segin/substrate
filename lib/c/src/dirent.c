#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

extern int _syscall3(int, int, int, int);

DIR *opendir(const char *name) {
    int fd = open(name, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return NULL;
    
    DIR *dir = malloc(sizeof(DIR));
    if (!dir) {
        close(fd);
        return NULL;
    }
    
    dir->fd = fd;
    dir->buf_pos = 0;
    dir->buf_end = 0;
    return dir;
}

struct dirent *readdir(DIR *dirp) {
    if (dirp->buf_pos >= dirp->buf_end) {
        // Refill buffer
        int ret = _syscall3(SYS_GETDENTS, dirp->fd, (int)dirp->buf, sizeof(dirp->buf));
        if (ret <= 0) return NULL;
        dirp->buf_end = ret;
        dirp->buf_pos = 0;
    }
    
    struct dirent *d = (struct dirent *)(dirp->buf + dirp->buf_pos);
    dirp->buf_pos += d->d_reclen;
    return d;
}

int closedir(DIR *dirp) {
    close(dirp->fd);
    free(dirp);
    return 0;
}
