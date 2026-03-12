/*
 * ide.c - ATA/IDE Driver Implementation
 *
 * Implements:
 * - PIO Mode transfers (LBA28 and LBA48)
 * - Bus Master DMA transfers
 * - PRDT (Physical Region Descriptor Table) management
 * - ATAPI packet command interface
 * - Primary/Secondary channels with Master/Slave drives
 *
 * References:
 * - ATA/ATAPI-6 Specification (T13/1410D)
 * - Intel PIIX4 Bus Master IDE Controller Datasheet
 */

#include <stdio.h>
#include <string.h>
#include <arch/i386/cpu.h>
#include <sys/random.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/isa.h>
#include <kern/pci.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/ide/ide.h>
#include <arch/x86-common/io.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <intr.h>
#include <pm/pm.h>

/*
 * ============================================================
 * Constants and Static Data
 * ============================================================
 */

/* Channel state */
static ide_channel_t ide_channels[MAX_IDE_CHANNELS];

/* Device state */
static ide_device_t ide_devices[MAX_IDE_DEVICES];
static int ide_device_count = 0;

/* Block device integration */
typedef struct {
    uint8_t channel;
    uint8_t drive;
    uint8_t type;  /* 0=ATA, 1=ATAPI */
} ide_drive_ctx_t;

static ide_drive_ctx_t ide_contexts[MAX_IDE_DEVICES];
static blkdev_t ide_blkdevs[MAX_IDE_DEVICES];
static int ide_attached;
static int ide_drivers_registered;

/* IRQ completion flag */
static volatile int ide_irq_complete[MAX_IDE_CHANNELS];

static void ide_wait_bsy(uint8_t channel);
static int ide_wait_drq(uint8_t channel);

static int ide_scan_controller(void);

static const char *const ide_isa_channel_names[MAX_IDE_CHANNELS] = {
    "ide-primary",
    "ide-secondary",
    "ide-tertiary",
    "ide-quaternary",
};

static const char *const ide_channel_labels[MAX_IDE_CHANNELS] = {
    "Primary",
    "Secondary",
    "Tertiary",
    "Quaternary",
};

static const char *const ide_drive_labels[2] = {
    "Master",
    "Slave",
};

static const uint16_t ide_default_io_bases[MAX_IDE_CHANNELS] = {
    ATA_PRIMARY_IO,
    ATA_SECONDARY_IO,
    ATA_TERTIARY_IO,
    ATA_QUATERNARY_IO,
};

static const uint16_t ide_default_ctrl_bases[MAX_IDE_CHANNELS] = {
    ATA_PRIMARY_CTRL,
    ATA_SECONDARY_CTRL,
    ATA_TERTIARY_CTRL,
    ATA_QUATERNARY_CTRL,
};

static const uint8_t ide_default_irqs[MAX_IDE_CHANNELS] = {
    ATA_PRIMARY_IRQ,
    ATA_SECONDARY_IRQ,
    ATA_TERTIARY_IRQ,
    ATA_QUATERNARY_IRQ,
};

