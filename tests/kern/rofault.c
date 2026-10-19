/*
 * rofault — kernel accesses to user memory that fault return EFAULT.
 *
 * futex_cmpxchg_user and copyinstr pointed their fault recovery at a C
 * label (&&fault).  The compiler may place such a label anywhere, so a
 * fault could resume into the access itself and fault again forever: a
 * PI futex on a present read-only page livelocked the kernel.
 *
 *   A. FUTEX_LOCK_PI and FUTEX_UNLOCK_PI on a read-only page -> EFAULT;
 *   B. open() of a path whose bytes run into an unmapped page -> EFAULT.
 *
 * On a kernel with the bug, A never returns (the harness times out).
 * Substrate-only (native SYS_FUTEX).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* A: a present, read-only page holding a nonzero "owner" word. */
    uint32_t *ro = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(ro != MAP_FAILED, "mmap");
    if (ro == MAP_FAILED) {
        printf("rofault: FAIL\n");
        return 1;
    }
    ro[0] = 12345;                  /* fault it in, not our tid */
    check(mprotect(ro, 4096, PROT_READ) == 0, "mprotect read-only");
    long r = syscall(SYS_FUTEX, (long)ro, FUTEX_LOCK_PI, 0, 0, 0, 0);
    printf("      FUTEX_LOCK_PI -> %ld\n", r);
    check(r == -EFAULT, "FUTEX_LOCK_PI on a read-only page -> EFAULT");
    r = syscall(SYS_FUTEX, (long)ro, FUTEX_TRYLOCK_PI, 0, 0, 0, 0);
    printf("      FUTEX_TRYLOCK_PI -> %ld\n", r);
    check(r == -EFAULT, "FUTEX_TRYLOCK_PI on a read-only page -> EFAULT");

    /* B: "abc" at the very end of a page, the next page unmapped. */
    char *two = mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(two != MAP_FAILED, "mmap two pages");
    if (two != MAP_FAILED) {
        memcpy(two + 4096 - 3, "abc", 3);   /* no terminator */
        munmap(two + 4096, 4096);
        errno = 0;
        int fd = open(two + 4096 - 3, O_RDONLY);
        printf("      open -> %d errno %d\n", fd, errno);
        check(fd == -1 && errno == EFAULT,
              "open of a path running into an unmapped page -> EFAULT");
    }

    printf("rofault: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
