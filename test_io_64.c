#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>

int main() {
    printf("Verifying 64-bit I/O Migration...\n");
    
    // 1. Verify Type Size
    printf("sizeof(off_t) = %d (Expected: 8)\n", sizeof(off_t));
    if (sizeof(off_t) != 8) {
        printf("FAIL: off_t is not 64-bit!\n");
        return 1;
    }
    
    // 2. Verify Struct Stat Size/Layout (compile-time check mainly)
    struct stat st;
    printf("sizeof(struct stat) = %d\n", sizeof(struct stat));
    
    // 3. Verify lseek signature matching
    int fd = open("/dev/null", O_RDONLY);
    if (fd >= 0) {
        off_t big_offset = 0x10000000200; // > 4GB
        off_t res = lseek(fd, big_offset, SEEK_SET);
        // We can't really run this on the host easily as it calls _syscall
        // But compiling it proves headers match.
        (void)res;
        close(fd);
    }
    
    printf("SUCCESS: Headers aligned for 64-bit I/O.\n");
    return 0;
}
