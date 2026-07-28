/*
 * ide_dma.c - Bus Master DMA: PRDT build/setup, BM control, and DMA
 * read/write transfer operations.
 */

#include <stdio.h>
#include <string.h>

#include <arch/i386/pmap.h>
#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>
#include <kern/console.h>

int ide_prdt_build_entries(prdt_entry_t *prdt, size_t max_entries,
                           uint32_t phys_addr, uint32_t byte_count) {
    uint32_t remaining = byte_count;
    size_t entry = 0;

    if (prdt == NULL || max_entries == 0) {
        return -1;
    }
    if (byte_count == 0 || byte_count > 256U * 512U) {
        return -1;
    }

    memset(prdt, 0, sizeof(*prdt) * max_entries);

    while (remaining > 0 && entry < max_entries) {
        uint32_t region_size = remaining;
        uint32_t boundary = (phys_addr & ~0xFFFFUL) + 0x10000UL;

        if (region_size > 65536U) {
            region_size = 65536U;
        }
        if (phys_addr + region_size > boundary) {
            region_size = boundary - phys_addr;
        }
        if (region_size == 0) {
            return -1;
        }

        prdt[entry].phys_addr = phys_addr;
        prdt[entry].byte_count = (region_size == 65536U) ? 0 : (uint16_t)region_size;
        prdt[entry].reserved = 0;
        prdt[entry].eot = 0;

        phys_addr += region_size;
        remaining -= region_size;
        entry++;
    }

    if (remaining != 0 || entry == 0) {
        return -1;
    }

    prdt[entry - 1].eot = 1;
    return (int)entry;
}

/*
 * ============================================================
 * PRDT Setup (per-channel page-aligned table)
 * ============================================================
 *
 * The buffer may span multiple physical pages, so we create a PRD entry
 * for each contiguous physical region.  Each region must not cross a
 * 64KB boundary; PRD entries are dword-aligned; the table must not cross
 * a 64KB boundary (guaranteed by the page-aligned ide_prdts[] slot).
 */
int ide_prdt_setup(uint8_t channel, void *buffer, uint32_t byte_count) {
    uintptr_t va;
    uintptr_t phys;
    uint32_t remaining;
    int entry;
    uint32_t prdt_phys;

    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (buffer == NULL || byte_count == 0 || byte_count > 256U * 512U) return -1;

    memset(ide_prdts[channel], 0, sizeof(ide_prdts[channel]));

    va = (uintptr_t)buffer;
    remaining = byte_count;
    entry = 0;

    while (remaining > 0) {
        uint32_t page_off;
        uint32_t chunk;

        if (entry >= MAX_PRD_ENTRIES) {
            return -1;
        }

        phys = pmap_extract(pmap_kernel(), va);
        if (phys == 0) {
            return -1;
        }

        page_off = (uint32_t)(va & 0xFFFU);
        chunk = 4096U - page_off;
        if (chunk > remaining) {
            chunk = remaining;
        }

        ide_prdts[channel][entry].phys_addr = (uint32_t)phys;
        ide_prdts[channel][entry].byte_count =
            (chunk == 65536U) ? 0 : (uint16_t)chunk;
        ide_prdts[channel][entry].reserved = 0;
        ide_prdts[channel][entry].eot = 0;

        va += chunk;
        remaining -= chunk;
        entry++;
    }

    ide_prdts[channel][entry - 1].eot = 1;

    /* Program PRDT base address into Bus Master */
    prdt_phys = (uint32_t)pmap_extract(pmap_kernel(),
                                       (uintptr_t)ide_prdts[channel]);
    if (prdt_phys == 0) {
        return -1;
    }
    ide_bm_write32(channel, BM_REG_PRDT, prdt_phys);

    return entry;
}

/*
 * ============================================================
 * Bus Master DMA Control
 * ============================================================
 */

void ide_bm_start(uint8_t channel, int write) {
    uint8_t cmd = BM_CMD_START;
    if (write) {
        cmd |= BM_CMD_WRITE;
    }
    ide_bm_write8(channel, BM_REG_COMMAND, cmd);
}

void ide_bm_stop(uint8_t channel) {
    ide_bm_write8(channel, BM_REG_COMMAND, 0);
}

uint8_t ide_bm_status(uint8_t channel) {
    return ide_bm_read8(channel, BM_REG_STATUS);
}

