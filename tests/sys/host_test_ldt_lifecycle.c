#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/ldt.h>

process_t *current_process;
thread_t *current_thread;

static size_t last_kfree_size;
static char last_log[128];
static int fail_next_kmalloc;

uint32_t lapic_get_id(void) {
    return 0;
}

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFU;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    while (__atomic_exchange_n(&lock->locked, 1U, __ATOMIC_ACQUIRE)) {
    }
    lock->cpu_id = lapic_get_id();
}

bool spinlock_try_acquire(spinlock_t *lock) {
    if (__atomic_exchange_n(&lock->locked, 1U, __ATOMIC_ACQUIRE) == 0U) {
        lock->cpu_id = lapic_get_id();
        return true;
    }
    return false;
}

void spinlock_release(spinlock_t *lock) {
    lock->cpu_id = 0xFFFFFFFFU;
    __atomic_store_n(&lock->locked, 0U, __ATOMIC_RELEASE);
}

bool spinlock_is_held(spinlock_t *lock) {
    return lock->locked && lock->cpu_id == lapic_get_id();
}

void *kmalloc(size_t size) {
    if (fail_next_kmalloc) {
        fail_next_kmalloc = 0;
        return NULL;
    }
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    last_kfree_size = size;
    free(ptr);
}

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    (void)num;
    (void)base;
    (void)limit;
    (void)access;
    (void)gran;
}

void kprint(const char *msg) {
    strncpy(last_log, msg ? msg : "", sizeof(last_log) - 1);
    last_log[sizeof(last_log) - 1] = '\0';
}

#include "../../sys/arch/i386/ldt.c"

static void fill_entry(gdt_entry_t *entry, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    memset(entry, 0, sizeof(*entry));
    entry->base_low = (uint16_t)(base & 0xFFFFU);
    entry->base_middle = (uint8_t)((base >> 16) & 0xFFU);
    entry->base_high = (uint8_t)((base >> 24) & 0xFFU);
    entry->limit_low = (uint16_t)(limit & 0xFFFFU);
    entry->granularity = (uint8_t)(((limit >> 16) & 0x0FU) | (gran & 0xF0U));
    entry->access = access;
}

