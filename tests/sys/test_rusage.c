/*
 * test_rusage.c - Unit tests for resource usage tracking
 *
 * Tests the rusage API for proper time accumulation, fault counting,
 * context switch tracking, and finalization during process exit.
 */

#include <sys/proc.h>
#include <sys/resource.h>
#include <string.h>
#include "tests.h"

extern void kprint(const char *msg);
extern void panic(const char *msg);

/* Declare rusage functions */
extern void rusage_init(process_t *p);
extern void rusage_add_tick(process_t *p, int is_usermode);
extern void rusage_add_fault(process_t *p, int major);
extern void rusage_add_ctx_switch(process_t *p, int voluntary);
extern void rusage_add_signal(process_t *p);
extern void rusage_update_maxrss(process_t *p, long rss_pages);
extern void rusage_add_io(process_t *p, int is_read);
extern void rusage_finalize(process_t *p);
extern void rusage_copy_to_child(process_t *child);
extern void timeval_add(struct timeval *result, const struct timeval *a,
                        const struct timeval *b);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        kprint("FAILED: " #cond "\n"); \
        panic("Assertion failed"); \
    } else { \
        kprint("PASS: " #cond "\n"); \
    } \
} while(0)

/* Test process structures */
static process_t test_proc;
static process_t child_proc;

void test_rusage_init(void) {
    /* Test rusage_init zeroes all fields */
    memset(&test_proc, 0xFF, sizeof(test_proc));
    rusage_init(&test_proc);

    TEST_ASSERT(test_proc.rusage.ru_utime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_usec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_usec == 0);
    TEST_ASSERT(test_proc.rusage.ru_maxrss == 0);
    TEST_ASSERT(test_proc.rusage.ru_minflt == 0);
    TEST_ASSERT(test_proc.rusage.ru_majflt == 0);
    TEST_ASSERT(test_proc.rusage.ru_nvcsw == 0);
    TEST_ASSERT(test_proc.rusage.ru_nivcsw == 0);

    /* Children rusage also zeroed */
    TEST_ASSERT(test_proc.rusage_children.ru_utime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage_children.ru_stime.tv_sec == 0);
}

void test_rusage_add_tick_usermode(void) {
    /* Test user time accumulation */
    rusage_init(&test_proc);

    /* At 100 Hz, each tick is 10000 usec */
    for (int i = 0; i < 100; i++) {
        rusage_add_tick(&test_proc, 1); /* user mode */
    }

    /* 100 ticks at 10000 usec each = 1000000 usec = 1 sec */
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_sec == 1);
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_usec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_usec == 0);
}

void test_rusage_add_tick_kernelmode(void) {
    /* Test system time accumulation */
    rusage_init(&test_proc);

    /* 50 ticks of kernel time */
    for (int i = 0; i < 50; i++) {
        rusage_add_tick(&test_proc, 0); /* kernel mode */
    }

    /* 50 ticks at 10000 usec each = 500000 usec = 0.5 sec */
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_usec == 500000);
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_usec == 0);
}

void test_rusage_add_tick_mixed(void) {
    /* Test mixed user/kernel time */
    rusage_init(&test_proc);

    /* 75 user ticks, 25 kernel ticks */
    for (int i = 0; i < 75; i++) {
        rusage_add_tick(&test_proc, 1);
    }
    for (int i = 0; i < 25; i++) {
        rusage_add_tick(&test_proc, 0);
    }

    /* User: 750000 usec = 0.75 sec */
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_usec == 750000);

    /* System: 250000 usec = 0.25 sec */
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_usec == 250000);
}

void test_rusage_add_fault(void) {
    rusage_init(&test_proc);

    /* Add minor faults */
    rusage_add_fault(&test_proc, 0);
    rusage_add_fault(&test_proc, 0);
    rusage_add_fault(&test_proc, 0);

    /* Add major faults */
    rusage_add_fault(&test_proc, 1);
    rusage_add_fault(&test_proc, 1);

    TEST_ASSERT(test_proc.rusage.ru_minflt == 3);
    TEST_ASSERT(test_proc.rusage.ru_majflt == 2);
}

void test_rusage_add_ctx_switch(void) {
    rusage_init(&test_proc);

    /* Voluntary context switches (sleep, wait) */
    rusage_add_ctx_switch(&test_proc, 1);
    rusage_add_ctx_switch(&test_proc, 1);

    /* Involuntary context switches (preemption) */
    rusage_add_ctx_switch(&test_proc, 0);
    rusage_add_ctx_switch(&test_proc, 0);
    rusage_add_ctx_switch(&test_proc, 0);

    TEST_ASSERT(test_proc.rusage.ru_nvcsw == 2);
    TEST_ASSERT(test_proc.rusage.ru_nivcsw == 3);
}

void test_rusage_add_signal(void) {
    rusage_init(&test_proc);

    rusage_add_signal(&test_proc);
    rusage_add_signal(&test_proc);
    rusage_add_signal(&test_proc);

    TEST_ASSERT(test_proc.rusage.ru_nsignals == 3);
}

