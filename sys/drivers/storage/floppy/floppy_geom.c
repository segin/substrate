#include <drivers/storage/floppy/floppy.h>

static const fdc_geometry_t fdc_geometries[] = {
    { 1, "360K", 40, 2, 9, 2 },
    { 2, "1.2M", 80, 2, 15, 0 },
    { 3, "720K", 80, 2, 9, 1 },
    { 4, "1.44M", 80, 2, 18, 0 },
    { 5, "2.88M", 80, 2, 36, 0 },
};

const fdc_geometry_t *fdc_geometry_from_cmos(uint8_t type) {
    size_t i;

    for (i = 0; i < sizeof(fdc_geometries) / sizeof(fdc_geometries[0]); i++) {
        if (fdc_geometries[i].cmos_type == type) {
            return &fdc_geometries[i];
        }
    }
    return NULL;
}

void fdc_parse_cmos_drive_types(uint8_t reg10, uint8_t types[2]) {
    if (types == NULL) {
        return;
    }
    types[0] = (uint8_t)((reg10 >> 4) & 0x0F);
    types[1] = (uint8_t)(reg10 & 0x0F);
}

int fdc_lba_to_chs(const fdc_geometry_t *geom, uint32_t lba, fdc_chs_t *chs) {
    uint32_t sectors_per_cyl;

    if (geom == NULL || chs == NULL || geom->heads == 0 || geom->sectors_per_track == 0) {
        return -1;
    }

    sectors_per_cyl = (uint32_t)geom->heads * (uint32_t)geom->sectors_per_track;
    chs->cylinder = (uint16_t)(lba / sectors_per_cyl);
    chs->head = (uint8_t)((lba / geom->sectors_per_track) % geom->heads);
    chs->sector = (uint8_t)((lba % geom->sectors_per_track) + 1U);

    if (chs->cylinder >= geom->cylinders) {
        return -1;
    }
    return 0;
}

uint32_t fdc_chs_to_lba(const fdc_geometry_t *geom, uint16_t cylinder,
                        uint8_t head, uint8_t sector) {
    if (geom == NULL || head >= geom->heads || sector == 0 || sector > geom->sectors_per_track) {
        return UINT32_MAX;
    }

    return ((uint32_t)cylinder * geom->heads + head) * geom->sectors_per_track +
           ((uint32_t)sector - 1U);
}

int fdc_dma_window_valid(uintptr_t phys_addr, size_t len) {
    uintptr_t end;

    if (len == 0) {
        return 0;
    }
    end = phys_addr + len - 1U;
    if (end < phys_addr) {
        return 0;
    }
    if (end >= FDC_DMA_LIMIT) {
        return 0;
    }
    if ((phys_addr & 0xFFFF0000U) != (end & 0xFFFF0000U)) {
        return 0;
    }
    return 1;
}
