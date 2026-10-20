/*
 * bigtrunc — shrinking a file from 4 GiB or more keeps the right size.
 *
 * ext2's shrink path stored only the low 32 bits of the new size, leaving
 * the high word (i_size_high) as it was: a 5 GiB file truncated to 1 MiB
 * came back as 4 GiB + 1 MiB the next time it was looked up.
 *
 * The file is grown sparsely (ftruncate), so no disk space is needed.
 *
 * Usage: bigtrunc [dir]   (default /tmp).  Runs on the host too.
 */
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char path[256];
    struct stat st;
    int fails = 0;

    if (sizeof(off_t) < 8) {
        printf("bigtrunc: SKIP (32-bit off_t)\n");
        return 0;
    }
    snprintf(path, sizeof(path), "%s/bigtrunc.%d", argc > 1 ? argv[1] : "/tmp",
             (int)getpid());
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(path);
        return 2;
    }
    const off_t big = (off_t)5 << 30, small = (off_t)1 << 20;
    if (ftruncate(fd, big) != 0 || fstat(fd, &st) != 0 || st.st_size != big) {
        printf("FAIL  grow to 5 GiB\n");
        fails++;
    }
    if (ftruncate(fd, small) != 0) {
        printf("FAIL  shrink to 1 MiB\n");
        fails++;
    }
    close(fd);
    if (stat(path, &st) != 0 || st.st_size != small) {
        printf("FAIL  size after shrinking: %lld, not %lld\n",
               (long long)st.st_size, (long long)small);
        fails++;
    }
    unlink(path);
    printf("bigtrunc: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
