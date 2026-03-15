#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/ldt.h>

process_t *current_process;
thread_t *current_thread;

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
    return calloc(1, size);
}

void kfree(void *ptr, size_t size) {
    (void)size;
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
    (void)msg;
}

#include "../../sys/arch/i386/ldt.c"

static void check_roundtrip(uint32_t base, uint32_t limit,
                            unsigned int seg32, unsigned int contents,
                            unsigned int pages) {
    struct user_desc desc;
    gdt_entry_t entry;
    uint32_t expected_limit;

    memset(&desc, 0, sizeof(desc));
    memset(&entry, 0, sizeof(entry));
    desc.entry_number = 7;
    desc.base_addr = base;
    desc.limit = limit;
    desc.seg_32bit = seg32;
    desc.contents = contents;
    desc.limit_in_pages = pages;
    desc.useable = 1;

    fill_ldt_entry(&entry, &desc);

    if (ldt_entry_base(&entry) != base) {
        fprintf(stderr, "FAIL: base roundtrip mismatch %#x != %#x\n",
                ldt_entry_base(&entry), base);
        exit(1);
    }

    expected_limit = pages ? ((limit << 12) | 0xFFFU) : limit;
    if (ldt_entry_limit(&entry) != expected_limit) {
        fprintf(stderr, "FAIL: limit roundtrip mismatch %#x != %#x\n",
                ldt_entry_limit(&entry), expected_limit);
        exit(1);
    }
}

int main(void) {
    check_roundtrip(0x00000000U, 0x0000FFFFU, 0U, 0U, 0U);
    check_roundtrip(0x12345000U, 0x000ABCDEU, 1U, 0U, 0U);
    check_roundtrip(0x00ABC000U, 0x0000007FU, 1U, 2U, 1U);
    check_roundtrip(0xFFF00000U, 0x0000000FU, 0U, 2U, 1U);
    puts("host_test_ldt_codec: ok");
    return 0;
}
