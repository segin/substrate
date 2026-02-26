/*
 * tests/sys/test_sysctl_copy.c
 *
 * Test copyin/copyout behavior in sysctl.
 */

#include <sys/sysctl.h>
#include <sys/errno.h>
#include <kern/console.h>
#include <string.h>

extern void *mock_fault_addr;
extern int sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
extern int copyin(const void *src, void *dst, size_t size);
extern int copyout(const void *src, void *dst, size_t size);

/* Custom sysctl handler */
static int test_copy_check(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
    (void)oidp;
    (void)arg1;
    (void)arg2;
    int error;
    int val = 12345;
    int new_val;

    if (req->oldptr) {
        error = copyout(&val, req->oldptr, sizeof(int));
        if (error) return error;
        req->oldidx = sizeof(int);
    }

    if (req->newptr) {
        error = copyin(req->newptr, &new_val, sizeof(int));
        if (error) return error;
        if (new_val != 67890) return EINVAL;
    }

    return 0;
}

/* debug.test_copy */
#define OID_DEBUG_TEST_COPY 8888

struct sysctl_oid sysctl_debug_test_copy = {
    &sysctl_debug_children,
    NULL,
    "test_copy",
    OID_DEBUG_TEST_COPY,
    CTLTYPE_INT | CTLFLAG_RW,
    NULL,
    0,
    NULL,
    test_copy_check,
    "Test Copy",
    0
};

int test_sysctl_copy_logic(void) {
    kprint("test_sysctl_copy_logic: starting...\n");

    sysctl_register_oid(&sysctl_debug_test_copy);

    int name[] = { CTL_DEBUG, OID_DEBUG_TEST_COPY };
    int error;
    int old_val;
    size_t old_len = sizeof(int);
    int new_val = 67890;
    int passed = 1;

    /* Case 1: Success */
    error = sys_sysctl(name, 2, &old_val, &old_len, &new_val, sizeof(int));
    if (error == 0 && old_val == 12345) {
        kprint("Case 1 (Success): PASS\n");
    } else {
        kprintf("Case 1 (Success): FAIL (error=%d, old_val=%d)\n", error, old_val);
        passed = 0;
    }

    /* Case 2: copyin failure (Bad newptr) */
    mock_fault_addr = (void*)0xDEADBEEF;
    error = sys_sysctl(name, 2, &old_val, &old_len, mock_fault_addr, sizeof(int));
    if (error == EFAULT) { // 14
        kprint("Case 2 (Bad newptr): PASS\n");
    } else {
        kprintf("Case 2 (Bad newptr): FAIL (error=%d)\n", error);
        passed = 0;
    }

    /* Case 3: copyout failure (Bad oldptr) */
    mock_fault_addr = (void*)0xCAFEBABE;
    size_t len = sizeof(int);
    error = sys_sysctl(name, 2, mock_fault_addr, &len, &new_val, sizeof(int));
    /* Note: sys_sysctl might return EFAULT from copyin of oldlenp if we passed bad pointer there,
       but here we pass bad pointer to oldptr buffer.
       The handler calls copyout(&val, req->oldptr, ...).
       If copyout fails, handler returns EFAULT.
       sys_sysctl propagates it. */
    if (error == EFAULT) {
        kprint("Case 3 (Bad oldptr): PASS\n");
    } else {
        kprintf("Case 3 (Bad oldptr): FAIL (error=%d)\n", error);
        passed = 0;
    }

    mock_fault_addr = NULL;
    sysctl_unregister_oid(&sysctl_debug_test_copy);
    return passed;
}