void ide_bm_clear_interrupt(uint8_t channel) {
    /* Write 1 to interrupt bit to clear it */
    uint8_t status = ide_bm_read8(channel, BM_REG_STATUS);
    ide_bm_write8(channel, BM_REG_STATUS, status | BM_STAT_INTERRUPT | BM_STAT_ERROR);
}

/*
 * ============================================================
 * DMA Initialization
 * ============================================================
 */

void ide_dma_init(uint16_t bm_base_primary, uint16_t bm_base_secondary) {
    ide_dma_init_pair(0, bm_base_primary, bm_base_secondary);
}

void ide_dma_init_pair(uint8_t base_channel, uint16_t bm_base_primary,
                       uint16_t bm_base_secondary) {
    if (base_channel >= MAX_IDE_CHANNELS) {
        return;
    }

    ide_channels[base_channel].bm_base = bm_base_primary;
    if (base_channel + 1 < MAX_IDE_CHANNELS) {
        ide_channels[base_channel + 1].bm_base = bm_base_secondary;
    }

    if (bm_base_primary) {
        ide_channels[base_channel].dma_capable = 1;
        ide_bm_clear_interrupt(base_channel);
        kprintf("  IDE %s: DMA enabled (BM base 0x%x)\n",
                ide_channel_labels[base_channel],
                (unsigned int)bm_base_primary);
    }

    if (base_channel + 1 < MAX_IDE_CHANNELS && bm_base_secondary) {
        ide_channels[base_channel + 1].dma_capable = 1;
        ide_bm_clear_interrupt((uint8_t)(base_channel + 1));
        kprintf("  IDE %s: DMA enabled (BM base 0x%x)\n",
                ide_channel_labels[base_channel + 1],
                (unsigned int)bm_base_secondary);
    }
}

/*
 * ============================================================
 * DMA Transfer Operations
 * ============================================================
 */

int ide_dma_read(uint8_t channel, uint8_t drive, uint64_t lba,
                 uint16_t count, void *buffer) {
    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (!ide_channels[channel].dma_capable) return -1;
    if (count == 0 || count > 256) return -1;
    if (ide_debug_enabled()) {
        kprintf("ide: dma-read ch=%u drive=%u lba=%llu count=%u\n",
                channel, drive, (unsigned long long)lba, count);
    }

    uint32_t byte_count = (uint32_t)count * 512;

    /* Setup PRDT */
    if (ide_prdt_setup(channel, buffer, byte_count) < 0) {
        if (ide_debug_enabled()) {
            kprintf("ide: dma-read prdt setup failed ch=%u\n", channel);
        }
        return -1;
    }

    /* Clear status and set direction */
    ide_bm_clear_interrupt(channel);
    ide_bm_write8(channel, BM_REG_COMMAND, 0);

    /* Select drive and ensure it is ready (BSY clear AND DRDY set) */
    ide_select_drive(channel, drive);
    if (ide_wait_ready(channel, IDE_TIMEOUT_READY_MS, "dma-read") < 0) {
        if (ide_debug_enabled()) {
            kprintf("ide: dma-read wait ready failed ch=%u\n", channel);
        }
        return -1;
    }

    /* Use LBA48 for large addresses or counts */
    int use_lba48 = (lba >= 0x10000000ULL) || (count > 256);

    ide_irq_complete[channel] = 0;

    if (use_lba48) {
        /* LBA48 mode */
        ide_write_reg(channel, ATA_REG_DEVICE, 0x40 | (drive << 4));

        /* High bytes first */
        ide_write_reg(channel, ATA_REG_SEC_COUNT, (count >> 8) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_LOW, (lba >> 24) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (lba >> 40) & 0xFF);

        /* Low bytes */
        ide_write_reg(channel, ATA_REG_SEC_COUNT, count & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_LOW, lba & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);

        ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_READ_DMA_EXT);
    } else {
        /* LBA28 mode */
        ide_write_reg(channel, ATA_REG_DEVICE,
                      0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
        ide_write_reg(channel, ATA_REG_SEC_COUNT, count & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_LOW, lba & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);

        ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_READ_DMA);
    }

    ide_bm_start(channel, 0);  /* 0 = read from disk */

    if (ide_debug_enabled()) {
        kprintf("ide: dma-read started ch=%u bm=%#x\n",
                channel, ide_bm_status(channel));
    }

    /* Wait for completion (interrupt-driven) */
    if (ide_wait_irq_completion(channel, IDE_TIMEOUT_DMA_MS, "dma-read") < 0) {
        if (ide_debug_enabled()) {
            kprintf("ide: dma-read wait completion failed ch=%u\n", channel);
        }
        return -1;
    }

    uint8_t bm_status = ide_bm_status(channel);
    uint8_t ide_status = ide_read_reg(channel, ATA_REG_STATUS);

    ide_bm_stop(channel);
    ide_bm_clear_interrupt(channel);

    if ((bm_status & BM_STAT_ERROR) || (ide_status & ATA_SR_ERR)) {
        return -1;
    }

    return 0;
}

