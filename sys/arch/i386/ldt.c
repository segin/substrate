#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/ldt.h>
#include <sys/proc.h>
#include <sys/kern_syscalls.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <arch/i386/gdt.h>
#include <stdio.h>
#ifndef HOST_TEST
#include <vm/uma.h>
#endif

/* GDT index 7 is reserved for the active process LDT */
#define GDT_LDT_INDEX 7

#ifndef HOST_TEST
static uma_zone_t *ldt_full_zone;

static void ldt_zone_ensure_init(void) {
    static volatile int init_lock = 0;

    if (ldt_full_zone) {
        return;
    }

    while (__sync_lock_test_and_set(&init_lock, 1)) {
        while (__atomic_load_n(&init_lock, __ATOMIC_RELAXED)) {
            __asm__ volatile("pause");
        }
    }

    if (!ldt_full_zone) {
        ldt_full_zone = uma_zcreate("ldt-full",
                                    LDT_ENTRIES * LDT_ENTRY_SIZE,
                                    NULL,
                                    NULL,
                                    NULL,
                                    NULL,
                                    16,
                                    UMA_ZONE_ZINIT | UMA_ZONE_NOBUCKET);
    }

    __sync_lock_release(&init_lock);
}
#endif

static void *ldt_alloc_storage(unsigned int entry_count, uint8_t *is_uma_out) {
    size_t bytes;
    void *ptr;

    if (is_uma_out) {
        *is_uma_out = 0;
    }

#ifndef HOST_TEST
    if (entry_count == LDT_ENTRIES) {
        ldt_zone_ensure_init();
        if (ldt_full_zone) {
            ptr = uma_zalloc(ldt_full_zone, M_NOWAIT | M_ZERO);
            if (ptr) {
                if (is_uma_out) {
                    *is_uma_out = 1;
                }
                return ptr;
            }
        }
    }
#endif

    bytes = (size_t)entry_count * LDT_ENTRY_SIZE;
    ptr = kmalloc(bytes);
    if (!ptr) {
        return NULL;
    }
    memset(ptr, 0, bytes);
    return ptr;
}

static void ldt_free_storage(void *ldt, int entry_count, uint8_t is_uma) {
    if (!ldt) {
        return;
    }

#ifndef HOST_TEST
    if (is_uma && ldt_full_zone && entry_count == LDT_ENTRIES) {
        uma_zfree(ldt_full_zone, ldt);
        return;
    }
#else
    (void)is_uma;
#endif

    kfree(ldt, (size_t)entry_count * LDT_ENTRY_SIZE);
}

static void ldt_load_selector(uint16_t selector) {
#ifndef HOST_TEST
    __asm__ volatile("lldt %0" : : "r"(selector));
#else
    (void)selector;
#endif
}

static void ldt_activate_locked(process_t *proc) {
    if (!proc || !proc->ldt) {
        ldt_load_selector(0);
        return;
    }

    /*
     * The caller holds proc->ldt_lock, so proc->ldt and
     * proc->ldt_entry_count remain stable while the active descriptor is
     * rewritten and LDTR is reloaded.
     */
    gdt_set_gate(GDT_LDT_INDEX,
                 (uint32_t)(uintptr_t)proc->ldt,
                 (uint32_t)(proc->ldt_entry_count * LDT_ENTRY_SIZE) - 1U,
                 0x82,
                 0x40);
    ldt_load_selector((uint16_t)(GDT_LDT_INDEX << 3));
}

static void ldt_swap_process(process_t *proc,
                             void *new_ldt,
                             int new_entry_count,
                             uint8_t new_is_uma,
                             void **old_ldt_out,
                             int *old_entry_count_out,
                             uint8_t *old_is_uma_out) {
    if (!proc) {
        return;
    }

    spinlock_acquire(&proc->ldt_lock);
    if (old_ldt_out) {
        *old_ldt_out = proc->ldt;
    }
    if (old_entry_count_out) {
        *old_entry_count_out = proc->ldt_entry_count;
    }
    if (old_is_uma_out) {
        *old_is_uma_out = proc->ldt_is_uma;
    }
    proc->ldt = new_ldt;
    proc->ldt_entry_count = new_entry_count;
    proc->ldt_is_uma = new_is_uma;
    spinlock_release(&proc->ldt_lock);
}

