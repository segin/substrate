#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

static int test_split_data_segment_window(void) {
    struct elks_exec hdr;
    struct elks_load_plan plan;
    struct elks_segment_layout layout;

    memset(&hdr, 0, sizeof(hdr));
    memset(&plan, 0, sizeof(plan));
    memset(&layout, 0, sizeof(layout));
    hdr.type = ELKS_SPLITID;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 0x1800;
    hdr.dseg = 0x0200;
    hdr.bseg = 0x0080;
    hdr.chmem = 0x0400;
    hdr.minstack = 0x0200;

    if (!elks_build_load_plan(&hdr, NULL, 0, &plan)) {
        fprintf(stderr, "FAIL: split data test load plan rejected\n");
        return 0;
    }

    elks_build_segment_layout(&plan, &layout);

    if (layout.ds.base_addr != ELKS_DATA_BASE || layout.es.base_addr != ELKS_DATA_BASE) {
        fprintf(stderr, "FAIL: split data segment base wrong\n");
        return 0;
    }
    if (layout.ds.limit != elks_data_segment_limit(&plan) ||
        layout.es.limit != elks_data_segment_limit(&plan)) {
        fprintf(stderr, "FAIL: split data segment limit wrong\n");
        return 0;
    }
    if (plan.brk_offset != (uint16_t)(hdr.dseg + hdr.bseg)) {
        fprintf(stderr, "FAIL: split data break offset wrong\n");
        return 0;
    }
    if (plan.brk_offset > plan.data_limit) {
        fprintf(stderr, "FAIL: split data break offset exceeds segment\n");
        return 0;
    }
    return 1;
}

static int test_combined_data_segment_window(void) {
    struct elks_exec hdr;
    struct elks_load_plan plan;
    struct elks_segment_layout layout;

    memset(&hdr, 0, sizeof(hdr));
    memset(&plan, 0, sizeof(plan));
    memset(&layout, 0, sizeof(layout));
    hdr.type = ELKS_COMBID;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 0;
    hdr.tseg = 0x1000;
    hdr.dseg = 0x0100;
    hdr.bseg = 0x0040;

    if (!elks_build_load_plan(&hdr, NULL, 0, &plan)) {
        fprintf(stderr, "FAIL: combined data test load plan rejected\n");
        return 0;
    }

    elks_build_segment_layout(&plan, &layout);

    if (layout.ds.base_addr != ELKS_TEXT_BASE || layout.es.base_addr != ELKS_TEXT_BASE) {
        fprintf(stderr, "FAIL: combined data segment base wrong\n");
        return 0;
    }
    if (layout.ds.limit != elks_data_segment_limit(&plan) ||
        layout.es.limit != elks_data_segment_limit(&plan)) {
        fprintf(stderr, "FAIL: combined data segment limit wrong\n");
        return 0;
    }
    if (plan.brk_offset != (uint16_t)(hdr.dseg + hdr.bseg)) {
        fprintf(stderr, "FAIL: combined data break offset wrong\n");
        return 0;
    }
    if (plan.brk_offset >= plan.data_limit) {
        fprintf(stderr, "FAIL: combined data break offset not inside segment\n");
        return 0;
    }
    return 1;
}

int main(void) {
    if (!test_split_data_segment_window()) {
        return 1;
    }
    if (!test_combined_data_segment_window()) {
        return 1;
    }

    puts("host_test_elks_data_bounds: ok");
    return 0;
}
