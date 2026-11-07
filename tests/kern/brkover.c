/*
 * brkover -- the heap cannot grow over another mapping, and msync() of a
 * range past user space is ENOMEM.
 *
 * sys_brk() mapped the new heap pages without looking at the address
 * space: grown over an mmap placed after the break, it replaced that
 * mapping's pages (its data gone), and shrunk back, unmapped them.  And
 * msync() let addr + length wrap, taking the range for an empty one, and
 * "succeeded".  Linux refuses both alike.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    uintptr_t brk0 = (uintptr_t)sbrk(0);
    uintptr_t above = ((brk0 + 0xFFF) & ~(uintptr_t)0xFFF) + 0x2000;
    char *m = mmap((void *)above, 0x1000, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    check(m == (char *)above, "map a page two pages above the break");
    if (m == (char *)above) {
        memcpy(m, "mapped", 7);
        errno = 0;
        void *r = sbrk(0x5000);
        check(r == (void *)-1 && errno == ENOMEM, "growing the heap over it: ENOMEM");
        check(memcmp(m, "mapped", 7) == 0, "the mapping keeps its data");
        check((uintptr_t)sbrk(0) == brk0, "the break is where it was");
    }

    errno = 0;
    check(msync((void *)0xFFFFF000, 0x2000, MS_SYNC) == -1 && errno == ENOMEM,
          "msync of a range that wraps: ENOMEM");
    errno = 0;
    check(msync((void *)0xBFFFF000, 0x2000, MS_SYNC) == -1 && errno == ENOMEM,
          "msync of a range past user space: ENOMEM");

    printf("%s\n", failed ? "brkover: FAIL" : "brkover: PASS");
    return failed;
}
