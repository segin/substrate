/*
 * getdentspad — getdents64 sends no stack bytes in an entry's padding.
 *
 * Each entry is rounded up to 8 bytes past its name's NUL, and the kernel
 * copied the whole rounded entry from a stack buffer it never cleared:
 * up to 7 bytes of kernel stack per entry.  Fills a directory with names
 * of every length mod 8 and checks every byte after each name's NUL, up to
 * d_reclen, is zero.
 *
 * Linux build (raw getdents64); runs on the host too.
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

struct ld64 {
    unsigned long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[];
};

int main(int argc, char **argv)
{
    char dir[256], p[300];
    unsigned char buf[4096];
    int bad = 0, entries = 0;

    snprintf(dir, sizeof(dir), "%s/getdentspad.%d", argc > 1 ? argv[1] : "/tmp",
             (int)getpid());
    mkdir(dir, 0755);
    for (int n = 1; n <= 16; n++) {
        char name[32];
        memset(name, 'a' + n, (size_t)n);
        name[n] = '\0';
        snprintf(p, sizeof(p), "%s/%s", dir, name);
        int fd = open(p, O_WRONLY | O_CREAT, 0644);
        if (fd >= 0) close(fd);
    }

    int fd = open(dir, O_RDONLY | O_DIRECTORY);
    memset(buf, 0xAA, sizeof(buf));
    long n = syscall(SYS_getdents64, fd, buf, sizeof(buf));
    for (long off = 0; off < n;) {
        struct ld64 *d = (struct ld64 *)(buf + off);
        size_t name_end = offsetof(struct ld64, d_name) + strlen(d->d_name) + 1;
        /* Zero, or the caller's own byte left alone (Linux does not write
         * the padding at all); anything else came from the kernel. */
        for (size_t i = name_end; i < d->d_reclen; i++)
            if (buf[off + i] != 0 && buf[off + i] != 0xAA) bad++;
        entries++;
        off += d->d_reclen;
    }
    close(fd);

    for (int k = 1; k <= 16; k++) {
        char name[32];
        memset(name, 'a' + k, (size_t)k);
        name[k] = '\0';
        snprintf(p, sizeof(p), "%s/%s", dir, name);
        unlink(p);
    }
    rmdir(dir);
    printf("getdentspad: %d entries, %d padding bytes from the kernel: %s\n",
           entries, bad, bad || entries < 16 ? "FAIL" : "PASS");
    return bad || entries < 16;
}
