#ifndef _SYS_LDT_H
#define _SYS_LDT_H

#include <stdint.h>

/*
 * Linux-compatible user_desc structure for modify_ldt
 */
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

/* modify_ldt commands */
#define LDT_READ        0
#define LDT_WRITE       1
#define LDT_READ_DEFAULT 2

#define LDT_ENTRIES     8192
#define LDT_ENTRY_SIZE  8

/* Function declarations */
struct process;
void ldt_init_process(struct process *proc);
void ldt_free_process(struct process *proc);
void ldt_activate(struct process *proc);
int sys_modify_ldt(int func, struct user_desc *ptr, unsigned long bytecount);
void fill_ldt_entry(void *entry, struct user_desc *info);

#endif /* _SYS_LDT_H */