static int ide_legacy_channel_present(const char *name) {
    struct device *dev;

    for (dev = isa_first_device(); dev != NULL; dev = isa_next_device(dev)) {
        if (strcmp(dev->name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static uintptr_t ide_pci_bar_base(pci_device_t *pdev, int bar) {
    uint32_t value;

    if (pdev == NULL || bar < 0 || bar >= PCI_BAR_COUNT) {
        return 0;
    }

    value = pci_read_config32(pdev->bus, pdev->slot, pdev->func,
                              (uint16_t)(0x10 + bar * 4));
    if (value == 0 || value == 0xFFFFFFFFU) {
        return 0;
    }

    if (value & 1U) {
        return (uintptr_t)(value & ~0x3U);
    }

    return (uintptr_t)(value & ~0xFU);
}

static void ide_configure_from_pci(void) {
    pci_device_t *pdev;

    for (pdev = pci_first_device(); pdev != NULL; pdev = pci_next_device(pdev)) {
        uintptr_t bm_base;

        if (pdev->kdev == NULL) {
            continue;
        }
        if (pdev->kdev->class != 0x01 || pdev->kdev->subclass != 0x01) {
            continue;
        }

        if (pdev->kdev->progif & 0x01) {
            uintptr_t io = ide_pci_bar_base(pdev, 0);
            uintptr_t ctrl = ide_pci_bar_base(pdev, 1);
            if (io != 0) {
                ide_channels[0].io_base = (uint16_t)io;
            }
            if (ctrl != 0) {
                ide_channels[0].ctrl_base = (uint16_t)(ctrl + 2);
            }
        }

        if (pdev->kdev->progif & 0x04) {
            uintptr_t io = ide_pci_bar_base(pdev, 2);
            uintptr_t ctrl = ide_pci_bar_base(pdev, 3);
            if (io != 0) {
                ide_channels[1].io_base = (uint16_t)io;
            }
            if (ctrl != 0) {
                ide_channels[1].ctrl_base = (uint16_t)(ctrl + 2);
            }
        }

        bm_base = ide_pci_bar_base(pdev, 4);
        if ((pdev->kdev->progif & 0x80) && bm_base != 0) {
            ide_dma_init((uint16_t)bm_base, (uint16_t)(bm_base + 8));
        }

        return;
    }
}

static int ide_identify_channel(uint8_t channel, uint8_t drive, void *buffer) {
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, 0);
    ide_write_reg(channel, ATA_REG_LBA_LOW, 0);
    ide_write_reg(channel, ATA_REG_LBA_MID, 0);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    if (ide_read_reg(channel, ATA_REG_STATUS) == 0) {
        return -1;
    }

    ide_wait_bsy(channel);

    if (ide_read_reg(channel, ATA_REG_LBA_MID) != 0 ||
        ide_read_reg(channel, ATA_REG_LBA_HIGH) != 0) {
        return -2;
    }

    if (ide_wait_drq(channel) < 0) {
        return -1;
    }

    insw(ide_channels[channel].io_base + ATA_REG_DATA, buffer, 256);
    return 0;
}

static int ide_identify_atapi_channel(uint8_t channel, uint8_t drive, void *buffer) {
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, 0);
    ide_write_reg(channel, ATA_REG_LBA_LOW, 0);
    ide_write_reg(channel, ATA_REG_LBA_MID, 0);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_ATAPI);

    if (ide_read_reg(channel, ATA_REG_STATUS) == 0) {
        return -1;
    }

    ide_wait_bsy(channel);
    if (ide_wait_drq(channel) < 0) {
        return -1;
    }

    insw(ide_channels[channel].io_base + ATA_REG_DATA, buffer, 256);
    return 0;
}

static int ide_isa_match(struct device *dev, struct driver *drv) {
    (void)drv;

    if (dev == NULL) {
        return 0;
    }

    return strcmp(dev->name, "ide-primary") == 0 ||
           strcmp(dev->name, "ide-secondary") == 0 ||
           strcmp(dev->name, "ide-tertiary") == 0 ||
           strcmp(dev->name, "ide-quaternary") == 0;
}

static int ide_attach_via_framework(struct device *dev) {
    (void)dev;

    if (ide_attached) {
        return -1;
    }

    return ide_scan_controller();
}

static const device_id_t ide_pci_ids[] = {
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x00010100U, 0x00FFFF00U, 0 },
    { 0, 0, 0, 0, 0 },
};

static struct driver ide_isa_driver = {
    .name = "ide-isa",
    .match_func = ide_isa_match,
    .attach = ide_attach_via_framework,
};

static struct driver ide_pci_driver = {
    .name = "ide-pci",
    .id_table = ide_pci_ids,
    .attach = ide_attach_via_framework,
};

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

static inline void ide_bm_write8(uint8_t channel, uint8_t reg, uint8_t data) {
    if (ide_channels[channel].bm_base) {
        outb(ide_channels[channel].bm_base + reg, data);
    }
}

static inline uint8_t ide_bm_read8(uint8_t channel, uint8_t reg) {
    if (ide_channels[channel].bm_base) {
        return inb(ide_channels[channel].bm_base + reg);
    }
    return 0;
}

static inline void ide_bm_write32(uint8_t channel, uint8_t reg, uint32_t data) {
    if (ide_channels[channel].bm_base) {
        outl(ide_channels[channel].bm_base + reg, data);
    }
}

/*
 * ============================================================
 * Wait Utilities
 * ============================================================
 */

/* Wait for BSY to clear */
static int ide_can_block_wait(void) {
    if (!current_thread || !current_process) {
        return 0;
    }

    /*
     * Early boot and kernel-task bring-up still exercise fragile context
     * switch paths. Keep IDE polling local until we are in ordinary
     * userspace or a later kernel worker.
     */
    if (current_process->is_kernel_task && current_process->pid <= 1) {
        return 0;
    }

    return 1;
}

