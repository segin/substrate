#define _GNU_SOURCE
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sys/include/sys/lock.h"

typedef struct process {
    void *ldt;
    int ldt_entry_count;
    uint8_t ldt_is_uma;
    spinlock_t ldt_lock;
} process_t;

typedef struct thread {
    int unused;
} thread_t;

#define _SYS_PROC_H

process_t *current_process;
thread_t *current_thread;

static pthread_mutex_t thread_id_lock = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local uint32_t thread_id;
static uint32_t next_thread_id = 1;
static volatile int start_flag;
static volatile int test_failed;
static char failure_msg[128];

static process_t shared_proc;
static process_t clone_proc;

static uint32_t host_thread_id(void) {
    if (thread_id == 0) {
        pthread_mutex_lock(&thread_id_lock);
        thread_id = next_thread_id++;
        pthread_mutex_unlock(&thread_id_lock);
    }
    return thread_id;
}

uint32_t lapic_get_id(void) {
    return host_thread_id();
}

void spinlock_init(spinlock_t *lock, const char *name) {
    lock->locked = 0;
    lock->cpu_id = 0xFFFFFFFFU;
    lock->name = name;
}

void spinlock_acquire(spinlock_t *lock) {
    while (__atomic_exchange_n(&lock->locked, 1U, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&lock->locked, __ATOMIC_RELAXED)) {
            __asm__ volatile("" ::: "memory");
        }
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

static void fail(const char *msg) {
    if (__sync_bool_compare_and_swap(&test_failed, 0, 1)) {
        strncpy(failure_msg, msg, sizeof(failure_msg) - 1);
        failure_msg[sizeof(failure_msg) - 1] = '\0';
    }
}

static void *writer_thread(void *arg) {
    int i;
    struct user_desc info;

    (void)arg;
    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) {
    }

    for (i = 0; i < 4000 && !test_failed; i++) {
        memset(&info, 0, sizeof(info));
        info.entry_number = (unsigned int)(i % 64);
        info.base_addr = 0x10000U + (uint32_t)(i * 0x100U);
        info.limit = 0xFFFFU;
        info.seg_32bit = 1;
        info.contents = (i % 3 == 2) ? 2U : 0U;
        info.useable = 1;

        if (sys_modify_ldt(LDT_WRITE, &info, sizeof(info)) != 0) {
            fail("sys_modify_ldt write failed");
            break;
        }

        if ((i % 17) == 0) {
            struct user_desc clear = info;
            clear.seg_not_present = 1;
            clear.base_addr = 0;
            clear.limit = 0;
            clear.contents = 0;
            clear.seg_32bit = 0;
            clear.limit_in_pages = 0;
            if (sys_modify_ldt(LDT_WRITE, &clear, sizeof(clear)) != 0) {
                fail("sys_modify_ldt clear failed");
                break;
            }
        }
    }

    return NULL;
}

static void *reader_thread(void *arg) {
    int i;
    unsigned char buf[256];

    (void)arg;
    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) {
    }

    for (i = 0; i < 4000 && !test_failed; i++) {
        int ret = sys_modify_ldt(LDT_READ, buf, sizeof(buf));
        if (ret < 0 || ret > (int)sizeof(buf) || (ret % LDT_ENTRY_SIZE) != 0) {
            fail("sys_modify_ldt read returned invalid size");
            break;
        }
    }

    return NULL;
}

static void *allocator_thread(void *arg) {
    int i;

    (void)arg;
    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) {
    }

    for (i = 0; i < 2000 && !test_failed; i++) {
        unsigned int entries = (unsigned int)(((i % 8) + 1) * 8);
        if (ldt_alloc_process(&shared_proc, entries) != 0) {
            fail("ldt_alloc_process failed");
            break;
        }
        ldt_activate(&shared_proc);
        if ((i % 9) == 0) {
            ldt_free_process(&shared_proc);
        }
    }

    return NULL;
}

static void *clone_thread(void *arg) {
    int i;

    (void)arg;
    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) {
    }

    for (i = 0; i < 2000 && !test_failed; i++) {
        int ret = ldt_clone_process(&clone_proc, &shared_proc);
        if (ret != 0) {
            fail("ldt_clone_process failed");
            break;
        }
        if ((clone_proc.ldt == NULL) != (clone_proc.ldt_entry_count == 0)) {
            fail("clone process LDT state became inconsistent");
            break;
        }
        if ((i % 7) == 0) {
            ldt_free_process(&clone_proc);
        }
    }

    return NULL;
}

int main(void) {
    pthread_t threads[4];
    int i;

    memset(&shared_proc, 0, sizeof(shared_proc));
    memset(&clone_proc, 0, sizeof(clone_proc));
    ldt_init_process(&shared_proc);
    ldt_init_process(&clone_proc);
    current_process = &shared_proc;

    if (pthread_create(&threads[0], NULL, writer_thread, NULL) != 0 ||
        pthread_create(&threads[1], NULL, reader_thread, NULL) != 0 ||
        pthread_create(&threads[2], NULL, allocator_thread, NULL) != 0 ||
        pthread_create(&threads[3], NULL, clone_thread, NULL) != 0) {
        fprintf(stderr, "FAIL: could not create host threads\n");
        return 1;
    }

    __atomic_store_n(&start_flag, 1, __ATOMIC_RELEASE);

    for (i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    if (test_failed) {
        fprintf(stderr, "FAIL: %s\n", failure_msg);
        return 1;
    }
    if ((shared_proc.ldt == NULL) != (shared_proc.ldt_entry_count == 0)) {
        fprintf(stderr, "FAIL: shared process LDT state inconsistent after race\n");
        return 1;
    }
    if (shared_proc.ldt_entry_count < 0 || shared_proc.ldt_entry_count > LDT_ENTRIES) {
        fprintf(stderr, "FAIL: shared process LDT entry count invalid: %d\n",
                shared_proc.ldt_entry_count);
        return 1;
    }
    if ((clone_proc.ldt == NULL) != (clone_proc.ldt_entry_count == 0)) {
        fprintf(stderr, "FAIL: clone process LDT state inconsistent after race\n");
        return 1;
    }
    if (clone_proc.ldt_entry_count < 0 || clone_proc.ldt_entry_count > LDT_ENTRIES) {
        fprintf(stderr, "FAIL: clone process LDT entry count invalid: %d\n",
                clone_proc.ldt_entry_count);
        return 1;
    }

    ldt_free_process(&shared_proc);
    ldt_free_process(&clone_proc);

    puts("host_test_ldt_race: ok");
    return 0;
}
