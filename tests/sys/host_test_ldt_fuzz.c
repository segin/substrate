#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/proc.h>
#include <sys/ldt.h>

process_t *current_process;
thread_t *current_thread;

static process_t proc;

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
    if (!src) {
        return -1;
    }
    memcpy(dst, src, size);
    return 0;
}

int copyout(const void *src, void *dst, size_t size) {
    if (!dst) {
        return -1;
    }
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

static uint32_t rng_state = 0x13579BDFU;

static uint32_t next_u32(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

int main(void) {
    unsigned char buf[128];
    int i;

    memset(&proc, 0, sizeof(proc));
    ldt_init_process(&proc);
    current_process = &proc;

    for (i = 0; i < 10000; i++) {
        int func = (int)(next_u32() % 5U);
        unsigned long size = next_u32() % (sizeof(buf) + 8U);
        struct user_desc desc;
        int ret;

        memset(&desc, 0, sizeof(desc));
        desc.entry_number = next_u32() % (LDT_ENTRIES + 8U);
        desc.base_addr = next_u32();
        desc.limit = next_u32() & 0x000FFFFFU;
        desc.seg_32bit = next_u32() & 1U;
        desc.contents = next_u32() & 3U;
        desc.read_exec_only = next_u32() & 1U;
        desc.limit_in_pages = next_u32() & 1U;
        desc.seg_not_present = next_u32() & 1U;
        desc.useable = next_u32() & 1U;

        switch (func) {
        case LDT_READ:
            ret = sys_modify_ldt(LDT_READ,
                                 (next_u32() & 1U) ? buf : NULL,
                                 size);
            if (!(ret >= 0 || ret == -EFAULT || ret == -ENOMEM)) {
                fprintf(stderr, "FAIL: unexpected LDT_READ result %d\n", ret);
                return 1;
            }
            break;
        case LDT_WRITE:
            ret = sys_modify_ldt(LDT_WRITE,
                                 (next_u32() & 1U) ? &desc : NULL,
                                 size);
            if (!(ret == 0 || ret == -EINVAL || ret == -EFAULT || ret == -ENOMEM)) {
                fprintf(stderr, "FAIL: unexpected LDT_WRITE result %d\n", ret);
                return 1;
            }
            break;
        case LDT_READ_DEFAULT:
            ret = sys_modify_ldt(LDT_READ_DEFAULT, buf, size);
            if (ret != 0) {
                fprintf(stderr, "FAIL: unexpected LDT_READ_DEFAULT result %d\n", ret);
                return 1;
            }
            break;
        default:
            ret = sys_modify_ldt(func, buf, size);
            if (ret != -EINVAL) {
                fprintf(stderr, "FAIL: unexpected invalid-func result %d\n", ret);
                return 1;
            }
            break;
        }

        if ((proc.ldt == NULL) != (proc.ldt_entry_count == 0)) {
            fprintf(stderr, "FAIL: inconsistent process LDT state after fuzz step\n");
            return 1;
        }
        if (proc.ldt_entry_count < 0 || proc.ldt_entry_count > LDT_ENTRIES) {
            fprintf(stderr, "FAIL: invalid LDT entry count after fuzz step: %d\n",
                    proc.ldt_entry_count);
            return 1;
        }
    }

    ldt_free_process(&proc);
    puts("host_test_ldt_fuzz: ok");
    return 0;
}
