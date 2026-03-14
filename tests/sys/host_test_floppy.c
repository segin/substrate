#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <drivers/storage/floppy/floppy.h>

static void test_cmos_parse(void) {
    uint8_t types[2] = {0, 0};

    fdc_parse_cmos_drive_types(0x43, types);
    assert(types[0] == 4);
    assert(types[1] == 3);
}

static void test_geometry_lookup(void) {
    const fdc_geometry_t *g;

    g = fdc_geometry_from_cmos(4);
    assert(g != NULL);
    assert(g->cylinders == 80);
    assert(g->heads == 2);
    assert(g->sectors_per_track == 18);

    g = fdc_geometry_from_cmos(2);
    assert(g != NULL);
    assert(g->sectors_per_track == 15);

    assert(fdc_geometry_from_cmos(0) == NULL);
    assert(fdc_geometry_from_cmos(9) == NULL);
}

static void test_chs_roundtrip(void) {
    static const uint8_t types[] = { 1, 2, 3, 4, 5 };
    size_t i;

    for (i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        const fdc_geometry_t *g = fdc_geometry_from_cmos(types[i]);
        uint32_t total = (uint32_t)g->cylinders * g->heads * g->sectors_per_track;
        uint32_t probes[] = { 0, 1, g->sectors_per_track - 1U, g->sectors_per_track, total - 1U };
        size_t j;

        for (j = 0; j < sizeof(probes) / sizeof(probes[0]); j++) {
            fdc_chs_t chs;
            assert(fdc_lba_to_chs(g, probes[j], &chs) == 0);
            assert(fdc_chs_to_lba(g, chs.cylinder, chs.head, chs.sector) == probes[j]);
        }

        assert(fdc_lba_to_chs(g, total, &(fdc_chs_t){0}) < 0);
        assert(fdc_chs_to_lba(g, 0, g->heads, 1) == UINT32_MAX);
        assert(fdc_chs_to_lba(g, 0, 0, 0) == UINT32_MAX);
    }
}

static void test_dma_window_validation(void) {
    assert(fdc_dma_window_valid(0x00012000U, 512));
    assert(fdc_dma_window_valid(0x0001F000U, 4096));
    assert(!fdc_dma_window_valid(0x00FFF000U, 8192));
    assert(!fdc_dma_window_valid(0x01000000U, 512));
    assert(!fdc_dma_window_valid(0x00001000U, 0));
}

int main(void) {
    test_cmos_parse();
    test_geometry_lookup();
    test_chs_roundtrip();
    test_dma_window_validation();
    puts("host_test_floppy: PASS");
    return 0;
}
