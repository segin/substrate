#include <sys/types.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

/* Syscall prototypes */
extern int sys_open(const char *path, int flags, int mode);
extern int sys_close(int fd);
extern int sys_read(int fd, char *buf, int len);
extern int sys_write(int fd, const char *buf, int len);
extern int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int w);
extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset);
extern int sys_munmap(void *addr, size_t length);
extern int sys_fstat(int fd, struct stat *buf);

static int parse_phys_addr(uintptr_t *phys, size_t *size) {
    int fd = sys_open("/dev/mem_test", O_RDONLY, 0);
    if (fd < 0) {
        kprint("test_mem: Failed to open /dev/mem_test\n");
        return -1;
    }

    char buf[128];
    memset(buf, 0, sizeof(buf));
    int len = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (len <= 0) return -1;

    /* Parse PHYS_ADDR=0xABC... */
    char *ptr = buf;
    if (strncmp(ptr, "PHYS_ADDR=0x", 12) != 0) return -1;
    ptr += 12;

    uintptr_t p = 0;
    while (*ptr) {
        if (*ptr >= '0' && *ptr <= '9') p = (p << 4) | (*ptr - '0');
        else if (*ptr >= 'A' && *ptr <= 'F') p = (p << 4) | (*ptr - 'A' + 10);
        else if (*ptr >= 'a' && *ptr <= 'f') p = (p << 4) | (*ptr - 'a' + 10);
        else break; /* Newline or other */
        ptr++;
    }
    *phys = p;

    /* Find SIZE= */
    while (*ptr && *ptr != '\n') ptr++;
    if (*ptr == '\n') ptr++;

    if (strncmp(ptr, "SIZE=", 5) == 0) {
        ptr += 5;
        size_t s = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            s = s * 10 + (*ptr - '0');
            ptr++;
        }
        *size = s;
    } else {
        *size = 4096; /* Default */
    }

    return 0;
}

int test_mem(void) {
    kprint("TEST: /dev/mem\n");

    uintptr_t phys_addr = 0;
    size_t page_size = 0;

    if (parse_phys_addr(&phys_addr, &page_size) != 0) {
        kprint("FAIL: Could not get test page info\n");
        return -1;
    }

    /* Test 1: Open /dev/mem */
    int fd = sys_open("/dev/mem", O_RDWR, 0);
    if (fd < 0) {
        kprint("FAIL: sys_open /dev/mem\n");
        return -1;
    }

    /* Test 2: Seek to PA */
    sys_lseek(fd, phys_addr, 0, SEEK_SET);

    /* Test 3: Read and Verify Pattern */
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int bytes = sys_read(fd, buf, 16); /* MEM_TEST_PATTERN is 16 chars */

    if (bytes != 16) {
        kprint("FAIL: Short read\n");
        sys_close(fd);
        return -1;
    }

    if (strncmp(buf, "MEM_TEST_PATTERN", 16) != 0) {
        kprint("FAIL: Pattern mismatch: ");
        kprint(buf);
        kprint("\n");
        sys_close(fd);
        return -1;
    }

    /* Test 4: Write new pattern */
    sys_lseek(fd, phys_addr, 0, SEEK_SET);
    const char *new_pat = "UPDATED_PATTERN_";
    sys_write(fd, new_pat, 16);

    sys_lseek(fd, phys_addr, 0, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    sys_read(fd, buf, 16);

    if (strncmp(buf, new_pat, 16) != 0) {
        kprint("FAIL: Write verification failed\n");
        sys_close(fd);
        return -1;
    }

    /* Test 5: mmap */
    /* Map 1 page at offset phys_addr */
    void *map_addr = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys_addr);
    if (map_addr == (void*)-1) {
        kprint("FAIL: mmap failed\n");
        sys_close(fd);
        return -1;
    }

    /* Verify mapped data matches new pattern */
    if (strncmp((char*)map_addr, new_pat, 16) != 0) {
        kprint("FAIL: mmap read mismatch\n");
        sys_munmap(map_addr, 4096);
        sys_close(fd);
        return -1;
    }

    /* Write via mmap */
    const char *mmap_pat = "MMAP_WRITE_TEST_";
    memcpy(map_addr, mmap_pat, 16);

    /* Verify via read */
    sys_lseek(fd, phys_addr, 0, SEEK_SET);
    memset(buf, 0, sizeof(buf));
    sys_read(fd, buf, 16);

    if (strncmp(buf, mmap_pat, 16) != 0) {
        kprint("FAIL: mmap write not visible via read\n");
        sys_munmap(map_addr, 4096);
        sys_close(fd);
        return -1;
    }

    sys_munmap(map_addr, 4096);

    /* Test 6: fstat */
    struct stat st;
    if (sys_fstat(fd, &st) != 0) {
        kprint("FAIL: fstat\n");
        sys_close(fd);
        return -1;
    }
    if (!(st.st_mode & S_IFCHR)) {
        kprint("FAIL: Not S_IFCHR\n");
        sys_close(fd);
        return -1;
    }

    sys_close(fd);
    kprint("PASS: /dev/mem\n");
    return 0;
}
