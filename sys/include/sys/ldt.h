#ifndef _SYS_LDT_H
#define _SYS_LDT_H

#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <arch/i386/gdt.h>

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

static inline uint32_t ldt_entry_base(const void *entry_ptr) {
    const gdt_entry_t *entry = (const gdt_entry_t *)entry_ptr;

    return (uint32_t)entry->base_low |
           ((uint32_t)entry->base_middle << 16) |
           ((uint32_t)entry->base_high << 24);
}

static inline uint32_t ldt_entry_limit(const void *entry_ptr) {
    const gdt_entry_t *entry = (const gdt_entry_t *)entry_ptr;
    uint32_t limit = (uint32_t)entry->limit_low |
                     (((uint32_t)entry->granularity & 0x0FU) << 16);

    if (entry->granularity & 0x80U) {
        limit = (limit << 12) | 0xFFFU;
    }
    return limit;
}

static inline int ldt_translate_selector_offset(const void *ldt,
                                                unsigned int entry_count,
                                                uint16_t selector,
                                                uint16_t offset,
                                                uintptr_t *linear_out) {
    unsigned int index;
    const gdt_entry_t *entry;

    if (!ldt || !linear_out) {
        return -EINVAL;
    }
    if ((selector & 0x4U) == 0) {
        return -EINVAL;
    }

    index = (unsigned int)(selector >> 3);
    if (index >= entry_count) {
        return -EINVAL;
    }

    entry = &((const gdt_entry_t *)ldt)[index];
    if ((entry->access & 0x80U) == 0 || (entry->access & 0x10U) == 0) {
        return -EINVAL;
    }
    if ((uint32_t)offset > ldt_entry_limit(entry)) {
        return -EFAULT;
    }

    *linear_out = (uintptr_t)ldt_entry_base(entry) + (uintptr_t)offset;
    return 0;
}

#endif /* _SYS_LDT_H */