static inline void ide_wait_backoff(int *yield_count) {
    if (ide_can_block_wait()) {
        if ((*yield_count)++ < 100) {
            sched_yield();
        } else {
            sched_sleep_until(NULL, get_ticks() + 1);
        }
        return;
    }

    for (int i = 0; i < 64; i++) {
        __asm__ volatile("pause");
    }
}

static void ide_wait_bsy(uint8_t channel) {
    uint64_t start = get_uptime_ms();
    int spins = 0;
    int yield_count = 0;
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY) {
        if (spins++ > 1000) {
            if (get_uptime_ms() - start > 1000) {
                kprint("ide: timeout waiting for BSY\n");
                break;
            }
            ide_wait_backoff(&yield_count);
        } else {
            __asm__ volatile("pause");
        }
    }
}

static int ide_wait_drq(uint8_t channel) {
    uint64_t start = get_uptime_ms();
    int spins = 0;
    int yield_count = 0;

    for (;;) {
        uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
        if (status & ATA_SR_DRQ) {
            return 0;
        }

        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            uint8_t error = ide_read_reg(channel, ATA_REG_ERROR);
            char decoded[64];

            ide_decode_error(error, decoded, sizeof(decoded));
            kprintf("ide: DRQ failed status=%02x error=%02x (%s)\n",
                    status, error, decoded);
            return -1;
        }

        if (get_uptime_ms() - start > 1000) {
            uint8_t error = ide_read_reg(channel, ATA_REG_ERROR);
            char decoded[64];

            ide_decode_error(error, decoded, sizeof(decoded));
            kprintf("ide: timeout waiting for DRQ status=%02x error=%02x (%s)\n",
                    status, error, decoded);
            return -1;
        }

        if (spins++ > 1000) {
            ide_wait_backoff(&yield_count);
        } else {
            __asm__ volatile("pause");
        }
    }
}

/* Wait with timeout (returns 0 on success, -1 on timeout/error) */
static int ide_wait_ready(uint8_t channel, int timeout_ms) {
    uint64_t start_ms = get_uptime_ms();
    int spins = 0;
    int yield_count = 0;
    
    /* 400ns delay (read alternate status 4 times) */
    for (int i = 0; i < 4; i++) {
        ide_read_ctrl(channel);
    }
    
    /* Wait for BSY to clear with timeout */
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY) {
        if (timeout_ms >= 0) {
            uint64_t current_ms = get_uptime_ms();
            if (current_ms - start_ms > (uint64_t)timeout_ms) {
                return -1;
            }
        }

        if (spins++ > 1000) {
            ide_wait_backoff(&yield_count);
        } else {
            __asm__ volatile("pause");
        }
    }
    
    /* Check for errors */
    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status & (ATA_SR_ERR | ATA_SR_DF)) {
        return -1;
    }
    
    return 0;
}

/* Select drive on channel */
static void ide_select_drive(uint8_t channel, uint8_t drive) {
    /* Select drive (0xA0 = master, 0xB0 = slave in LBA mode) */
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    
    /* 400ns delay */
    for (int i = 0; i < 4; i++) {
        ide_read_ctrl(channel);
    }
}

/*
 * ============================================================
 * PRDT (Physical Region Descriptor Table) Management
 * ============================================================
 */

/*
 * Setup PRDT for a DMA transfer
 *
 * The buffer may span multiple physical pages, so we need to
 * create a PRD entry for each contiguous physical region.
 *
 * Constraints:
 * - Each region must not cross a 64KB boundary
 * - PRD entries must be dword-aligned
 * - Total PRDT must not cross 64KB boundary
 * - Buffer must be physically contiguous (or we split it)
 */
