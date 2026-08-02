#include <kern/console.h>
#include <pm/pm.h>
#include "tests.h"

/*
 * #425: this test used to index a flat `process_t processes[]` array.  The
 * process table is now the `allproc` list plus a PID hash, so the same two
 * invariants are checked through proc_find()/FOREACH_PROC.
 */
void run_pid_tests(void) {
    kprint("TEST: Checking PID invariants...\n");

    /* Test 1: the swapper must be PID 0 and must be reachable by lookup. */
    process_t *swapper = proc_find(0);
    if (swapper != NULL && swapper->pid == 0) {
        kprint("PASS: Swapper is PID 0\n");
    } else {
        kprint("FAIL: PID 0 does not resolve to the swapper\n");
    }

    /* Test 2: PID 1 should NOT exist yet (called before the kinit spawn). */
    if (proc_find(1) == NULL) {
        kprint("PASS: PID 1 does not exist (as expected before init spawn)\n");
    } else {
        kprint("INFO: PID 1 already exists\n");
    }

    /* Test 3: the list and the hash must agree -- every process reachable on
     * allproc has to be findable by its own PID. */
    int mismatched = 0;
    FOREACH_PROC(p) {
        if (proc_find(p->pid) != p)
            mismatched++;
    }
    if (mismatched == 0) {
        kprint("PASS: allproc and pid_hash agree\n");
    } else {
        kprint("FAIL: a process on allproc is not findable by PID\n");
    }
}
