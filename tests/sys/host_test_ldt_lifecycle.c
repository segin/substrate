#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/ldt.h>

process_t *current_process;
thread_t *current_thread;

static size_t last_kfree_size;

void *kmalloc(size_t size) {
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

    ldt_free_process(&parent);
    ldt_free_process(&child);
    ldt_free_process(&replaced);

    puts("host_test_ldt_lifecycle: ok");
    return 0;
}
