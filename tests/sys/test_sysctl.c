/*
 * tests/sys/test_sysctl.c
 *
 * Test sysctl functionality.
 */

#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <kern/console.h>
#include <string.h>

extern process_t *current_process;
extern int sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
extern void test_sysctl_handlers(void);

/* Custom sysctl handler to verify request parameters */
static int test_uid_check(struct sysctl_oid *oidp, void *arg1, int arg2, struct sysctl_req *req) {
    (void)oidp;
    (void)arg1;
    (void)arg2;

    if (!current_process) {
        kprint("test_uid_check: current_process is NULL\n");
        return EINVAL;
    }

    if (req->p_pid != current_process->pid) {
        kprintf("test_uid_check: p_pid mismatch. Expected %d, got %d\n", current_process->pid, req->p_pid);
        return EINVAL;
    }

    if (req->p_uid != current_process->uid) {
        kprintf("test_uid_check: p_uid mismatch. Expected %u, got %u\n", current_process->uid, req->p_uid);
        return EINVAL;
    }

    return 0;
}

/* Define a test sysctl node under 'debug' */
/* debug.9999 */
#define OID_DEBUG_TEST_UID 9999

struct sysctl_oid sysctl_debug_test_uid = {
    &sysctl_debug_children,
    NULL,
    "test_uid",
    OID_DEBUG_TEST_UID,
    CTLTYPE_INT | CTLFLAG_RD,
    NULL,
    0,
    NULL,
    test_uid_check,
    "Test UID check",
    0
};

void test_sysctl(void) {
    kprint("sysctl: starting tests...\n");

    /* Run handler unit tests */
    test_sysctl_handlers();

    int name[] = { CTL_DEBUG, OID_DEBUG_TEST_UID };
    int error;

    /* Register the test OID */
    sysctl_register_oid(&sysctl_debug_test_uid);

    /* Call sys_sysctl to trigger the handler */
    error = sys_sysctl(name, 2, NULL, NULL, NULL, 0);

    if (error == 0) {
        kprint("sysctl_test_uid: PASS\n");
    } else {
        kprintf("sysctl_test_uid: FAIL (error %d)\n", error);
    }

    /* Unregister */
    sysctl_unregister_oid(&sysctl_debug_test_uid);
}
