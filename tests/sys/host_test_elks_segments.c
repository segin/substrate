#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    struct elks_load_plan plan;
    struct elks_segment_layout layout;

    memset(&plan, 0, sizeof(plan));
    memset(&layout, 0, sizeof(layout));

    plan.text_base = ELKS_TEXT_BASE;
    plan.data_base = ELKS_DATA_BASE;
    plan.text_limit = 0x2400;
    plan.data_limit = 0x3800;

    elks_build_segment_layout(&plan, &layout);

    if (layout.cs.entry_number != ELKS_LDT_CS_INDEX ||
        layout.ds.entry_number != ELKS_LDT_DS_INDEX ||
        layout.ss.entry_number != ELKS_LDT_SS_INDEX ||
        layout.es.entry_number != ELKS_LDT_ES_INDEX) {
        fprintf(stderr, "FAIL: ELKS LDT entry numbering wrong\n");
        return 1;
    }

    if (layout.cs.base_addr != plan.text_base ||
        layout.ds.base_addr != plan.data_base ||
        layout.ss.base_addr != plan.data_base ||
        layout.es.base_addr != plan.data_base) {
        fprintf(stderr, "FAIL: ELKS segment bases wrong\n");
        return 1;
    }

    if (layout.cs.limit != (uint32_t)(plan.text_limit - 1U) ||
        layout.ds.limit != (uint32_t)(plan.data_limit - 1U) ||
        layout.ss.limit != (uint32_t)(plan.data_limit - 1U) ||
        layout.es.limit != (uint32_t)(plan.data_limit - 1U)) {
        fprintf(stderr, "FAIL: ELKS segment limits wrong\n");
        return 1;
    }

    if (layout.cs_sel == layout.ds_sel ||
        layout.ds_sel == layout.ss_sel ||
        layout.ds_sel == layout.es_sel ||
        layout.ss_sel == layout.es_sel) {
        fprintf(stderr, "FAIL: ELKS selectors are not distinct\n");
        return 1;
    }

    puts("host_test_elks_segments: ok");
    return 0;
}
