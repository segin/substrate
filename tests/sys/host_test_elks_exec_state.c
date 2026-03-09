#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    process_t proc;
    struct elks_load_plan plan;

    memset(&proc, 0, sizeof(proc));
    memset(&plan, 0, sizeof(plan));
    plan.data_base = ELKS_DATA_BASE;
    plan.brk_offset = 0x0240;

    elks_apply_exec_state(&proc, &plan, "/bin/busyelks");

    if (proc.perso_id != PERS_ELKS) {
        fprintf(stderr, "FAIL: ELKS personality not applied\n");
        return 1;
    }
    if (proc.bitness != BITNESS_16) {
        fprintf(stderr, "FAIL: ELKS bitness not set to 16-bit\n");
        return 1;
    }
    if (proc.brk_start != ELKS_DATA_BASE + 0x0240 || proc.brk != proc.brk_start) {
        fprintf(stderr, "FAIL: ELKS break state wrong\n");
        return 1;
    }
    if (strcmp(proc.comm, "busyelks") != 0) {
        fprintf(stderr, "FAIL: ELKS process name wrong\n");
        return 1;
    }

    puts("host_test_elks_exec_state: ok");
    return 0;
}
