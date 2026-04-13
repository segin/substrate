#include <dirent.h>
#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

extern int _syscall3(int, uintptr_t, uintptr_t, uintptr_t);

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
    struct dirent *d;
    size_t reclen;

    if (dirp == NULL) {
        errno = EINVAL;
        return NULL;
    }

    if (dirp->buf_pos >= dirp->buf_end) {
        // Refill buffer
        int ret = _syscall3(SYS_GETDENTS, dirp->fd, (uintptr_t)dirp->buf, sizeof(dirp->buf));
        if (ret <= 0) return NULL;
        dirp->buf_end = ret;
        dirp->buf_pos = 0;
    }

    if (dirp->buf_end - dirp->buf_pos < (int)offsetof(struct dirent, d_name)) {
        errno = EIO;
        return NULL;
    }

    d = (struct dirent *)(dirp->buf + dirp->buf_pos);
    reclen = d->d_reclen;
    if (reclen < offsetof(struct dirent, d_name) ||
        reclen > (size_t)(dirp->buf_end - dirp->buf_pos)) {
        errno = EIO;
        return NULL;
    }

    dirp->buf_pos += (int)reclen;
    return d;
}

int closedir(DIR *dirp) {
    close(dirp->fd);
    free(dirp);
    return 0;
}
