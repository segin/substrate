#include <sys/types.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

extern int sys_open(const char *path, int flags, int mode);
extern int sys_close(int fd);
extern int sys_read(int fd, char *buf, int len);
extern int sys_write(int fd, const char *buf, int len);
extern int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int w);
extern int sys_ioctl(int fd, uint32_t request, void *arg);
extern int sys_poll(struct pollfd *fds, unsigned int nfds, int timeout);
extern int sys_fstat(int fd, struct stat *buf);
extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset);
extern int sys_munmap(void *addr, size_t length);

static int check_all_zero(const uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0) {
            return 0;
        }
    }
    return 1;
}

static int test_null_device(void) {
    int fd = sys_open("/dev/null", O_RDWR, 0);
    if (fd < 0) {
        kprint("FAIL: /dev/null open\n");
        return -1;
    }

    uint8_t buf[64];
    memset(buf, 0xAA, sizeof(buf));

    if (sys_read(fd, (char *)buf, sizeof(buf)) != 0) {
        kprint("FAIL: /dev/null read != EOF\n");
        sys_close(fd);
        return -1;
    }

    if (sys_write(fd, "abcd", 4) != 4) {
        kprint("FAIL: /dev/null write did not accept full count\n");
        sys_close(fd);
        return -1;
    }

    if (sys_lseek(fd, 1234, 0, SEEK_SET) != 1234) {
        kprint("FAIL: /dev/null lseek\n");
        sys_close(fd);
        return -1;
    }

    if (sys_ioctl(fd, 0x1234, NULL) != -ENOTTY) {
        kprint("FAIL: /dev/null ioctl\n");
        sys_close(fd);
        return -1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLOUT, .revents = 0 };
    int rc = sys_poll(&pfd, 1, 0);
    if (rc <= 0 || !(pfd.revents & POLLOUT) || (pfd.revents & POLLIN)) {
        kprint("FAIL: /dev/null poll semantics\n");
        sys_close(fd);
        return -1;
    }

    struct stat st;
    if (sys_fstat(fd, &st) != 0 || !(st.st_mode & S_IFCHR)) {
        kprint("FAIL: /dev/null fstat\n");
        sys_close(fd);
        return -1;
    }

    if (sys_mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0) != (void *)-1) {
        kprint("FAIL: /dev/null mmap should fail\n");
        sys_close(fd);
        return -1;
    }

    sys_close(fd);
    return 0;
}

static int test_full_device(void) {
    int fd = sys_open("/dev/full", O_RDWR, 0);
    if (fd < 0) {
        kprint("FAIL: /dev/full open\n");
        return -1;
    }

    uint8_t buf[128];
    memset(buf, 0xAA, sizeof(buf));

    if (sys_read(fd, (char *)buf, sizeof(buf)) != (int)sizeof(buf) || !check_all_zero(buf, sizeof(buf))) {
        kprint("FAIL: /dev/full read not zero-filled\n");
        sys_close(fd);
        return -1;
    }

    if (sys_write(fd, "abcd", 4) != -ENOSPC) {
        kprint("FAIL: /dev/full write expected ENOSPC\n");
        sys_close(fd);
        return -1;
    }

    if (sys_lseek(fd, 4096, 0, SEEK_SET) != 4096) {
        kprint("FAIL: /dev/full lseek\n");
        sys_close(fd);
        return -1;
    }

    if (sys_ioctl(fd, 0x1234, NULL) != -ENOTTY) {
        kprint("FAIL: /dev/full ioctl\n");
        sys_close(fd);
        return -1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLOUT, .revents = 0 };
    int rc = sys_poll(&pfd, 1, 0);
    if (rc <= 0 || !(pfd.revents & POLLIN) || !(pfd.revents & POLLOUT)) {
        kprint("FAIL: /dev/full poll semantics\n");
        sys_close(fd);
        return -1;
    }

    if (sys_mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0) != (void *)-1) {
        kprint("FAIL: /dev/full mmap should fail\n");
        sys_close(fd);
        return -1;
    }

    sys_close(fd);
    return 0;
}

static int test_zero_device(void) {
    int fd = sys_open("/dev/zero", O_RDWR, 0);
    if (fd < 0) {
        kprint("FAIL: /dev/zero open\n");
        return -1;
    }

    uint8_t buf[256];
    memset(buf, 0xAA, sizeof(buf));

    if (sys_read(fd, (char *)buf, sizeof(buf)) != (int)sizeof(buf) || !check_all_zero(buf, sizeof(buf))) {
        kprint("FAIL: /dev/zero read not zero-filled\n");
        sys_close(fd);
        return -1;
    }

    if (sys_write(fd, "abcd", 4) != 4) {
        kprint("FAIL: /dev/zero write did not accept full count\n");
        sys_close(fd);
        return -1;
    }

    if (sys_lseek(fd, 8192, 0, SEEK_SET) != 8192) {
        kprint("FAIL: /dev/zero lseek\n");
        sys_close(fd);
        return -1;
    }

    if (sys_ioctl(fd, 0x1234, NULL) != -ENOTTY) {
        kprint("FAIL: /dev/zero ioctl\n");
        sys_close(fd);
        return -1;
    }

    struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLOUT, .revents = 0 };
    int rc = sys_poll(&pfd, 1, 0);
    if (rc <= 0 || !(pfd.revents & POLLIN) || !(pfd.revents & POLLOUT)) {
        kprint("FAIL: /dev/zero poll semantics\n");
        sys_close(fd);
        return -1;
    }

    void *map = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (map == (void *)-1) {
        kprint("FAIL: /dev/zero mmap failed\n");
        sys_close(fd);
        return -1;
    }
    if (!check_all_zero((const uint8_t *)map, 4096)) {
        kprint("FAIL: /dev/zero mmap not zero-filled\n");
        sys_munmap(map, 4096);
        sys_close(fd);
        return -1;
    }
    sys_munmap(map, 4096);

    sys_close(fd);
    return 0;
}

void run_devfs_special_device_tests(void) {
    kprint("TEST: /dev/null /dev/full /dev/zero\n");

    if (test_null_device() != 0 || test_full_device() != 0 || test_zero_device() != 0) {
        kprint("FAIL: special device semantics\n");
        return;
    }

    kprint("PASS: special device semantics\n");
}
