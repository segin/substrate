/*
 * mapaligned — FreeBSD MAP_ALIGNED(n) takes only shifts FreeBSD takes.
 *
 * The personality checked only for a shift above 12: a shift of 32 or
 * more was undefined behaviour in the kernel's 1 << n, and a huge one made
 * the over-allocation wrap, so the trim unmapped a range of the caller's
 * memory it never asked for.  A shift below 12 was silently ignored.  As
 * on FreeBSD, those are EINVAL; 21 still gives a 2 MiB-aligned mapping,
 * and memory mapped beforehand survives the attempts.
 *
 * FreeBSD build; runs on FreeBSD too.
 */
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static void *map(size_t len, int shift)
{
    return mmap(NULL, len, PROT_READ | PROT_WRITE,
                MAP_ANON | MAP_PRIVATE | MAP_ALIGNED(shift), -1, 0);
}

int main(void)
{
    char *keep = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE,
                      MAP_ANON | MAP_PRIVATE, -1, 0);
    if (keep == MAP_FAILED) {
        printf("mapaligned: setup failed: FAIL\n");
        return 2;
    }
    memset(keep, 0x5a, 1 << 20);

    void *p = map(4096, 21);
    check(p != MAP_FAILED && ((uintptr_t)p & ((1u << 21) - 1)) == 0,
          "MAP_ALIGNED(21) is 2 MiB aligned");
    if (p != MAP_FAILED) {
        memset(p, 1, 4096);
        munmap(p, 4096);
    }

    p = map(5000, 16);
    check(p != MAP_FAILED && ((uintptr_t)p & 0xffff) == 0,
          "MAP_ALIGNED(16) with a length not a page multiple");
    if (p != MAP_FAILED) {
        memset(p, 1, 5000);
        munmap(p, 5000);
    }

    errno = 0;
    p = map(4096, 40);
    check(p == MAP_FAILED && errno == EINVAL, "MAP_ALIGNED(40) is EINVAL");
    errno = 0;
    p = map(4096, 32);
    check(p == MAP_FAILED && errno == EINVAL, "MAP_ALIGNED(32) is EINVAL");
    errno = 0;
    p = map(4096, 5);
    check(p == MAP_FAILED && errno == EINVAL, "MAP_ALIGNED(5) is EINVAL");

    int intact = 1;
    for (int i = 0; i < (1 << 20); i++)
        if (keep[i] != 0x5a) {
            intact = 0;
            break;
        }
    check(intact, "earlier memory is untouched");

    printf("mapaligned: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
