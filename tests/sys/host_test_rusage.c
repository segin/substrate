#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <sys/proc.h>

process_t *current_process;
thread_t *current_thread;

#include "../../sys/pm/rusage.c"

static void test_rusage_fields_finalize_correctly(void) {
    process_t proc;

    memset(&proc, 0, sizeof(proc));
    rusage_init(&proc);

    for (int i = 0; i < 3; i++) {
        rusage_add_tick(&proc, 1);
    }
    for (int i = 0; i < 2; i++) {
        rusage_add_tick(&proc, 0);
    }

    rusage_add_fault(&proc, 0);
    rusage_add_fault(&proc, 1);
    rusage_add_ctx_switch(&proc, 1);
    rusage_add_ctx_switch(&proc, 0);
    rusage_update_maxrss(&proc, 32);

    proc.rusage_children.ru_utime.tv_sec = 1;
    proc.rusage_children.ru_stime.tv_usec = 500000;
    proc.rusage_children.ru_minflt = 2;
    proc.rusage_children.ru_majflt = 3;
    proc.rusage_children.ru_nvcsw = 4;
    proc.rusage_children.ru_nivcsw = 5;
    proc.rusage_children.ru_maxrss = 256;

    rusage_finalize(&proc);

    assert(proc.rusage.ru_utime.tv_sec == 1);
    assert(proc.rusage.ru_utime.tv_usec == 23436);
    assert(proc.rusage.ru_stime.tv_sec == 0);
    assert(proc.rusage.ru_stime.tv_usec == 515624);
    assert(proc.rusage.ru_minflt == 3);
    assert(proc.rusage.ru_majflt == 4);
    assert(proc.rusage.ru_nvcsw == 5);
    assert(proc.rusage.ru_nivcsw == 6);
    assert(proc.rusage.ru_maxrss == 256);
}

int main(void) {
    test_rusage_fields_finalize_correctly();
    puts("host_test_rusage: PASS");
    return 0;
}
