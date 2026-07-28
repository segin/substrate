/*
 * ide.c - ATA/IDE driver: shared global state, block-device callbacks,
 * the legacy polled IRQ handler, and driver initialization.
 *
 * The driver is split across several modules that share the globals
 * defined here (declared extern in ide_priv.h):
 *   ide_regs.c   - low-level ATA / Bus Master register access
 *   ide_wait.c   - readiness / completion wait helpers
 *   ide_cmd.c    - drive select and command issue (ide_issue_rw)
 *   ide_pio.c    - PIO sector transfers
 *   ide_dma.c    - Bus Master DMA: PRDT, BM control, DMA transfers
 *   ide_atapi.c  - ATAPI packet command interface
 *   ide_probe.c  - identify, DMA-mode, reset, IRQ, scan, power mgmt
 *   ide_identify.c / ide_pci.c - IDENTIFY parsing and PCI channel config
 *
 * References:
 * - ATA/ATAPI-6 Specification (T13/1410D)
 * - Intel PIIX4 Bus Master IDE Controller Datasheet
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <arch/i386/cpu.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/driver.h>
#include <kern/isa.h>
#include <kern/pci.h>
#include <kern/sched.h>
#include <sys/random.h>

/*
 * ============================================================
 * Shared Global State (declared extern in ide_priv.h)
 * ============================================================
 */

/* Channel state */
ide_channel_t ide_channels[MAX_IDE_CHANNELS];

/*
 * Keep each channel's PRDT in a dedicated page-aligned slot so the table
 * cannot straddle a 64KB boundary.
 */
prdt_entry_t ide_prdts[MAX_IDE_CHANNELS][MAX_PRD_ENTRIES]
    __attribute__((aligned(4096)));

/* Device state */
ide_device_t ide_devices[MAX_IDE_DEVICES];
int ide_device_count = 0;

/* Block device integration */
ide_drive_ctx_t ide_contexts[MAX_IDE_DEVICES];
blkdev_t ide_blkdevs[MAX_IDE_DEVICES];
int ide_attached;

/* IRQ completion flags */
volatile int ide_irq_complete[MAX_IDE_CHANNELS];

uint8_t ide_channel_irq_registered[MAX_IDE_CHANNELS];
uint8_t ide_channel_irq_shared[MAX_IDE_CHANNELS];

/* Label / default tables */
const char *const ide_isa_channel_names[MAX_IDE_CHANNELS] = {
    "ide-primary",
    "ide-secondary",
    "ide-tertiary",
    "ide-quaternary",
};

const char *const ide_channel_labels[MAX_IDE_CHANNELS] = {
    "Primary",
    "Secondary",
    "Tertiary",
    "Quaternary",
};

const char *const ide_drive_labels[2] = {
    "Master",
    "Slave",
};

const uint16_t ide_default_io_bases[MAX_IDE_CHANNELS] = {
    ATA_PRIMARY_IO,
    ATA_SECONDARY_IO,
    ATA_TERTIARY_IO,
    ATA_QUATERNARY_IO,
};

const uint16_t ide_default_ctrl_bases[MAX_IDE_CHANNELS] = {
    ATA_PRIMARY_CTRL,
    ATA_SECONDARY_CTRL,
    ATA_TERTIARY_CTRL,
    ATA_QUATERNARY_CTRL,
};

const uint8_t ide_default_irqs[MAX_IDE_CHANNELS] = {
    ATA_PRIMARY_IRQ,
    ATA_SECONDARY_IRQ,
    ATA_TERTIARY_IRQ,
    ATA_QUATERNARY_IRQ,
};

static int ide_drivers_registered;

int ide_debug_enabled(void) {
    return cmdline_debug_enabled("storage:ide");
}

/*
 * ============================================================
 * Single-shot Transfer Dispatch
 * ============================================================
 */

int ide_transfer_read_once(ide_drive_ctx_t *ctx, uint64_t sector,
                           uint32_t count, void *buffer) {
    uint8_t channel = ctx->channel;
    uint8_t drive = ctx->drive;
    uint16_t bus = ide_channels[channel].io_base;
    ide_device_t *dev = &ide_devices[ctx->index];

    if (ctx->type == 1) {
        return ide_atapi_read_sectors(channel, drive, (uint32_t)sector,
                                      (uint16_t)count, buffer);
    }

    if (ide_attached && ide_channels[channel].dma_capable &&
        !dev->dma_forced_pio && dev->dma_mode != 0 && count <= 256) {
        if (ide_dma_read(channel, drive, sector, (uint16_t)count, buffer) == 0) {
            return 0;
        }
        ide_disable_device_dma(dev, "read");
    }

    if (sector < 0x10000000ULL && count <= 256) {
        return ide_read_sectors(bus, drive, (uint32_t)sector, (uint8_t)count, buffer);
    }

    return ide_read_sectors_ext(bus, drive, sector, (uint16_t)count, buffer);
}

