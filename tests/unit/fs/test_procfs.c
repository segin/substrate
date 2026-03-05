#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#include <vfs/vfs.h>
#include <include/sys/proc.h>
#include <pm/pm.h>
#include <kern/sched.h>
#include <exec/perso/personality.h>
#include <arch/i386/pmap.h>
#include <vm/vm_kmem.h>
#include <sys/lock.h>

extern process_t processes[MAX_PROCS];
extern process_t *current_process;

#include "../../../sys/fs/procfs.c"

bool test_proc_status_injection(void) {
    process_t temp_proc;
    memset(&temp_proc, 0, sizeof(process_t));
    temp_proc.pid = 999;
    strcpy(temp_proc.comm, "fake\nUid:\t0");
    temp_proc.uid = 1000;
    temp_proc.gid = 1000;

    char buffer[1024];
    proc_generate_status(buffer, sizeof(buffer), &temp_proc);

    if (strstr(buffer, "fake_Uid:_0") == NULL) {
        return false;
    }
    if (strstr(buffer, "Uid:\t0\n") != NULL) {
        return false;
    }
    if (strstr(buffer, "\nUid:\t0") != NULL) {
        return false;
    }
    return true;
}