int ide_prdt_setup(uint8_t channel, void *buffer, uint32_t byte_count) {
    int entry;
    uint32_t prdt_phys;

    if (channel >= MAX_IDE_CHANNELS) return -1;

    entry = ide_prdt_build_entries(ide_channels[channel].prdt,
                                   MAX_PRD_ENTRIES,
                                   (uint32_t)(uintptr_t)buffer,
                                   byte_count);
    if (entry < 0) {
        return -1;
    }

    /* Program PRDT base address into Bus Master */
    prdt_phys = (uint32_t)(uintptr_t)ide_channels[channel].prdt;
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
    if (!write) {
        cmd |= BM_CMD_WRITE;  /* Confusing: WRITE bit means "write to memory" = read from disk */
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
    ide_channels[0].bm_base = bm_base_primary;
    ide_channels[1].bm_base = bm_base_secondary;
    
    if (bm_base_primary) {
        ide_channels[0].dma_capable = 1;
        /* Clear status bits */
        ide_bm_clear_interrupt(0);
        kprintf("  IDE Primary: DMA enabled (BM base 0x%x)\n", (unsigned int)bm_base_primary);
    }
    
    if (bm_base_secondary) {
        ide_channels[1].dma_capable = 1;
        ide_bm_clear_interrupt(1);
        kprint("  IDE Secondary: DMA enabled\n");
    }
}

/*
 * ============================================================
 * DMA Transfer Operations
 * ============================================================
 */

/*
 * Perform DMA read
 */
int ide_dma_read(uint8_t channel, uint8_t drive, uint64_t lba, 
                 uint16_t count, void *buffer) {
    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (!ide_channels[channel].dma_capable) return -1;
    if (count == 0 || count > 256) return -1;
    
    uint32_t byte_count = (uint32_t)count * 512;
    
    /* Setup PRDT */
    if (ide_prdt_setup(channel, buffer, byte_count) < 0) {
        return -1;
    }
    
    /* Clear status and set direction */
    ide_bm_clear_interrupt(channel);
    ide_bm_write8(channel, BM_REG_COMMAND, BM_CMD_WRITE);  /* Read from disk = write to memory */
    
    /* Select drive and setup registers */
    ide_select_drive(channel, drive);
    ide_wait_bsy(channel);
    
    /* Use LBA48 for large addresses or counts */
    int use_lba48 = (lba >= 0x10000000ULL) || (count > 256);
    
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
    
    /* Start DMA */
    ide_irq_complete[channel] = 0;
    ide_bm_start(channel, 0);  /* 0 = read from disk */
    
    /* Wait for completion (interrupt-driven) */
    while (!ide_irq_complete[channel]) {
        uint32_t flags = intr_disable();
        if (!ide_irq_complete[channel]) {
            sched_sleep((void *)&ide_irq_complete[channel]);
        }
        intr_restore(flags);
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
 * Perform DMA write
 */
int ide_dma_write(uint8_t channel, uint8_t drive, uint64_t lba, 
                  uint16_t count, const void *buffer) {
    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (!ide_channels[channel].dma_capable) return -1;
    if (count == 0 || count > 256) return -1;
    
    uint32_t byte_count = (uint32_t)count * 512;
    
    /* Setup PRDT (cast away const - buffer won't be modified for write) */
    if (ide_prdt_setup(channel, (void *)buffer, byte_count) < 0) {
        return -1;
    }
    
    /* Clear status and set direction */
    ide_bm_clear_interrupt(channel);
    ide_bm_write8(channel, BM_REG_COMMAND, 0);  /* Write to disk = read from memory */
    
    /* Select drive */
    ide_select_drive(channel, drive);
    ide_wait_bsy(channel);
    
    int use_lba48 = (lba >= 0x10000000ULL) || (count > 256);
    
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
    
    /* Start DMA */
    ide_irq_complete[channel] = 0;
    ide_bm_start(channel, 1);  /* 1 = write to disk */
    
    /* Wait for completion */
    while (!ide_irq_complete[channel]) {
        uint32_t flags = intr_disable();
        if (!ide_irq_complete[channel]) {
            sched_sleep((void *)&ide_irq_complete[channel]);
        }
        intr_restore(flags);
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
    /* Determine channel from bus address */
    uint8_t channel;
    if (bus == ATA_PRIMARY_IO) {
        channel = 0;
    } else if (bus == ATA_SECONDARY_IO) {
        channel = 1;
    } else {
        return -1;
    }
    
    if (write) {
        return ide_dma_write(channel, drive, lba, count, phys_addr);
    } else {
        return ide_dma_read(channel, drive, lba, count, phys_addr);
    }
}

/*
 * ============================================================
 * PIO Transfer Operations (Original Implementation)
 * ============================================================
 */

int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, 
                     uint8_t count, void *buffer) {
    uint8_t channel = (bus == ATA_SECONDARY_IO) ? 1 : 0;
    
    ide_write_reg(channel, ATA_REG_DEVICE, 
                  0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, count);
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(channel);
        if (ide_wait_drq(channel) < 0) {
            return -1;
        }
        insw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, 
                         uint16_t count, void *buffer) {
    uint8_t channel = (bus == ATA_SECONDARY_IO) ? 1 : 0;
    
    ide_write_reg(channel, ATA_REG_DEVICE, 0x40 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)(count >> 8));
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)(lba >> 24));
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 40));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)count);
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_READ_PIO_EXT);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(channel);
        if (ide_wait_drq(channel) < 0) {
            return -1;
        }
        insw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba, 
                      uint8_t count, const void *buffer) {
    uint8_t channel = (bus == ATA_SECONDARY_IO) ? 1 : 0;
    
    ide_write_reg(channel, ATA_REG_DEVICE, 
                  0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, count);
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(channel);
        if (ide_wait_drq(channel) < 0) {
            return -1;
        }
        outsw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_write_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, 
                          uint16_t count, const void *buffer) {
    uint8_t channel = (bus == ATA_SECONDARY_IO) ? 1 : 0;
    
    ide_write_reg(channel, ATA_REG_DEVICE, 0x40 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)(count >> 8));
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)(lba >> 24));
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 32));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 40));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, (uint8_t)count);
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO_EXT);

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ide_wait_bsy(channel);
        if (ide_wait_drq(channel) < 0) {
            return -1;
        }
        outsw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

