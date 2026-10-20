/*
 * host_test_compat - host unit test for sys_nice() in
 * sys/exec/perso/compat.c.
 *
 * compat.c is compiled whole against the kernel headers; the build rule
 * uses -ffunction-sections and --gc-sections so only what sys_nice()
 * reaches has to link.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../../sys/exec/perso/compat.c"

thread_t *current_thread;
process_t *current_process;

int main(void) {
    static thread_t nice_thread;
    static process_t nice_proc;

    printf("Testing sys_nice()...\n");

    /*
     * base_priority is the scheduler priority in [1,40]; the nice value is
     * 20 - base_priority, in [-20,19].
     */
    memset(&nice_thread, 0, sizeof(nice_thread));
    memset(&nice_proc, 0, sizeof(nice_proc));
    current_thread = &nice_thread;
    current_process = &nice_proc;

    /* Non-root: raising nice (lowering priority) is allowed. */
    nice_proc.euid = 1000;
    nice_thread.base_priority = 20;
    assert(sys_nice(5) == 5);
    assert(nice_thread.base_priority == 15);
    assert(nice_thread.priority == 15);

    /* Non-root: lowering nice (raising priority) is denied with EPERM. */
    assert(sys_nice(-1) == -EPERM);
    assert(nice_thread.base_priority == 15); /* unchanged */

    /* Clamp at the upper bound of nice (19). */
    nice_thread.base_priority = 20;
    assert(sys_nice(100) == 19);
    assert(nice_thread.base_priority == 1);

    /* Root may lower nice; clamp at the lower bound (-20). */
    nice_proc.euid = 0;
    nice_thread.base_priority = 20;
    assert(sys_nice(-100) == -20);
    assert(nice_thread.base_priority == 40);

    printf("All sys_nice tests passed!\n");
    puts("host_test_compat: ok");
    return 0;
}
