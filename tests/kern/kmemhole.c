/*
 * kmemhole — /dev/kmem reads an unmapped kernel page as EFAULT, not a
 * kernel page fault.
 *
 * kmem_read copied from any address at or above the kernel's base with a
 * plain memcpy: an unmapped page there faulted in kernel mode.  Reads a
 * page at each of several kernel addresses, some of them in holes; every
 * read must return the page or fail with EFAULT, at least one must fail
 * (a hole was hit), and the process must live to say so.  (On x86_64 the
 * kernel lies above any positive off_t, so these 32-bit addresses are all
 * EFAULT there.)  Run as root.  Substrate only.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    static const uint64_t addrs[] = {
        0xC0100000ull,              /* the kernel image (i386) */
        0xE8000000ull, 0xF0000000ull, 0xF8000000ull, 0xFF000000ull,
    };
    unsigned char page[4096];
    int ok = 0, efault = 0, other = 0;

    int fd = open("/dev/kmem", O_RDONLY);
    if (fd < 0) {
        printf("kmemhole: open /dev/kmem failed (errno %d): FAIL\n", errno);
        return 2;
    }
    for (unsigned i = 0; i < sizeof(addrs) / sizeof(addrs[0]); i++) {
        if (lseek(fd, (off_t)addrs[i], SEEK_SET) == (off_t)-1)
            continue;               /* not an address on this kernel */
        errno = 0;
        ssize_t n = read(fd, page, sizeof(page));
        printf("  0x%llx: %zd (errno %d)\n", (unsigned long long)addrs[i], n, errno);
        if (n > 0) ok++;
        else if (n < 0 && errno == EFAULT) efault++;
        else other++;
    }
    close(fd);
    int pass = efault > 0 && other == 0;
    printf("kmemhole: %d read, %d EFAULT, %d other: %s\n", ok, efault, other,
           pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