/*
 * ============================================================
 * Device Identification
 * ============================================================
 */

int ide_identify(uint16_t bus, uint8_t drive, void *buffer) {
    uint8_t channel = (bus == ATA_SECONDARY_IO) ? 1 : 0;
    
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, 0);
    ide_write_reg(channel, ATA_REG_LBA_LOW, 0);
    ide_write_reg(channel, ATA_REG_LBA_MID, 0);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0) return -1;

    ide_wait_bsy(channel);
    
    /* Check for ATAPI signature */
    if (ide_read_reg(channel, ATA_REG_LBA_MID) != 0 || 
        ide_read_reg(channel, ATA_REG_LBA_HIGH) != 0) {
        return -2;  /* Not ATA (might be ATAPI) */
    }

    if (ide_wait_drq(channel) < 0) {
        return -1;
    }

    insw(bus + ATA_REG_DATA, buffer, 256);
    return 0;
}

int ide_identify_atapi(uint16_t bus, uint8_t drive, void *buffer) {
    uint8_t channel = (bus == ATA_SECONDARY_IO) ? 1 : 0;
    
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, 0);
    ide_write_reg(channel, ATA_REG_LBA_LOW, 0);
    ide_write_reg(channel, ATA_REG_LBA_MID, 0);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_ATAPI);

    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0) return -1;

    ide_wait_bsy(channel);
    if (ide_wait_drq(channel) < 0) {
        return -1;
    }

    insw(bus + ATA_REG_DATA, buffer, 256);
    return 0;
}

/*
 * ============================================================
 * ATAPI Packet Command Interface
 * ============================================================
 *
 * ATAPI uses SCSI command packets sent over the ATA interface.
 * The PACKET command (0xA0) is followed by a 12-byte CDB (Command
 * Descriptor Block) containing the SCSI command.
 */

/* SCSI Command Codes (subset for CD-ROM) */
#define SCSI_TEST_UNIT_READY   0x00
#define SCSI_REQUEST_SENSE     0x03
#define SCSI_READ_6            0x08
#define SCSI_INQUIRY           0x12
#define SCSI_READ_CAPACITY     0x25
#define SCSI_READ_10           0x28
#define SCSI_READ_12           0xA8
#define SCSI_READ_TOC          0x43

/*
 * Send an ATAPI packet command
 *
 * Implements the ATAPI PIO protocol:
 * 1. Set byte count limit (transfer size)
 * 2. Issue PACKET command (0xA0)
 * 3. Wait for DRQ
 * 4. Send 12-byte CDB
 * 5. For data transfers: wait for DRQ, read/write data
 * 6. Check status
 */
