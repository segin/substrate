#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <stdarg.h>

#define _SYS_SYSCALL_H
#define SYS_MODIFY_LDT 123

// LDT defines from Substrate
#define LDT_ENTRIES 8192
#define LDT_ENTRY_SIZE 8
#define LDT_READ 0
#define LDT_WRITE 1

// We need the fallback definition of user_desc without sys/ldt.h
#define _SYS_LDT_H
struct user_desc {
    unsigned int  entry_number;
    unsigned int  base_addr;
    unsigned int  limit;
    unsigned int  seg_32bit:1;
    unsigned int  contents:2;
    unsigned int  read_exec_only:1;
    unsigned int  limit_in_pages:1;
    unsigned int  seg_not_present:1;
    unsigned int  useable:1;
#ifdef __x86_64__
    unsigned int  lm:1;
#endif
};

// Mock LDT table
static uint8_t mock_ldt[LDT_ENTRIES * LDT_ENTRY_SIZE];

long syscall_mock(long number, ...);
#define syscall syscall_mock

int ldtctl_main(int argc, char *argv[]);
#define main ldtctl_main
#include "../../../bin/ldtctl/ldtctl.c"
#undef main

long syscall_mock(long number, ...) {
    if (number == SYS_MODIFY_LDT) {
        va_list ap;
        va_start(ap, number);
        int func = va_arg(ap, int);
        uintptr_t ptr = va_arg(ap, uintptr_t);
        unsigned long bytecount = va_arg(ap, unsigned long);
        va_end(ap);
        
        if (func == LDT_READ) {
            unsigned long to_copy = bytecount;
            if (to_copy > sizeof(mock_ldt)) to_copy = sizeof(mock_ldt);
            memcpy((void*)ptr, mock_ldt, to_copy);
            return sizeof(mock_ldt); // Return size of LDT
        } else if (func == LDT_WRITE) {
            if (bytecount != sizeof(struct user_desc)) return -1;
            struct user_desc *desc = (struct user_desc *)ptr;
            if (desc->entry_number >= LDT_ENTRIES) return -1;
            
            uint32_t *entry = (uint32_t*)&mock_ldt[desc->entry_number * LDT_ENTRY_SIZE];
            if (desc->read_exec_only && desc->seg_not_present) {
                // Clear
                entry[0] = 0;
                entry[1] = 0;
                return 0;
            }
            
            // Build segment
            uint32_t base = desc->base_addr;
            uint32_t limit = desc->limit;
            uint32_t a = (base & 0xffff) << 16 | (limit & 0xffff);
            uint32_t b = (base & 0xff000000) | ((base & 0xff0000) >> 16);
            b |= (limit & 0xf0000);
            
            int p = desc->seg_not_present ? 0 : 1;
            int type = (desc->contents << 2) | (!desc->read_exec_only << 1);
            if (desc->contents == 2) type |= 0x08; // code
            else type |= 0x10; // data (actually in real CPU code/data bit is 0x10 and code=1 data=0, but ldtctl.c only reads type & 0x1f)
            
            b |= (type & 0x1f) << 8;
            b |= 0x1000; // S bit = 1 (user)
            b |= (3 << 13); // DPL = 3
            b |= (p << 15);
            b |= (desc->useable << 20);
            b |= (desc->limit_in_pages << 23);
            b |= (desc->seg_32bit << 22);
            
            entry[0] = a;
            entry[1] = b;
            return 0;
        }
    }
    return -1;
}

void test_write_and_read(void) {
    memset(mock_ldt, 0, sizeof(mock_ldt));
    
    char *argv_write[] = {"ldtctl", "write", "1", "0x12345678", "0xdead", "--32", "--rx", "--contents=2"};
    int ret = ldtctl_main(8, argv_write);
    assert(ret == 0);
    
    // Check mock array structure
    uint32_t *entry = (uint32_t*)&mock_ldt[1 * 8];
    assert(entry[0] != 0 || entry[1] != 0); // Not empty!
    
    // Now verify via read
    char temp_file[] = "/tmp/ldt_test_XXXXXX";
    int fd = mkstemp(temp_file);
    assert(fd != -1);
    
    int old_stdout = dup(1);
    dup2(fd, 1);
    
    char *argv_read[] = {"ldtctl", "read", "1"};
    ldtctl_main(3, argv_read);
    
    fflush(stdout);
    dup2(old_stdout, 1);
    close(old_stdout);
    
    lseek(fd, 0, SEEK_SET);
    char buf[1024];
    memset(buf, 0, sizeof(buf));
    read(fd, buf, sizeof(buf)-1);
    close(fd);
    unlink(temp_file);
    
    assert(strstr(buf, "Base=0x12345678") != NULL);
    assert(strstr(buf, "Limit=0x0000dead") != NULL);
    assert(strstr(buf, "D/B=1") != NULL); // 32-bit
    assert(strstr(buf, "P=1") != NULL);
    
    // Test clear
    char *argv_clear[] = {"ldtctl", "clear", "1"};
    ldtctl_main(3, argv_clear);
    
    assert(entry[0] == 0 && entry[1] == 0);
    
    printf("ldtctl integration tests passed.\n");
}

int main(void) {
    test_write_and_read();
    return 0;
}