static int ldt_install_process(process_t *proc,
                               void *new_ldt,
                               unsigned int entry_count,
                               uint8_t new_is_uma) {
    void *old_ldt = NULL;
    int old_entry_count = 0;
    uint8_t old_is_uma = 0;

    if (!proc) {
        return -EINVAL;
    }

    ldt_swap_process(proc,
                     new_ldt,
                     (int)entry_count,
                     new_is_uma,
                     &old_ldt,
                     &old_entry_count,
                     &old_is_uma);
    if (old_ldt) {
        ldt_free_storage(old_ldt, old_entry_count, old_is_uma);
    }
    return 0;
}

static int ldt_ensure_process(process_t *proc, unsigned int entry_count) {
    void *new_ldt;
    void *old_ldt;
    int old_entry_count;
    uint8_t new_is_uma;
    uint8_t old_is_uma;

    if (!proc || entry_count == 0 || entry_count > LDT_ENTRIES) {
        return -EINVAL;
    }

    for (;;) {
        spinlock_acquire(&proc->ldt_lock);
        old_ldt = proc->ldt;
        old_entry_count = proc->ldt_entry_count;
        if (old_entry_count >= (int)entry_count) {
            spinlock_release(&proc->ldt_lock);
            return 0;
        }
        spinlock_release(&proc->ldt_lock);

        new_ldt = ldt_alloc_storage(entry_count, &new_is_uma);
        if (!new_ldt) {
            return -ENOMEM;
        }

        spinlock_acquire(&proc->ldt_lock);
        if (proc->ldt != old_ldt || proc->ldt_entry_count != old_entry_count) {
            spinlock_release(&proc->ldt_lock);
            ldt_free_storage(new_ldt, (int)entry_count, new_is_uma);
            continue;
        }
        if (old_ldt && old_entry_count > 0) {
            memcpy(new_ldt, old_ldt, (size_t)old_entry_count * LDT_ENTRY_SIZE);
        }
        proc->ldt = new_ldt;
        proc->ldt_entry_count = (int)entry_count;
        old_is_uma = proc->ldt_is_uma;
        proc->ldt_is_uma = new_is_uma;
        spinlock_release(&proc->ldt_lock);

        if (old_ldt) {
            ldt_free_storage(old_ldt, old_entry_count, old_is_uma);
        }
        return 0;
    }
}

static int ldt_desc_is_clear(const struct user_desc *info) {
    return info &&
           info->base_addr == 0 &&
           info->limit == 0 &&
           info->contents == 0 &&
           info->seg_not_present == 1 &&
           info->seg_32bit == 0 &&
           info->limit_in_pages == 0;
}

static int ldt_validate_user_desc(const struct user_desc *info) {
    if (!info) {
        return -EINVAL;
    }
    if (info->entry_number >= LDT_ENTRIES) {
        return -EINVAL;
    }
    if (ldt_desc_is_clear(info)) {
        return 0;
    }
    if (info->contents > 2) {
        return -EINVAL;
    }
    return 0;
}

static void ldt_warn_suspicious(const struct user_desc *info) {
    char buf[128];

    if (!info || ldt_desc_is_clear(info)) {
        return;
    }
    if (info->entry_number < 256 &&
        ((info->limit_in_pages == 0 && (info->limit & 0x0FU) == 0) ||
         (info->limit_in_pages != 0 && (info->limit & 0x01U) == 0))) {
        return;
    }

    sprintf(buf, "LDT: suspicious entry=%u limit=0x%x pages=%u\n",
            info->entry_number, info->limit, info->limit_in_pages ? 1U : 0U);
    kprint(buf);
}

void ldt_activate(process_t *proc) {
    if (!proc) {
        ldt_load_selector(0);
        return;
    }
    spinlock_acquire(&proc->ldt_lock);
    ldt_activate_locked(proc);
    spinlock_release(&proc->ldt_lock);
}

void ldt_init_process(process_t *proc) {
    proc->ldt = NULL;
    proc->ldt_entry_count = 0;
    proc->ldt_is_uma = 0;
    spinlock_init(&proc->ldt_lock, "ldt");
}

int ldt_alloc_process(process_t *proc, unsigned int entry_count) {
    void *new_ldt;
    uint8_t new_is_uma;

    if (!proc || entry_count == 0 || entry_count > LDT_ENTRIES) {
        return -EINVAL;
    }

    new_ldt = ldt_alloc_storage(entry_count, &new_is_uma);
    if (!new_ldt) {
        return -ENOMEM;
    }

    return ldt_install_process(proc, new_ldt, entry_count, new_is_uma);
}

