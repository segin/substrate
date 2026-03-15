#ifndef _SUBSTRATE_SYS_LDT_H
#define _SUBSTRATE_SYS_LDT_H

#include <sys/types.h>

struct user_desc {
    unsigned int entry_number;
    unsigned int base_addr;
    unsigned int limit;
    unsigned int seg_32bit:1;
    unsigned int contents:2;
    unsigned int read_exec_only:1;
    unsigned int limit_in_pages:1;
    unsigned int seg_not_present:1;
    unsigned int useable:1;
#ifdef __x86_64__
    unsigned int lm:1;
#endif
};

#define LDT_READ         0
#define LDT_WRITE        1
#define LDT_READ_DEFAULT 2

#define LDT_ENTRIES      8192
#define LDT_ENTRY_SIZE   8

int modify_ldt(int func, void *ptr, unsigned long bytecount);

#endif
