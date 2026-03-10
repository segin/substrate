#include <kern/console.h>
#include <sys/ldt.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <string.h>
#include "tests.h"

extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint32_t offset);
extern int sys_munmap(void *addr, size_t length);

void run_ldt_tests(void) {
    kprint("Running LDT tests...\n");

    // Allocate "user" memory for testing
    void *user_mem = sys_mmap(NULL, 65536, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (user_mem == MAP_FAILED) {
        kprint("FAIL: sys_mmap failed, cannot allocate user memory for LDT test\n");
        return;
    }

    // Test 1: LDT_READ on empty LDT
    int ret = sys_modify_ldt(LDT_READ, (struct user_desc *)user_mem, 65536);
    if (ret < 0) {
        kprintf("FAIL: LDT_READ returned error %d\n", ret);
    } else {
        kprintf("PASS: LDT_READ on (potentially) empty LDT returned %d\n", ret);
    }

    // Test 2: LDT_READ_DEFAULT
    ret = sys_modify_ldt(LDT_READ_DEFAULT, (struct user_desc *)user_mem, 65536);
    if (ret != 0) {
        kprintf("FAIL: LDT_READ_DEFAULT returned %d (expected 0)\n", ret);
    } else {
        kprint("PASS: LDT_READ_DEFAULT returned 0\n");
    }

    // Test 3: LDT_WRITE followed by LDT_READ
    struct user_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.entry_number = 0;
    desc.base_addr = 0x12345678;
    desc.limit = 0xabcde;
    desc.seg_32bit = 1;
    desc.contents = 0; // Data
    desc.limit_in_pages = 1;

    // Copy desc to user memory so sys_modify_ldt(LDT_WRITE) can read it via copyin
    memcpy(user_mem, &desc, sizeof(desc));

    ret = sys_modify_ldt(LDT_WRITE, (struct user_desc *)user_mem, sizeof(desc));
    if (ret != 0) {
        kprintf("FAIL: LDT_WRITE returned %d\n", ret);
    } else {
        kprint("PASS: LDT_WRITE succeeded\n");

        // Now read it back
        void *read_buf = (char *)user_mem + 4096;
        memset(read_buf, 0, 8); // Clear the destination first

        ret = sys_modify_ldt(LDT_READ, (struct user_desc *)read_buf, 8);
        if (ret != 8) {
            kprintf("FAIL: LDT_READ back returned %d (expected 8)\n", ret);
        } else {
            // Verify the descriptor.
            uint8_t *b = (uint8_t *)read_buf;
            bool match = (b[0] == 0xde && b[1] == 0xbc && // limit_low
                          b[2] == 0x78 && b[3] == 0x56 && // base_low
                          b[4] == 0x34 &&                 // base_middle
                          b[5] == 0xf2 &&                 // access
                          b[6] == 0xca &&                 // granularity
                          b[7] == 0x12);                  // base_high

            if (match) {
                kprint("PASS: LDT_READ back verified content\n");
            } else {
                kprintf("FAIL: LDT_READ back content mismatch: %02x%02x%02x%02x %02x%02x%02x%02x\n",
                        b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
            }
        }
    }

    // Test 4: Small buffer read
    ret = sys_modify_ldt(LDT_READ, (struct user_desc *)((char*)user_mem + 8192), 4);
    if (ret != 4) {
        kprintf("FAIL: LDT_READ with 4-byte buffer returned %d\n", ret);
    } else {
        kprint("PASS: LDT_READ with 4-byte buffer returned 4\n");
    }

    sys_munmap(user_mem, 65536);
    kprint("LDT tests complete.\n");
}
