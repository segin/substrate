/*
 * fatops -- FAT truncate, case-only rename and 4 GiB offsets.
 *
 * Usage: fatops <directory on a FAT mount>
 *
 *   grow     an empty file truncated up to 10000 bytes reads back as
 *            zeros (it had no cluster, so it read the volume's sector 0,
 *            and the end-of-chain mark went into FAT entry 0);
 *   rename   "foo.txt" -> "FOO.TXT" keeps the file (the case-insensitive
 *            lookup found the source as the destination and freed it);
 *   4gib     a write at 4 GiB fails, and does not land at offset 0 (the
 *            offset was cut to 32 bits).
 *
 * Run fsck.fat -n on the image afterwards.  Prints a "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static void path(char *buf, size_t sz, const char *dir, const char *name)
{
    snprintf(buf, sz, "%s/%s", dir, name);
}

static void test_grow(const char *dir)
{
    static unsigned char buf[10000];
    char p[256];
    int fd, zero = 1;
    ssize_t n;

    path(p, sizeof(p), dir, "grow.bin");
    fd = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
    check(fd >= 0, "grow: create an empty file");
    if (fd < 0)
        return;
    check(ftruncate(fd, sizeof(buf)) == 0, "grow: ftruncate to 10000");
    memset(buf, 0xAA, sizeof(buf));
    n = pread(fd, buf, sizeof(buf), 0);
    for (size_t i = 0; i < sizeof(buf); i++)
        if (buf[i] != 0)
            zero = 0;
    check(n == (ssize_t)sizeof(buf) && zero, "grow: reads back as zeros");
    if (!zero)
        printf("      first bytes %02x %02x %02x\n", buf[0], buf[1], buf[2]);
    close(fd);
}

static void test_rename(const char *dir)
{
    char from[256], to[256], got[64];
    const char msg[] = "the file that must survive its rename";
    int fd;
    ssize_t n;

    path(from, sizeof(from), dir, "foo.txt");
    path(to, sizeof(to), dir, "FOO.TXT");
    fd = open(from, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, msg, sizeof(msg)) != (ssize_t)sizeof(msg)) {
        check(0, "rename: create foo.txt");
        return;
    }
    close(fd);
    check(rename(from, to) == 0, "rename: foo.txt -> FOO.TXT");
    fd = open(to, O_RDONLY);
    memset(got, 0, sizeof(got));
    n = fd >= 0 ? read(fd, got, sizeof(got)) : -1;
    if (fd >= 0)
        close(fd);
    check(n == (ssize_t)sizeof(msg) && memcmp(got, msg, sizeof(msg)) == 0,
          "rename: contents intact");
}

static void test_4gib(const char *dir)
{
    char p[256], got[8];
    int fd;
    ssize_t n;

    path(p, sizeof(p), dir, "wrap.bin");
    fd = open(p, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0 || write(fd, "ABCDEFGH", 8) != 8) {
        check(0, "4gib: create");
        return;
    }
    n = pwrite(fd, "XXXX", 4, 0x100000000LL);
    check(n != 4, "4gib: a write at 4 GiB does not succeed");
    memset(got, 0, sizeof(got));
    n = pread(fd, got, 8, 0);
    check(n == 8 && memcmp(got, "ABCDEFGH", 8) == 0,
          "4gib: offset 0 was not overwritten");
    close(fd);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: fatops <dir on FAT>\n");
        return 2;
    }
    test_grow(argv[1]);
    test_rename(argv[1]);
    test_4gib(argv[1]);
    printf("Result: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
