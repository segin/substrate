#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <exec/perso/personality.h>
#include <sys/proc.h>

process_t *current_process;
static process_t proc;
static void *syscalls[4];
static struct personality test_pers;

void syscall_entry(void) {
}

struct personality *perso_lookup(int id) {
    return id == 77 ? &test_pers : NULL;
}

static uint64_t test_syscall(uint64_t a1, uint64_t a2, uint64_t a3,
                             uint64_t a4, uint64_t a5, uint64_t a6) {
    return a1 + a2 + a3 + a4 + a5 + a6;
}

#define HOST_TEST 1
#include "../../sys/arch/x86_64/syscall.c"

int main(void) {
    memset(&proc, 0, sizeof(proc));
    memset(&test_pers, 0, sizeof(test_pers));
    memset(syscalls, 0, sizeof(syscalls));

    proc.perso_id = 77;
    current_process = &proc;
    test_pers.syscall_count = 4;
    test_pers.syscall_table = syscalls;
    syscalls[2] = (void *)test_syscall;

    syscall_init_64();
    const struct syscall64_host_snapshot *snap = syscall64_host_get_snapshot();
    assert(snap->msr_lstar == (uint64_t)syscall_entry);
    assert(snap->msr_fmask == 0x0200);
    assert(snap->msr_star == (((uint64_t)0x08 << 32) | ((uint64_t)0x10 << 48)));

    syscall_handler_64(2, 1, 2, 3, 4, 5, 6);
    assert(snap->last_return == 21);

    syscall_handler_64(3, 1, 2, 3, 4, 5, 6);
    assert((int64_t)snap->last_return == -38);

    current_process = NULL;
    syscall_handler_64(2, 1, 2, 3, 4, 5, 6);
    assert((int64_t)snap->last_return == -38);

    puts("host_test_x86_64_syscall: PASS");
    return 0;
}