int ide_atapi_packet(uint8_t channel, uint8_t drive, 
                     const uint8_t *packet, uint8_t packet_len,
                     void *buffer, uint32_t buffer_len, int write) {
    if (channel >= MAX_IDE_CHANNELS) return -1;
    if (packet_len != 12) return -1;  /* ATAPI uses 12-byte CDB */
    
    uint16_t bus = ide_channels[channel].io_base;
    
    /* Select drive */
    ide_select_drive(channel, drive);
    if (ide_wait_ready(channel, 500) < 0) return -1;
    
    /* Set byte count limit (in LBA_MID and LBA_HIGH) */
    /* This tells the device the maximum transfer size */
    ide_write_reg(channel, ATA_REG_LBA_MID, buffer_len & 0xFF);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (buffer_len >> 8) & 0xFF);
    
    /* Issue PACKET command */
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_PACKET);
    
    /* Wait for DRQ to send the command packet */
    ide_wait_bsy(channel);
    
    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status & ATA_SR_ERR) {
        return -1;
    }
    if (!(status & ATA_SR_DRQ)) {
        return -1;
    }
    
    /* Send the 12-byte CDB as 6 words */
    const uint16_t *pkt = (const uint16_t *)packet;
    for (int i = 0; i < 6; i++) {
        outw(bus + ATA_REG_DATA, pkt[i]);
    }
    
    /* For non-data commands, we're done */
    if (buffer_len == 0 || buffer == NULL) {
        ide_wait_bsy(channel);
        status = ide_read_reg(channel, ATA_REG_STATUS);
        return (status & ATA_SR_ERR) ? -1 : 0;
    }
    
    /* Data transfer phase */
    uint16_t *buf = (uint16_t *)buffer;
    uint32_t transferred = 0;
    
    while (transferred < buffer_len) {
        /* Wait for DRQ or completion */
        ide_wait_bsy(channel);
        status = ide_read_reg(channel, ATA_REG_STATUS);
        
        if (status & ATA_SR_ERR) {
            return -1;
        }
        
        if (!(status & ATA_SR_DRQ)) {
            /* No more data */
            break;
        }
        
        /* Get transfer size from byte count */
        uint16_t byte_count = ide_read_reg(channel, ATA_REG_LBA_MID) |
                             (ide_read_reg(channel, ATA_REG_LBA_HIGH) << 8);
        
        /* Transfer data */
        uint16_t words = byte_count / 2;
        
        /* Ensure we don't overflow the buffer */
        if (transferred + words * 2 > buffer_len) {
             words = (buffer_len - transferred) / 2;
        }

        if (words > 0) {
            if (write) {
                outsw(bus + ATA_REG_DATA, buf, words);
            } else {
                insw(bus + ATA_REG_DATA, buf, words);
            }
            buf += words;
            transferred += words * 2;
        } else {
            /* No words to transfer in this chunk? (odd length/zero) */
            break; 
        }
    }
    
    /* Wait for completion */
    ide_wait_bsy(channel);
    status = ide_read_reg(channel, ATA_REG_STATUS);
    
    return (status & ATA_SR_ERR) ? -1 : 0;
}

/*
 * ATAPI Read Capacity command
 *
 * Returns the last LBA and block size of the media.
 */
int ide_atapi_read_capacity(uint8_t channel, uint8_t drive, 
                            uint32_t *lba, uint32_t *block_size) {
    uint8_t packet[12] = {0};
    uint8_t response[8];
    
    packet[0] = SCSI_READ_CAPACITY;
    /* Rest are zeros */
    
    int ret = ide_atapi_packet(channel, drive, packet, 12, 
                               response, 8, 0);
    if (ret < 0) return ret;
    
    /* Response is big-endian */
    *lba = ((uint32_t)response[0] << 24) |
           ((uint32_t)response[1] << 16) |
           ((uint32_t)response[2] << 8) |
           response[3];
    
    *block_size = ((uint32_t)response[4] << 24) |
                  ((uint32_t)response[5] << 16) |
                  ((uint32_t)response[6] << 8) |
                  response[7];
    
    return 0;
}

/*
 * ATAPI Read Sectors (READ10/READ12)
 *
 * Reads sectors from ATAPI device (CD-ROM).
 * Uses READ10 for small counts, READ12 for large.
 */
int ide_atapi_read_sectors(uint8_t channel, uint8_t drive, 
                           uint32_t lba, uint16_t count, void *buffer) {
    uint8_t packet[12] = {0};
    
    /* All uint16_t counts fit in READ10 (max 65535 sectors) */
    /* Use READ10 - 10-byte CDB */
    packet[0] = SCSI_READ_10;
    packet[1] = 0;
    /* LBA (big-endian) */
    packet[2] = (lba >> 24) & 0xFF;
    packet[3] = (lba >> 16) & 0xFF;
    packet[4] = (lba >> 8) & 0xFF;
    packet[5] = lba & 0xFF;
    /* Reserved */
    packet[6] = 0;
    /* Transfer length (big-endian) */
    packet[7] = (count >> 8) & 0xFF;
    packet[8] = count & 0xFF;
    packet[9] = 0;
    /* Remaining bytes are zeros */
    
    /* CD-ROM sectors are typically 2048 bytes */
    uint32_t buffer_len = (uint32_t)count * 2048;
    
    return ide_atapi_packet(channel, drive, packet, 12, 
                            buffer, buffer_len, 0);
}