int main(void) {
    process_t parent;
    process_t child;
    process_t replaced;
    gdt_entry_t *parent_ldt;
    gdt_entry_t *child_ldt;
    size_t bytes = 4U * sizeof(gdt_entry_t);
    size_t old_bytes = 2U * sizeof(gdt_entry_t);

    memset(&parent, 0, sizeof(parent));
    memset(&child, 0, sizeof(child));
    memset(&replaced, 0, sizeof(replaced));
    memset(last_log, 0, sizeof(last_log));
    fail_next_kmalloc = 0;
    ldt_init_process(&parent);
    ldt_init_process(&child);
    ldt_init_process(&replaced);

    parent.ldt_entry_count = 4;
    parent.ldt = kmalloc(bytes);
    if (!parent.ldt) {
        fprintf(stderr, "FAIL: parent LDT allocation failed\n");
        return 1;
    }

    parent_ldt = (gdt_entry_t *)parent.ldt;
    fill_entry(&parent_ldt[0], 0x00010000U, 0x00FFFU, 0xFAU, 0x40U);
    fill_entry(&parent_ldt[1], 0x00020000U, 0x01FFFU, 0xF2U, 0x40U);
    fill_entry(&parent_ldt[2], 0x00030000U, 0x02FFFU, 0xF2U, 0x40U);
    fill_entry(&parent_ldt[3], 0x00040000U, 0x03FFFU, 0xF2U, 0x40U);

    if (ldt_clone_process(&child, &parent) != 0) {
        fprintf(stderr, "FAIL: ldt_clone_process rejected valid source\n");
        return 1;
    }
    if (!child.ldt || child.ldt == parent.ldt) {
        fprintf(stderr, "FAIL: child LDT missing or aliased to parent\n");
        return 1;
    }
    if (child.ldt_entry_count != parent.ldt_entry_count) {
        fprintf(stderr, "FAIL: child LDT entry count mismatch\n");
        return 1;
    }
    if (memcmp(child.ldt, parent.ldt, bytes) != 0) {
        fprintf(stderr, "FAIL: child LDT contents differ from parent\n");
        return 1;
    }

    child_ldt = (gdt_entry_t *)child.ldt;
    child_ldt[1].base_low ^= 0x00FFU;
    if (memcmp(child.ldt, parent.ldt, bytes) == 0) {
        fprintf(stderr, "FAIL: child LDT writes leaked back to parent\n");
        return 1;
    }

    ldt_free_process(&child);
    if (child.ldt || child.ldt_entry_count != 0) {
        fprintf(stderr, "FAIL: child LDT not cleared on free\n");
        return 1;
    }
    if (last_kfree_size != bytes) {
        fprintf(stderr, "FAIL: child LDT freed with wrong size %lu\n",
                (unsigned long)last_kfree_size);
        return 1;
    }

    if (ldt_clone_process(&child, &parent) != 0) {
        fprintf(stderr, "FAIL: second ldt_clone_process failed\n");
        return 1;
    }

    if (ldt_alloc_process(&replaced, 2) != 0) {
        fprintf(stderr, "FAIL: initial ldt_alloc_process failed\n");
        return 1;
    }
    ((gdt_entry_t *)replaced.ldt)[0].base_low = 0x1234U;
    last_kfree_size = 0;
    if (ldt_alloc_process(&replaced, 6) != 0) {
        fprintf(stderr, "FAIL: replacement ldt_alloc_process failed\n");
        return 1;
    }
    if (replaced.ldt_entry_count != 6) {
        fprintf(stderr, "FAIL: replacement LDT entry count mismatch\n");
        return 1;
    }
    if (last_kfree_size != old_bytes) {
        fprintf(stderr, "FAIL: replacement LDT freed wrong size %lu\n",
                (unsigned long)last_kfree_size);
        return 1;
    }
    if (((gdt_entry_t *)replaced.ldt)[0].base_low != 0) {
        fprintf(stderr, "FAIL: replacement LDT was not zeroed\n");
        return 1;
    }

    {
        gdt_entry_t seeded[3];

        memset(seeded, 0, sizeof(seeded));
        fill_entry(&seeded[0], 0x00011000U, 0x00FFFU, 0xFAU, 0x40U);
        fill_entry(&seeded[1], 0x00022000U, 0x01FFFU, 0xF2U, 0x40U);
        fill_entry(&seeded[2], 0x00033000U, 0x02FFFU, 0xF2U, 0x40U);
        last_kfree_size = 0;
        if (ldt_replace_process(&replaced, seeded, 3) != 0) {
            fprintf(stderr, "FAIL: ldt_replace_process failed\n");
            return 1;
        }
        if (replaced.ldt_entry_count != 3) {
            fprintf(stderr, "FAIL: ldt_replace_process entry count mismatch\n");
            return 1;
        }
        if (memcmp(replaced.ldt, seeded, sizeof(seeded)) != 0) {
            fprintf(stderr, "FAIL: ldt_replace_process contents mismatch\n");
            return 1;
        }
        if (last_kfree_size != 6U * sizeof(gdt_entry_t)) {
            fprintf(stderr, "FAIL: ldt_replace_process freed wrong size %lu\n",
                    (unsigned long)last_kfree_size);
            return 1;
        }
    }

    current_process = &replaced;
    {
        struct user_desc bad;
        struct user_desc suspicious;
        struct ldt_diag_snapshot before_diag;
        struct ldt_diag_snapshot after_diag;

        memset(&bad, 0, sizeof(bad));
        bad.entry_number = 1;
        bad.base_addr = 0x12340000U;
        bad.limit = 0x123U;
        bad.seg_32bit = 1;
        bad.contents = 3;
        ldt_get_diag_snapshot(&before_diag);
        if (sys_modify_ldt(LDT_WRITE, &bad, sizeof(bad)) != -EINVAL) {
            fprintf(stderr, "FAIL: invalid LDT descriptor contents accepted\n");
            return 1;
        }
        ldt_get_diag_snapshot(&after_diag);
        if (after_diag.validation_failures != before_diag.validation_failures + 1U) {
            fprintf(stderr, "FAIL: validation failure counter did not increment\n");
            return 1;
        }

        memset(&suspicious, 0, sizeof(suspicious));
        suspicious.entry_number = 512;
        suspicious.base_addr = 0x56780000U;
        suspicious.limit = 0x123U;
        suspicious.seg_32bit = 1;
        suspicious.contents = 0;
        suspicious.useable = 1;
        memset(last_log, 0, sizeof(last_log));
        if (sys_modify_ldt(LDT_WRITE, &suspicious, sizeof(suspicious)) != 0) {
            fprintf(stderr, "FAIL: suspicious but valid LDT descriptor rejected\n");
            return 1;
        }
        if (strstr(last_log, "suspicious") == NULL) {
            fprintf(stderr, "FAIL: suspicious LDT descriptor did not log warning\n");
            return 1;
        }

        ldt_get_diag_snapshot(&before_diag);
        ldt_free_process(&replaced);
        fail_next_kmalloc = 1;
        if (ldt_alloc_process(&replaced, 4) != -ENOMEM) {
            fprintf(stderr, "FAIL: ldt_alloc_process did not surface ENOMEM\n");
            return 1;
        }
        ldt_get_diag_snapshot(&after_diag);
        if (after_diag.allocation_failures != before_diag.allocation_failures + 1U) {
            fprintf(stderr, "FAIL: allocation failure counter did not increment\n");
            return 1;
        }
        if (ldt_alloc_process(&replaced, 4) != 0) {
            fprintf(stderr, "FAIL: ldt_alloc_process did not recover after ENOMEM\n");
            return 1;
        }
    }

    ldt_free_process(&parent);
    ldt_free_process(&child);
    ldt_free_process(&replaced);

    puts("host_test_ldt_lifecycle: ok");
    return 0;
}