int ldt_clone_process(process_t *dst, const process_t *src) {
    void *new_ldt;
    const process_t *src_proc = src;
    int src_entry_count;
    uint8_t new_is_uma;
    size_t bytes;

    if (!dst || !src) {
        return -EINVAL;
    }

    for (;;) {
        spinlock_acquire((spinlock_t *)&src_proc->ldt_lock);
        if (!src_proc->ldt || src_proc->ldt_entry_count <= 0) {
            spinlock_release((spinlock_t *)&src_proc->ldt_lock);
            ldt_free_process(dst);
            return 0;
        }
        src_entry_count = src_proc->ldt_entry_count;
        spinlock_release((spinlock_t *)&src_proc->ldt_lock);

        bytes = (size_t)src_entry_count * LDT_ENTRY_SIZE;
        new_ldt = ldt_alloc_storage((unsigned int)src_entry_count, &new_is_uma);
        if (!new_ldt) {
            return -ENOMEM;
        }

        spinlock_acquire((spinlock_t *)&src_proc->ldt_lock);
        if (!src_proc->ldt || src_proc->ldt_entry_count != src_entry_count) {
            spinlock_release((spinlock_t *)&src_proc->ldt_lock);
            kfree(new_ldt, bytes);
            continue;
        }
        memcpy(new_ldt, src_proc->ldt, bytes);
        spinlock_release((spinlock_t *)&src_proc->ldt_lock);

        return ldt_install_process(dst, new_ldt, (unsigned int)src_entry_count, new_is_uma);
    }
}

void ldt_free_process(process_t *proc) {
    void *old_ldt = NULL;
    int old_entry_count = 0;
    uint8_t old_is_uma = 0;

    if (!proc) {
        return;
    }

    ldt_swap_process(proc, NULL, 0, 0, &old_ldt, &old_entry_count, &old_is_uma);
    if (old_ldt) {
        ldt_free_storage(old_ldt, old_entry_count, old_is_uma);
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
        void *tmp = NULL;
        void *ldt_ptr;
        unsigned int actual_size;
        unsigned int copy_size;

        for (;;) {
            spinlock_acquire(&current_process->ldt_lock);
            ldt_ptr = current_process->ldt;
            actual_size = (unsigned int)current_process->ldt_entry_count * LDT_ENTRY_SIZE;
            copy_size = (bytecount < actual_size) ? (unsigned int)bytecount : actual_size;
            spinlock_release(&current_process->ldt_lock);

            if (copy_size == 0 || !ldt_ptr) {
                return 0;
            }

            tmp = kmalloc(copy_size);
            if (!tmp) {
                return -ENOMEM;
            }

            spinlock_acquire(&current_process->ldt_lock);
            if (current_process->ldt == ldt_ptr &&
                (unsigned int)current_process->ldt_entry_count * LDT_ENTRY_SIZE == actual_size) {
                memcpy(tmp, ldt_ptr, copy_size);
                spinlock_release(&current_process->ldt_lock);
                break;
            }
            spinlock_release(&current_process->ldt_lock);

            kfree(tmp, copy_size);
            tmp = NULL;
        }

        if (copy_size > 0 && tmp) {
            if (copyout(tmp, ptr, copy_size)) {
                kfree(tmp, copy_size);
                return -EFAULT;
            }
            kfree(tmp, copy_size);
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
    
    if (ldt_validate_user_desc(&info) != 0) return -EINVAL;
    ldt_warn_suspicious(&info);
    
    /* Lazy allocate LDT if needed */
    for (;;) {
        if (ldt_ensure_process(current_process, LDT_ENTRIES) != 0) {
            return -ENOMEM;
        }

        spinlock_acquire(&current_process->ldt_lock);
        gdt_entry_t *ldt = (gdt_entry_t *)current_process->ldt;
        if (ldt && info.entry_number < (unsigned int)current_process->ldt_entry_count) {
            if (ldt_desc_is_clear(&info)) {
                /* Clear entry */
                memset(&ldt[info.entry_number], 0, 8);
            } else {
                /* Set entry */
                fill_ldt_entry(&ldt[info.entry_number], &info);
            }
            
            /* If we modified the LDT, we need to reload LDTR if it's the current one */
            ldt_activate_locked(current_process);
            spinlock_release(&current_process->ldt_lock);
            return 0;
        }
        spinlock_release(&current_process->ldt_lock);
    }
}