/*
 * ATAPI Read TOC (Table of Contents)
 *
 * Reads the CD Table of Contents for audio CD support.
 */
int ide_atapi_read_toc(uint8_t channel, uint8_t drive, 
                       uint8_t start_track, void *buffer, uint16_t buffer_len) {
    uint8_t packet[12] = {0};
    
    packet[0] = SCSI_READ_TOC;
    packet[1] = 0x02;  /* MSF format */
    packet[6] = start_track;
    packet[7] = (buffer_len >> 8) & 0xFF;
    packet[8] = buffer_len & 0xFF;
    
    return ide_atapi_packet(channel, drive, packet, 12, 
                            buffer, buffer_len, 0);
}


/*
 * ============================================================
 * Block Device Callbacks
 * ============================================================
 */

static int ide_blkdev_read(blkdev_t *dev, uint64_t sector, uint32_t count, 
                           void *buffer) {
    ide_drive_ctx_t *ctx = (ide_drive_ctx_t *)dev->priv;
    uint8_t channel = ctx->channel;
    uint8_t drive = ctx->drive;
    
    if (ctx->type == 1) { /* ATAPI */
        return ide_atapi_read_sectors(channel, drive, (uint32_t)sector, (uint16_t)count, buffer);
    }

    /* Use DMA if available, otherwise PIO */
    if (ide_channels[channel].dma_capable && count <= 256) {
        return ide_dma_read(channel, drive, sector, (uint16_t)count, buffer);
    }
    
    /* Fallback to PIO */
    uint16_t bus = ide_channels[channel].io_base;
    if (sector < 0x10000000ULL && count <= 256) {
        return ide_read_sectors(bus, drive, (uint32_t)sector, (uint8_t)count, buffer);
    } else {
        return ide_read_sectors_ext(bus, drive, sector, (uint16_t)count, buffer);
    }
}

static int ide_blkdev_write(blkdev_t *dev, uint64_t sector, uint32_t count, 
                            const void *buffer) {
    ide_drive_ctx_t *ctx = (ide_drive_ctx_t *)dev->priv;
    uint8_t channel = ctx->channel;
    uint8_t drive = ctx->drive;
    
    if (ctx->type == 1) { /* ATAPI */
        return -1; /* Write not supported yet */
    }

    if (ide_channels[channel].dma_capable && count <= 256) {
        return ide_dma_write(channel, drive, sector, (uint16_t)count, buffer);
    }
    
    uint16_t bus = ide_channels[channel].io_base;
    if (sector < 0x10000000ULL && count <= 256) {
        return ide_write_sectors(bus, drive, (uint32_t)sector, (uint8_t)count, buffer);
    } else {
        return ide_write_sectors_ext(bus, drive, sector, (uint16_t)count, buffer);
    }
}

/*
 * ============================================================
 * IRQ Handler
 * ============================================================
 */

void ide_irq_handler(int irq) {
    uint8_t channel = 0xFF;
    
    /* Harvest entropy from interrupt */
    struct {
        uint64_t tsc;
        int irq;
        uint8_t channel;
    } __attribute__((packed)) entropy;
    
    entropy.tsc = i386_cpu_cycle_counter();
    entropy.irq = irq;
    for (uint8_t i = 0; i < MAX_IDE_CHANNELS; i++) {
        if (ide_channels[i].irq == (uint8_t)irq) {
            channel = i;
            break;
        }
    }
    if (channel == 0xFF) {
        return;
    }
    entropy.channel = channel;
    
    random_harvest_fast(&entropy, sizeof(entropy));
    
    /* Read status to acknowledge interrupt */
    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    (void)status;
    
    /* Clear Bus Master interrupt */
    ide_bm_clear_interrupt(channel);
    
    /* Signal completion */
    ide_irq_complete[channel] = 1;

    /* Wake up waiting thread */
    sched_wakeup((void *)&ide_irq_complete[channel]);
}

/*
 * ============================================================
 * Initialization
 * ============================================================
 */

