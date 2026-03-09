#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <sys/kern_syscalls.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <arch/i386/gdt.h>

/* GDT index 7 is reserved for the active process LDT */
#define GDT_LDT_INDEX 7

void ldt_activate(process_t *proc) {
    if (!proc || !proc->ldt) {
        /* Deactivate LDT by loading a null selector */
#ifndef HOST_TEST
        __asm__ volatile("lldt %0" : : "r"((uint16_t)0));
#endif
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
#ifndef HOST_TEST
    __asm__ volatile("lldt %0" : : "r"((uint16_t)(GDT_LDT_INDEX << 3)));
#endif
}

void ldt_init_process(process_t *proc) {
    proc->ldt = NULL;
    proc->ldt_entry_count = 0;
}

int ldt_alloc_process(process_t *proc, unsigned int entry_count) {
    size_t bytes;

    if (!proc || entry_count == 0 || entry_count > LDT_ENTRIES) {
        return -EINVAL;
    }

    ldt_free_process(proc);
    bytes = (size_t)entry_count * LDT_ENTRY_SIZE;
    proc->ldt = kmalloc(bytes);
    if (!proc->ldt) {
        proc->ldt_entry_count = 0;
        return -ENOMEM;
    }

    memset(proc->ldt, 0, bytes);
    proc->ldt_entry_count = (int)entry_count;
    return 0;
}

int ldt_clone_process(process_t *dst, const process_t *src) {
    size_t bytes;
    int ret;

    if (!dst || !src) {
        return -EINVAL;
    }

    ldt_free_process(dst);
    if (!src->ldt || src->ldt_entry_count <= 0) {
        return 0;
    }

    bytes = (size_t)src->ldt_entry_count * LDT_ENTRY_SIZE;
    ret = ldt_alloc_process(dst, (unsigned int)src->ldt_entry_count);
    if (ret != 0) {
        return ret;
    }

    memcpy(dst->ldt, src->ldt, bytes);
    return 0;
}

void ldt_free_process(process_t *proc) {
    if (proc->ldt) {
        kfree(proc->ldt, (size_t)proc->ldt_entry_count * LDT_ENTRY_SIZE);
        proc->ldt = NULL;
        proc->ldt_entry_count = 0;
    }
}

/* Helper to convert user_desc to gdt_entry_t */
void fill_ldt_entry(void *entry_ptr, struct user_desc *info) {
    gdt_entry_t *entry = (gdt_entry_t *)entry_ptr;
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

int sys_modify_ldt(int func, void *ptr, unsigned long bytecount) {
    if (func == LDT_READ) {
        unsigned int actual_size = current_process->ldt_entry_count * LDT_ENTRY_SIZE;
        unsigned int copy_size = (bytecount < actual_size) ? bytecount : actual_size;

        if (copy_size > 0 && current_process->ldt) {
            if (copyout(current_process->ldt, ptr, copy_size)) {
                return -EFAULT;
            }
        }
        return copy_size;
    }
    
    if (func == LDT_READ_DEFAULT) {
        return 0;
    }

    if (func != LDT_WRITE) {
        return -EINVAL;
    }
    
    if (bytecount != sizeof(struct user_desc)) {
        return -EINVAL;
    }
    
    struct user_desc info;
    if (copyin(ptr, &info, sizeof(struct user_desc))) {
        return -EFAULT;
    }
    
    if (info.entry_number >= LDT_ENTRIES) {
        return -EINVAL;
    }
    
    /* Lazy allocate LDT if needed */
    if (!current_process->ldt) {
        if (ldt_alloc_process(current_process, LDT_ENTRIES) != 0) return -ENOMEM;
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
