#include <exec/formats/elks_aout.h>
#include <stdio.h>
#include <string.h>

static int test_split_v1_defaults(void) {
    struct elks_exec hdr;
    struct elks_load_plan plan;

    memset(&hdr, 0, sizeof(hdr));
    memset(&plan, 0, sizeof(plan));
    hdr.type = ELKS_SPLITID;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 0x1200;
    hdr.dseg = 0x0200;
    hdr.bseg = 0x0100;

    if (!elks_build_load_plan(&hdr, NULL, 0, &plan)) {
        fprintf(stderr, "FAIL: split v1 load plan rejected\n");
        return 0;
    }
    if (plan.combined) {
        fprintf(stderr, "FAIL: split image marked combined\n");
        return 0;
    }
    if (plan.text_base != ELKS_TEXT_BASE || plan.data_base != ELKS_DATA_BASE) {
        fprintf(stderr, "FAIL: split image bases wrong\n");
        return 0;
    }
    if (plan.text_limit != hdr.tseg) {
        fprintf(stderr, "FAIL: split text limit wrong\n");
        return 0;
    }
    if (plan.data_limit != (uint16_t)(hdr.dseg + hdr.bseg + ELKS_INIT_STACK + ELKS_INIT_HEAP)) {
        fprintf(stderr, "FAIL: split data limit wrong\n");
        return 0;
    }
    return 1;
}

static int test_combined_v0_default(void) {
    struct elks_exec hdr;
    struct elks_load_plan plan;

    memset(&hdr, 0, sizeof(hdr));
    memset(&plan, 0, sizeof(plan));
    hdr.type = ELKS_COMBID;
    hdr.hlen = ELKS_MINIX_HDR_SIZE;
    hdr.version = 0;
    hdr.tseg = 0x1000;
    hdr.dseg = 0x0200;
    hdr.bseg = 0x0100;
    hdr.chmem = 0;

    if (!elks_build_load_plan(&hdr, NULL, 0, &plan)) {
        fprintf(stderr, "FAIL: combined v0 load plan rejected\n");
        return 0;
    }
    if (!plan.combined) {
        fprintf(stderr, "FAIL: combined image not marked combined\n");
        return 0;
    }
    if (plan.data_base != ELKS_TEXT_BASE) {
        fprintf(stderr, "FAIL: combined image data base wrong\n");
        return 0;
    }
    if (plan.text_limit != plan.data_limit) {
        fprintf(stderr, "FAIL: combined image limits diverged\n");
        return 0;
    }
    if (plan.brk_offset != (uint16_t)(hdr.dseg + hdr.bseg)) {
        fprintf(stderr, "FAIL: combined image brk offset wrong\n");
        return 0;
    }
    return 1;
}

static int test_fartext_offsets(void) {
    struct elks_exec hdr;
    struct elks_supl_hdr suph;
    struct elks_load_plan plan;

    memset(&hdr, 0, sizeof(hdr));
    memset(&suph, 0, sizeof(suph));
    memset(&plan, 0, sizeof(plan));
    hdr.type = ELKS_SPLITID_AHISTORICAL;
    hdr.hlen = ELKS_FARTEXT_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 0x0400;
    hdr.dseg = 0x0100;
    hdr.bseg = 0x0080;
    suph.esh_ftseg = 0x0300;

    if (!elks_build_load_plan(&hdr, &suph, 0, &plan)) {
        fprintf(stderr, "FAIL: far-text load plan rejected\n");
        return 0;
    }
    if (plan.fartext_size != suph.esh_ftseg) {
        fprintf(stderr, "FAIL: far-text size wrong\n");
        return 0;
    }
    if (plan.data_file_offset != (uint32_t)(hdr.hlen + hdr.tseg + suph.esh_ftseg)) {
        fprintf(stderr, "FAIL: far-text data offset wrong\n");
        return 0;
    }
    return 1;
}

static int test_bad_supplemental_header(void) {
    struct elks_exec hdr;
    struct elks_supl_hdr suph;
    struct elks_load_plan plan;

    memset(&hdr, 0, sizeof(hdr));
    memset(&suph, 0, sizeof(suph));
    memset(&plan, 0, sizeof(plan));
    hdr.type = ELKS_SPLITID;
    hdr.hlen = ELKS_FARTEXT_HDR_SIZE;
    hdr.version = 1;
    hdr.tseg = 0x0400;
    hdr.dseg = 0x0100;
    hdr.bseg = 0x0080;
    suph.msh_dbase = 1;

    if (elks_build_load_plan(&hdr, &suph, 0, &plan)) {
        fprintf(stderr, "FAIL: invalid supplemental header accepted\n");
        return 0;
    }
    return 1;
}

int main(void) {
    if (!test_split_v1_defaults()) {
        return 1;
    }
    if (!test_combined_v0_default()) {
        return 1;
    }
    if (!test_fartext_offsets()) {
        return 1;
    }
    if (!test_bad_supplemental_header()) {
        return 1;
    }

    puts("host_test_elks_load_plan: ok");
    return 0;
}