static int ide_scan_controller(void) {
    kprint("IDE Driver Initialized.\n");
    int legacy_hints[MAX_IDE_CHANNELS];
    int use_legacy_hints = 0;
    
    /* Setup channel structures */
    memset(ide_devices, 0, sizeof(ide_devices));
    memset(ide_contexts, 0, sizeof(ide_contexts));
    memset(ide_blkdevs, 0, sizeof(ide_blkdevs));
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        ide_irq_complete[ch] = 0;
    }
    ide_device_count = 0;
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        ide_channels[ch].io_base = ide_default_io_bases[ch];
        ide_channels[ch].ctrl_base = ide_default_ctrl_bases[ch];
        ide_channels[ch].irq = ide_default_irqs[ch];
        ide_channels[ch].bm_base = 0;
        ide_channels[ch].dma_capable = 0;
        legacy_hints[ch] = ide_legacy_channel_present(ide_isa_channel_names[ch]);
        use_legacy_hints |= legacy_hints[ch];
    }

    ide_configure_from_pci();
    
    /* Disable interrupts during probe */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        ide_write_ctrl((uint8_t)ch, ATA_CTRL_NIEN);
    }

    /* Probe all channels and drives */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        if (use_legacy_hints) {
            if (!legacy_hints[ch]) {
                continue;
            }
        }

        /* Check for floating bus */
        if (inb(ide_channels[ch].io_base + ATA_REG_STATUS) == 0xFF) continue;
        
        for (int d = 0; d < 2; d++) {
            uint16_t buf[256];
            memset(buf, 0, 512);
            
            int type = -1;
            int ret = ide_identify_channel((uint8_t)ch, (uint8_t)d, buf);

            if (ret == 0) {
                type = 0; /* ATA */
            } else if (ret == -2) {
                /* ATAPI signature detected, try ATAPI command */
                if (ide_identify_atapi_channel((uint8_t)ch, (uint8_t)d, buf) == 0) {
                    type = 1; /* ATAPI */
                }
            }

            if (type != -1 && ide_device_count < MAX_IDE_DEVICES) {
                uint64_t total_sectors;
                uint32_t sector_size = 512;

                ide_parse_identify_data(&ide_devices[ide_device_count], buf,
                                        (uint8_t)type, (uint8_t)ch,
                                        (uint8_t)d);
                total_sectors = ide_devices[ide_device_count].size;

                if (type == 1) {
                    /* ATAPI size calculation */
                    uint32_t lba, blk_size;
                    /* Try to read capacity. If fails (no media), size=0 */
                    if (ide_atapi_read_capacity(ch, d, &lba, &blk_size) == 0) {
                        total_sectors = (uint64_t)lba + 1;
                        sector_size = blk_size;
                    } else {
                        /* Default for CD-ROM if no media */
                        sector_size = 2048;
                    }
                }
                
                ide_devices[ide_device_count].size = total_sectors;
                
                /* Setup context */
                ide_contexts[ide_device_count].channel = ch;
                ide_contexts[ide_device_count].drive = d;
                ide_contexts[ide_device_count].type = type;
                
                /* Setup blkdev */
                blkdev_t *bdev = &ide_blkdevs[ide_device_count];
                memset(bdev, 0, sizeof(blkdev_t));
                
                bdev->name[0] = 'i';
                bdev->name[1] = 'd';
                bdev->name[2] = 'e';
                bdev->name[3] = '0' + ide_device_count;
                bdev->name[4] = '\0';
                
                bdev->sector_size = sector_size;
                bdev->total_sectors = total_sectors;
                bdev->priv = &ide_contexts[ide_device_count];
                bdev->read = ide_blkdev_read;
                bdev->write = ide_blkdev_write;
                
                blkdev_register(bdev);
                kprint("  ");
                kprint(bdev->name);
                kprint(": ");
                kprint(ide_devices[ide_device_count].model);
                kprint(" (");
                kprint(ide_channel_labels[ch]);
                kprint(" ");
                kprint(ide_drive_labels[d]);
                if (type == 1) kprint(", ATAPI");
                kprint(")\n");

                blkdev_scan_partitions(bdev);
                
                ide_device_count++;
            }
        }
    }
    
    /* Re-enable interrupts */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        ide_write_ctrl((uint8_t)ch, 0);
    }

    ide_attached = 1;
    return 0;
}

void ide_init(void) {
    if (ide_drivers_registered) {
        return;
    }

    (void)driver_register(&ide_isa_driver, &isa_bus_type);
    if (pci_present()) {
        (void)driver_register(&ide_pci_driver, &pci_bus_type);
    }
    ide_drivers_registered = 1;
}
