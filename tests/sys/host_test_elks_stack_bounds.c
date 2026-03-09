#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

static int test_stack_pointer_alignment(void) {
    struct elks_exec hdr;
    struct elks_load_plan plan;

    memset(&hdr, 0, sizeof(hdr));
    memset(&plan, 0, sizeof(plan));
    hdr.type = ELKS_SPLITID;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 0x1000;
    hdr.dseg = 0x0211;
    hdr.bseg = 0x0001;
    hdr.chmem = 0x0033;
    hdr.minstack = 0x0101;

    if (!elks_build_load_plan(&hdr, NULL, 0, &plan)) {
        fprintf(stderr, "FAIL: stack test load plan rejected\n");
        return 0;
    }
    if ((plan.stack_top & 1U) != 0) {
        fprintf(stderr, "FAIL: stack top is not even-aligned\n");
        return 0;
    }
    if (elks_initial_stack_pointer(&plan) != plan.stack_top) {
        fprintf(stderr, "FAIL: initial stack pointer diverges from stack top\n");
        return 0;
    }
    if (plan.stack_top > plan.data_limit) {
        fprintf(stderr, "FAIL: stack top exceeds data segment limit\n");
        return 0;
    }
    return 1;
}

static int test_stack_segment_window(void) {
    struct elks_load_plan plan;
    struct elks_segment_layout layout;

    memset(&plan, 0, sizeof(plan));
    memset(&layout, 0, sizeof(layout));
    plan.data_base = ELKS_DATA_BASE;
    plan.data_limit = 0x3800;
    plan.stack_top = 0x3800;

    elks_build_segment_layout(&plan, &layout);

    if (layout.ss.base_addr != plan.data_base) {
        fprintf(stderr, "FAIL: stack segment base wrong\n");
        return 0;
    }
    if (layout.ss.limit != (uint32_t)(plan.data_limit - 1U)) {
        fprintf(stderr, "FAIL: stack segment limit wrong\n");
        return 0;
    }
    if (elks_stack_segment_limit(&plan) != layout.ss.limit) {
        fprintf(stderr, "FAIL: stack helper limit wrong\n");
        return 0;
    }
    if ((uint32_t)elks_initial_stack_pointer(&plan) != (uint32_t)plan.data_limit) {
        fprintf(stderr, "FAIL: initial stack pointer is not at aligned top of stack window\n");
        return 0;
    }
    if ((uint32_t)elks_initial_stack_pointer(&plan) > layout.ss.limit + 1U) {
        fprintf(stderr, "FAIL: initial stack pointer exceeds stack segment window\n");
        return 0;
    }
    return 1;
}

int main(void) {
    if (!test_stack_pointer_alignment()) {
        return 1;
    }
    if (!test_stack_segment_window()) {
        return 1;
    }

    puts("host_test_elks_stack_bounds: ok");
    return 0;
}