int ide_transfer_write_once(ide_drive_ctx_t *ctx, uint64_t sector,
                            uint32_t count, const void *buffer) {
    uint8_t channel = ctx->channel;
    uint8_t drive = ctx->drive;
    uint16_t bus = ide_channels[channel].io_base;
    ide_device_t *dev = &ide_devices[ctx->index];

    if (ctx->type == 1) {
        return -1;
    }

    if (ide_attached && ide_channels[channel].dma_capable &&
        !dev->dma_forced_pio && dev->dma_mode != 0 && count <= 256) {
        if (ide_dma_write(channel, drive, sector, (uint16_t)count, buffer) == 0) {
            return 0;
        }
        /*
         * Mirror the read path: on a DMA write failure, disable DMA for
         * this device and fall through to the PIO write path rather than
         * failing the whole transfer.
         */
        ide_disable_device_dma(dev, "write");
    }

    if (sector < 0x10000000ULL && count <= 256) {
        return ide_write_sectors(bus, drive, (uint32_t)sector, (uint8_t)count, buffer);
    }

    return ide_write_sectors_ext(bus, drive, sector, (uint16_t)count, buffer);
}

/*
 * ============================================================
 * Block Device Callbacks
 * ============================================================
 */

int ide_blkdev_read(blkdev_t *dev, uint64_t sector, uint32_t count,
                    void *buffer) {
    ide_drive_ctx_t *ctx = (ide_drive_ctx_t *)dev->priv;
    ide_device_t *ide_dev;
    int ret;

    if (ctx == NULL) {
        return -1;
    }
    ide_dev = &ide_devices[ctx->index];
    if (ide_dev->offline) {
        return -1;
    }

    /*
     * Serialize the whole transfer (and its retries/reset recovery) on the
     * channel.  The bio buffer cache drops its spinlock before calling
     * ->read, so two threads reading different sectors of the same channel
     * would otherwise interleave ATA command sequences on one register file
     * and leave the drive wedged (no DRQ / stuck BSY) -- the source of the
     * intermittent "pio-read timeout" + retry recoveries.
     */
    mutex_lock(&ide_channels[ctx->channel].lock);
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = ide_transfer_read_once(ctx, sector, count, buffer);
        if (ret >= 0) {
            ide_dev->offline = 0;
            ide_dev->reset_recovery_seen = 0;
            mutex_unlock(&ide_channels[ctx->channel].lock);
            return ret;
        }
        if (attempt < 2) {
            char msg[96];

            snprintf(msg, sizeof(msg),
                     "ide: ide%u read retry %d for LBA %llu count %u\n",
                     ctx->index, attempt + 1,
                     (unsigned long long)sector, (unsigned int)count);
            kprint(msg);
        }
    }

    ide_mark_offline(ctx, "read");
    mutex_unlock(&ide_channels[ctx->channel].lock);
    return -1;
}

int ide_blkdev_write(blkdev_t *dev, uint64_t sector, uint32_t count,
                     const void *buffer) {
    ide_drive_ctx_t *ctx = (ide_drive_ctx_t *)dev->priv;
    ide_device_t *ide_dev;
    int ret;

    if (ctx == NULL) {
        return -1;
    }
    ide_dev = &ide_devices[ctx->index];
    if (ide_dev->offline) {
        return -1;
    }

    /* Serialize on the channel -- see ide_blkdev_read(). */
    mutex_lock(&ide_channels[ctx->channel].lock);
    for (int attempt = 0; attempt < 3; attempt++) {
        ret = ide_transfer_write_once(ctx, sector, count, buffer);
        if (ret >= 0) {
            ide_dev->offline = 0;
            ide_dev->reset_recovery_seen = 0;
            mutex_unlock(&ide_channels[ctx->channel].lock);
            return ret;
        }
        if (attempt < 2) {
            char msg[96];

            snprintf(msg, sizeof(msg),
                     "ide: ide%u write retry %d for LBA %llu count %u\n",
                     ctx->index, attempt + 1,
                     (unsigned long long)sector, (unsigned int)count);
            kprint(msg);
        }
    }
    ide_mark_offline(ctx, "write");
    mutex_unlock(&ide_channels[ctx->channel].lock);
    return -1;
}

/*
 * ============================================================
 * Legacy Polled IRQ Handler
 * ============================================================
 *
 * The registered handler is ide_irq_dispatch() (ide_probe.c).  This
 * older int-irq entry point is retained for callers/tables that still
 * reference it by name.
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