int ide_dma_write(uint8_t channel, uint8_t drive, uint64_t lba,
                  uint16_t count, const void *buffer) {
    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (!ide_channels[channel].dma_capable) return -1;
    if (count == 0 || count > 256) return -1;
    if (ide_debug_enabled()) {
        kprintf("ide: dma-write ch=%u drive=%u lba=%llu count=%u\n",
                channel, drive, (unsigned long long)lba, count);
    }

    uint32_t byte_count = (uint32_t)count * 512;

    /* Setup PRDT (cast away const - buffer won't be modified for write) */
    if (ide_prdt_setup(channel, (void *)buffer, byte_count) < 0) {
        if (ide_debug_enabled()) {
            kprintf("ide: dma-write prdt setup failed ch=%u\n", channel);
        }
        return -1;
    }

    /* Clear status and set direction */
    ide_bm_clear_interrupt(channel);
    ide_bm_write8(channel, BM_REG_COMMAND, BM_CMD_WRITE);

    /* Select drive and ensure it is ready (BSY clear AND DRDY set) */
    ide_select_drive(channel, drive);
    if (ide_wait_ready(channel, IDE_TIMEOUT_READY_MS, "dma-write") < 0) {
        if (ide_debug_enabled()) {
            kprintf("ide: dma-write wait ready failed ch=%u\n", channel);
        }
        return -1;
    }

    int use_lba48 = (lba >= 0x10000000ULL) || (count > 256);

    ide_irq_complete[channel] = 0;

    if (use_lba48) {
        ide_write_reg(channel, ATA_REG_DEVICE, 0x40 | (drive << 4));

        ide_write_reg(channel, ATA_REG_SEC_COUNT, (count >> 8) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_LOW, (lba >> 24) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_MID, (lba >> 32) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (lba >> 40) & 0xFF);

        ide_write_reg(channel, ATA_REG_SEC_COUNT, count & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_LOW, lba & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);

        ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_DMA_EXT);
    } else {
        ide_write_reg(channel, ATA_REG_DEVICE,
                      0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
        ide_write_reg(channel, ATA_REG_SEC_COUNT, count & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_LOW, lba & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        ide_write_reg(channel, ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);

        ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_DMA);
    }

    ide_bm_start(channel, 1);  /* 1 = write to disk */

    if (ide_debug_enabled()) {
        kprintf("ide: dma-write started ch=%u bm=%#x\n",
                channel, ide_bm_status(channel));
    }

    /* Wait for completion */
    if (ide_wait_irq_completion(channel, IDE_TIMEOUT_DMA_MS, "dma-write") < 0) {
        if (ide_debug_enabled()) {
            kprintf("ide: dma-write wait completion failed ch=%u\n", channel);
        }
        return -1;
    }

    uint8_t bm_status = ide_bm_status(channel);
    uint8_t ide_status = ide_read_reg(channel, ATA_REG_STATUS);

    ide_bm_stop(channel);
    ide_bm_clear_interrupt(channel);

    if ((bm_status & BM_STAT_ERROR) || (ide_status & ATA_SR_ERR)) {
        return -1;
    }

    return 0;
}

/*
 * Legacy DMA setup function (compatibility wrapper)
 */
int ide_dma_setup(uint16_t bus, uint8_t drive, uint64_t lba,
                  uint16_t count, void *phys_addr, int write) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    if (write) {
        return ide_dma_write(channel, drive, lba, count, phys_addr);
    } else {
        return ide_dma_read(channel, drive, lba, count, phys_addr);
    }
}
