#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

#include <sys/types.h>

struct stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint32_t st_size;
    uint32_t st_blksize;
    uint32_t st_blocks;
    uint32_t st_atime;
    uint32_t st_atime_nsec;
    uint32_t st_mtime;
    uint32_t st_mtime_nsec;
    uint32_t st_ctime;
    uint32_t st_ctime_nsec;
};

/* Mock out sys/syscall_impl.h */
#define _SYS_SYSCALL_IMPL_H
/* Mock out sys/stat.h */
#define _SYS_STAT_H

/* Mock external functions that compat.c calls */
int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence) { return 0; }
int sys_time(void *t) { return 0; }
void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, int offset) { return NULL; }
int sys_execve(const char *path, char *const argv[], char *const envp[]) { return 0; }
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) { return 0; }
int copyout(const void *kaddr, void *uaddr, size_t len) { return 0; }

int kern_stat(const char *path, struct stat *st) { return 0; }
int kern_lstat(const char *path, struct stat *st) { return 0; }
int kern_fstat(int fd, struct stat *st) { return 0; }

/* Now we can include compat.c directly! */
#include "../../sys/exec/perso/compat.c"

int main() {
    printf("Testing sys_nice()...\n");

    // Assert sys_nice returns -ENOSYS
    assert(sys_nice(0) == -ENOSYS);
    assert(sys_nice(1) == -ENOSYS);
    assert(sys_nice(-1) == -ENOSYS);
    assert(sys_nice(100) == -ENOSYS);

    printf("All sys_nice tests passed!\n");
    return 0;
}
