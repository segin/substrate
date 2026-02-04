#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>
#include "gdt.h"

/* GDT index 7 is reserved for the active process LDT */
#define GDT_LDT_INDEX 7

extern void kprint(const char *s);
extern int copy_from_user(void *dest, const void *src, size_t n);

/* Fallback if copy_from_user is not defined elsewhere */
#ifndef copy_from_user
#define copy_from_user(dest, src, n) (memcpy(dest, src, n), 0)
#endif

void ldt_activate(process_t *proc) {
    if (!proc || !proc->ldt) {
        /* Deactivate LDT by loading a null selector */
        __asm__ volatile("lldt %0" : : "r"((uint16_t)0));
        return;
    }

    uint32_t base = (uint32_t)proc->ldt;
    uint32_t limit = (proc->ldt_entry_count * 8) - 1;

    /* 
     * LDT descriptor in GDT:
     * Access: 0x82 (Present=1, DPL=0, Type=0x2(LDT))
     * Granularity: 0x40 (32-bit, Byte granularity)
     */
    gdt_set_gate(GDT_LDT_INDEX, base, limit, 0x82, 0x40);

    /* Load LDTR with selector (Index 7, GDT, RPL 0) = 0x38 */
    __asm__ volatile("lldt %0" : : "r"((uint16_t)(GDT_LDT_INDEX << 3)));
}

void ldt_init_process(process_t *proc) {
    proc->ldt = NULL;
    proc->ldt_entry_count = 0;
}

void ldt_free_process(process_t *proc) {
    if (proc->ldt) {
        kfree(proc->ldt, LDT_ENTRIES * 8);
        proc->ldt = NULL;
        proc->ldt_entry_count = 0;
    }
}

/* Helper to convert user_desc to gdt_entry_t */
static void fill_ldt_entry(gdt_entry_t *entry, struct user_desc *info) {
    uint32_t base = info->base_addr;
    uint32_t limit = info->limit;
    
    entry->base_low = base & 0xFFFF;
    entry->base_middle = (base >> 16) & 0xFF;
    entry->base_high = (base >> 24) & 0xFF;
    
    entry->limit_low = limit & 0xFFFF;
    
    /* Granularity byte: [G][D/B][L][AVL][Limit High] */
    entry->granularity = (limit >> 16) & 0x0F;
    if (info->limit_in_pages) entry->granularity |= 0x80;
    if (info->seg_32bit)      entry->granularity |= 0x40;
    
    /* Access byte: [P][DPL][S][Type] */
    /* Type for data: [1][C][E][W][A] where C=0, E=expand-down, W=writable, A=accessed */
    /* Type for code: [1][C][R][A] where C=conforming, R=readable, A=accessed */
    
    uint8_t type = 0x10; /* S=1 (Code/Data) */
    if (info->contents == 0 || info->contents == 1) {
        /* Data segment */
        type |= 0x02; /* Writable */
    } else if (info->contents == 2) {
        /* Code segment */
        type |= 0x0A; /* Executable, Readable */
    }
    
    uint8_t access = 0x80 | 0x60 | type; /* P=1, DPL=3, S=1 */
    if (info->seg_not_present) access &= ~0x80;
    
    entry->access = access;
}

int sys_modify_ldt(int func, struct user_desc *ptr, unsigned long bytecount) {
    if (func == LDT_READ) {
        /* TODO: Implement reading LDT */
        return -ENOSYS;
    }
    
    if (func != LDT_WRITE && func != LDT_READ_DEFAULT) {
        return -EINVAL;
    }
    
    if (bytecount != sizeof(struct user_desc)) {
        return -EINVAL;
    }
    
    struct user_desc info;
    if (copy_from_user(&info, ptr, sizeof(struct user_desc))) {
        return -EFAULT;
    }
    
    if (info.entry_number >= LDT_ENTRIES) {
        return -EINVAL;
    }
    
    /* Lazy allocate LDT if needed */
    if (!current_process->ldt) {
        current_process->ldt = kmalloc(LDT_ENTRIES * 8);
        if (!current_process->ldt) return -ENOMEM;
        memset(current_process->ldt, 0, LDT_ENTRIES * 8);
        current_process->ldt_entry_count = LDT_ENTRIES;
    }
    
    gdt_entry_t *ldt = (gdt_entry_t *)current_process->ldt;
    
    if (info.base_addr == 0 && info.limit == 0 && info.contents == 0 && 
        info.seg_not_present == 1 && info.seg_32bit == 0 && info.limit_in_pages == 0) {
        /* Clear entry */
        memset(&ldt[info.entry_number], 0, 8);
    } else {
        /* Set entry */
        fill_ldt_entry(&ldt[info.entry_number], &info);
    }
    
    /* If we modified the LDT, we need to reload LDTR if it's the current one */
    ldt_activate(current_process);
    
    return 0;
}
