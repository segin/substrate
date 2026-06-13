/*
 * ide_regs.c - Low-level ATA and Bus Master register access.
 */

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>

#include <arch/x86-common/io.h>

/*
 * ============================================================
 * Low-Level Register Access
 * ============================================================
 */

void ide_write_reg(uint8_t channel, uint8_t reg, uint8_t data) {
    outb(ide_channels[channel].io_base + reg, data);
}

uint8_t ide_read_reg(uint8_t channel, uint8_t reg) {
    return inb(ide_channels[channel].io_base + reg);
}

void ide_write_ctrl(uint8_t channel, uint8_t data) {
    outb(ide_channels[channel].ctrl_base + ATA_REG_CONTROL, data);
}

uint8_t ide_read_ctrl(uint8_t channel) {
    return inb(ide_channels[channel].ctrl_base + ATA_REG_ALTSTATUS);
}

/*
 * ============================================================
 * Bus Master DMA Register Access
 * ============================================================
 */

void ide_bm_write8(uint8_t channel, uint8_t reg, uint8_t data) {
    if (ide_channels[channel].bm_base) {
        outb(ide_channels[channel].bm_base + reg, data);
    }
}

uint8_t ide_bm_read8(uint8_t channel, uint8_t reg) {
    if (ide_channels[channel].bm_base) {
        return inb(ide_channels[channel].bm_base + reg);
    }
    return 0;
}

void ide_bm_write32(uint8_t channel, uint8_t reg, uint32_t data) {
    if (ide_channels[channel].bm_base) {
        outl(ide_channels[channel].bm_base + reg, data);
    }
}
