#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sys/types.h>



/* In a host test we need to stop it from including the system headers that conflict */
#define _SUBSTRATE_SYS_TYPES_H
#define _SUBSTRATE_SYS_TIME_H
#define _SUBSTRATE_SYS_RESOURCE_H

/* Host system headers for actual structs */
#include <sys/time.h>
#include <sys/resource.h>

typedef long clock_t;

/* Define mock variables for kern_times BEFORE including the source file so they can be accessed */
clock_t mock_kern_times_ret = 0;
/* We declare a void pointer for the mock struct tms so we can set it later */
void* mock_tms_val_ptr = NULL;

int mock_copyout_ret = 0;
int copyout(const void *kaddr, void *uaddr, size_t len) {
    if (mock_copyout_ret != 0) return mock_copyout_ret;
    memcpy(uaddr, kaddr, len);
    return 0;
}

/*
 * We `#include` the actual logic directly from perso_netbsd.c
 * It will bring in `sys/times.h` and `sys/kern_syscalls.h` which declare `struct tms` and `kern_times`.
 */
#include "../../sys/exec/perso/perso_netbsd.c"

/* Now we can define kern_times using the real struct tms type */
clock_t kern_times(struct tms *buf) {
    if (mock_kern_times_ret == (clock_t)-1) return (clock_t)-1;
    if (buf && mock_tms_val_ptr) {
        *buf = *(struct tms*)mock_tms_val_ptr;
    }
    return mock_kern_times_ret;
}

void test_getrusage_invalid_who() {
    printf("Test: netbsd_sys_getrusage invalid who...\n");
    struct rusage ru;
    int ret = netbsd_sys_getrusage(999, &ru);
    assert(ret == -EINVAL);
    printf("PASS\n");
}

void test_getrusage_kern_times_fail() {
    printf("Test: netbsd_sys_getrusage kern_times failure...\n");
    mock_kern_times_ret = (clock_t)-1;
    struct rusage ru;
    int ret = netbsd_sys_getrusage(RUSAGE_SELF, &ru);
    assert(ret == -1);
    mock_kern_times_ret = 0; // reset
    printf("PASS\n");
}

void test_getrusage_success_self() {
    printf("Test: netbsd_sys_getrusage success (RUSAGE_SELF)...\n");
    mock_kern_times_ret = 0;

    struct tms mock_t;
    memset(&mock_t, 0, sizeof(mock_t));
    mock_t.tms_utime = 128; // 1 second
    mock_t.tms_stime = 64;  // 0.5 second
    mock_tms_val_ptr = &mock_t;

    struct rusage ru;
    memset(&ru, 0, sizeof(ru));

    int ret = netbsd_sys_getrusage(RUSAGE_SELF, &ru);
    assert(ret == 0);
    assert(ru.ru_utime.tv_sec == 1);
    assert(ru.ru_utime.tv_usec == 0);
    assert(ru.ru_stime.tv_sec == 0);
    assert(ru.ru_stime.tv_usec == 500000);
    printf("PASS\n");
}

void test_getrusage_success_children() {
    printf("Test: netbsd_sys_getrusage success (RUSAGE_CHILDREN)...\n");
    mock_kern_times_ret = 0;

    struct tms mock_t;
    memset(&mock_t, 0, sizeof(mock_t));
    mock_t.tms_cutime = 256; // 2 seconds
    mock_t.tms_cstime = 192; // 1.5 seconds
    mock_tms_val_ptr = &mock_t;

    struct rusage ru;
    memset(&ru, 0, sizeof(ru));

    int ret = netbsd_sys_getrusage(RUSAGE_CHILDREN, &ru);
    assert(ret == 0);
    assert(ru.ru_utime.tv_sec == 2);
    assert(ru.ru_utime.tv_usec == 0);
    assert(ru.ru_stime.tv_sec == 1);
    assert(ru.ru_stime.tv_usec == 500000);
    printf("PASS\n");
}

void test_getrusage_copyout_fail() {
    printf("Test: netbsd_sys_getrusage copyout failure...\n");
    mock_kern_times_ret = 0;
    mock_copyout_ret = -14; // EFAULT

    struct rusage ru;
    int ret = netbsd_sys_getrusage(RUSAGE_SELF, &ru);
    assert(ret == -14);

    mock_copyout_ret = 0; // reset
    printf("PASS\n");
}

int main() {
    test_getrusage_invalid_who();
    test_getrusage_kern_times_fail();
    test_getrusage_success_self();
    test_getrusage_success_children();
    test_getrusage_copyout_fail();
    printf("All tests passed!\n");
    return 0;
}
