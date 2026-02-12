#include <stdbool.h>
#include <stddef.h>
#include <exec/perso/personality.h"
#include <arch/i386/syscall.h"

extern int sys_exit(int);
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);
extern void *sys_mmap(void*, size_t, int, int, int, uint64_t);

bool test_svr3_personality_table(void) {
    if (personality_svr3.syscall_table[1] != &sys_exit) return false;
    if (personality_svr3.syscall_table[3] != &sys_read) return false;
    if (personality_svr3.syscall_table[18] == NULL) return false; // stat
    return true;
}

bool test_svr4_personality_table(void) {
    if (personality_svr4.syscall_table[1] != &sys_exit) return false;
    if (personality_svr4.syscall_table[91] != &sys_mmap) return false;
    if (personality_svr4.syscall_table[105] == NULL) return false; // sigaction
    return true;
}