void test_rusage_update_maxrss(void) {
    rusage_init(&test_proc);

    /* Update maxrss multiple times */
    rusage_update_maxrss(&test_proc, 10);  /* 40 KB */
    TEST_ASSERT(test_proc.rusage.ru_maxrss == 40);

    rusage_update_maxrss(&test_proc, 20);  /* 80 KB - higher */
    TEST_ASSERT(test_proc.rusage.ru_maxrss == 80);

    rusage_update_maxrss(&test_proc, 15);  /* 60 KB - lower, no change */
    TEST_ASSERT(test_proc.rusage.ru_maxrss == 80);
}

void test_rusage_add_io(void) {
    rusage_init(&test_proc);

    /* Read operations */
    rusage_add_io(&test_proc, 1);
    rusage_add_io(&test_proc, 1);

    /* Write operations */
    rusage_add_io(&test_proc, 0);
    rusage_add_io(&test_proc, 0);
    rusage_add_io(&test_proc, 0);

    TEST_ASSERT(test_proc.rusage.ru_inblock == 2);
    TEST_ASSERT(test_proc.rusage.ru_oublock == 3);
}

void test_rusage_finalize(void) {
    /* Set up parent with some usage */
    rusage_init(&test_proc);
    rusage_add_tick(&test_proc, 1);  /* 10000 usec user */
    rusage_add_tick(&test_proc, 0);  /* 10000 usec system */
    rusage_add_fault(&test_proc, 0); /* 1 minor fault */

    /* Set up children's accumulated usage */
    test_proc.rusage_children.ru_utime.tv_sec = 1;
    test_proc.rusage_children.ru_utime.tv_usec = 500000;
    test_proc.rusage_children.ru_stime.tv_sec = 0;
    test_proc.rusage_children.ru_stime.tv_usec = 250000;
    test_proc.rusage_children.ru_minflt = 5;
    test_proc.rusage_children.ru_maxrss = 100;

    /* Finalize */
    rusage_finalize(&test_proc);

    /* User time: 10000 usec + 1.5 sec = 1.510000 sec */
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_sec == 1);
    TEST_ASSERT(test_proc.rusage.ru_utime.tv_usec == 510000);

    /* System time: 10000 usec + 0.25 sec = 0.260000 sec */
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_sec == 0);
    TEST_ASSERT(test_proc.rusage.ru_stime.tv_usec == 260000);

    /* Faults accumulated */
    TEST_ASSERT(test_proc.rusage.ru_minflt == 6);

    /* Max RSS from children */
    TEST_ASSERT(test_proc.rusage.ru_maxrss == 100);
}

void test_rusage_copy_to_child(void) {
    /* Set up parent with usage */
    rusage_init(&test_proc);
    rusage_add_tick(&test_proc, 1);
    rusage_add_fault(&test_proc, 0);

    /* Copy to child - should be zeroed */
    rusage_copy_to_child(&child_proc);

    TEST_ASSERT(child_proc.rusage.ru_utime.tv_sec == 0);
    TEST_ASSERT(child_proc.rusage.ru_utime.tv_usec == 0);
    TEST_ASSERT(child_proc.rusage.ru_minflt == 0);
    TEST_ASSERT(child_proc.rusage_children.ru_utime.tv_sec == 0);
}

void test_timeval_add(void) {
    struct timeval a = {1, 500000};  /* 1.5 sec */
    struct timeval b = {2, 700000};  /* 2.7 sec */
    struct timeval result;

    timeval_add(&result, &a, &b);

    /* 1.5 + 2.7 = 4.2 sec */
    TEST_ASSERT(result.tv_sec == 4);
    TEST_ASSERT(result.tv_usec == 200000);
}

void test_rusage_null_safety(void) {
    /* All functions should handle NULL gracefully */
    rusage_init(NULL);
    rusage_add_tick(NULL, 1);
    rusage_add_fault(NULL, 1);
    rusage_add_ctx_switch(NULL, 1);
    rusage_add_signal(NULL);
    rusage_update_maxrss(NULL, 10);
    rusage_add_io(NULL, 1);
    rusage_finalize(NULL);
    rusage_copy_to_child(NULL);

    kprint("PASS: NULL safety checks\n");
}

void test_rusage(void) {
    kprint("\n=== rusage tests ===\n");

    test_rusage_init();
    test_rusage_add_tick_usermode();
    test_rusage_add_tick_kernelmode();
    test_rusage_add_tick_mixed();
    test_rusage_add_fault();
    test_rusage_add_ctx_switch();
    test_rusage_add_signal();
    test_rusage_update_maxrss();
    test_rusage_add_io();
    test_rusage_finalize();
    test_rusage_copy_to_child();
    test_timeval_add();
    test_rusage_null_safety();

    kprint("=== rusage tests PASSED ===\n\n");
}
