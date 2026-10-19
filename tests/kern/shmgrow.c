/*
 * shmgrow — growing a POSIX shared memory object while it is mapped
 * does not free the frames the mapping uses.
 *
 * shmfs keeps an object in one physically contiguous buffer; growing it
 * (write past the end, ftruncate) allocates a larger one, copies, and
 * freed the old one -- while a mapping of the object still had PTEs to
 * it.  The freed frames went back to the allocator under a live user
 * mapping.
 *
 * The test maps a page, grows the object to many pages, then allocates
 * and fills memory so freed frames are reused, and checks that the
 * mapping still holds what was written through it.  Builds for the host
 * as well (cc -o shmgrow shmgrow.c -lrt).
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#define PAGE 4096

static int failed;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

int main(void) {
    const char *name = "/shmgrow.test";
    setvbuf(stdout, NULL, _IONBF, 0);
    shm_unlink(name);
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    check(fd >= 0, "shm_open");
    if (fd < 0) return 1;
    check(ftruncate(fd, PAGE) == 0, "size it to a page");

    unsigned char *m = mmap(NULL, PAGE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    check(m != MAP_FAILED, "map it");
    if (m == MAP_FAILED) return 1;
    memset(m, 0xA5, PAGE);

    /* Grow while mapped: the object moves to a new buffer. */
    check(ftruncate(fd, 64 * PAGE) == 0, "grow it to 64 pages while mapped");

    /* Reuse whatever was freed: allocate and fill a lot of memory. */
    for (int i = 0; i < 64; i++) {
        void *p = mmap(NULL, 16 * PAGE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (p != MAP_FAILED) memset(p, 0x5A, 16 * PAGE);
    }

    int intact = 1;
    for (int i = 0; i < PAGE; i++)
        if (m[i] != 0xA5) { intact = 0; break; }
    check(intact, "the old mapping still holds what was written to it");

    munmap(m, PAGE);
    close(fd);
    shm_unlink(name);
    printf("%s\n", failed ? "shmgrow: FAIL" : "shmgrow: PASS");
    return failed;
}
