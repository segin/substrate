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
#include <arch/i386/pmap.h>
#include <sys/random.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/isa.h>
#include <kern/pci.h>
#include <kern/cmdline.h>
#include <sys/irq.h>
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
/*
 * Keep each channel's PRDT in a dedicated page-aligned slot so the table
 * cannot straddle a 64KB boundary.
 */
static prdt_entry_t ide_prdts[MAX_IDE_CHANNELS][MAX_PRD_ENTRIES]
    __attribute__((aligned(4096)));

/* Device state */
static ide_device_t ide_devices[MAX_IDE_DEVICES];
static int ide_device_count = 0;

/* Block device integration */
typedef struct {
    uint8_t channel;
    uint8_t drive;
    uint8_t index;
    uint8_t type;  /* 0=ATA, 1=ATAPI */
} ide_drive_ctx_t;

static ide_drive_ctx_t ide_contexts[MAX_IDE_DEVICES];
static blkdev_t ide_blkdevs[MAX_IDE_DEVICES];
static int ide_attached;
static int ide_drivers_registered;
static uint8_t ide_channel_irq_registered[MAX_IDE_CHANNELS];
static uint8_t ide_channel_irq_shared[MAX_IDE_CHANNELS];

/* IRQ completion flag */
static volatile int ide_irq_complete[MAX_IDE_CHANNELS];

static int ide_debug_enabled(void) {
    return cmdline_debug_enabled("storage:ide");
}

static inline void ide_wait_backoff(int *yield_count);
static int ide_wait_bsy(uint8_t channel, uint32_t timeout_ms, const char *op);
static int ide_wait_drq(uint8_t channel, uint32_t timeout_ms, const char *op);
static int ide_wait_irq_completion(uint8_t channel, uint32_t timeout_ms,
                                   const char *op);
static int ide_wait_ready(uint8_t channel, int timeout_ms);
static int ide_identify_channel(uint8_t channel, uint8_t drive, void *buffer);
static int ide_identify_atapi_channel(uint8_t channel, uint8_t drive, void *buffer);
static void ide_select_drive(uint8_t channel, uint8_t drive);
static int ide_program_dma_mode(ide_device_t *dev);
static int ide_irq_dispatch(unsigned int irq, void *dev_id, void *frame);
static void ide_register_irqs(void);
static void ide_bm_set_drive_dma_capable(uint8_t channel, uint8_t drive, int enabled);
static void ide_disable_device_dma(ide_device_t *dev, const char *op);

static int ide_scan_controller(void);
static int ide_software_reset_channel(uint8_t channel);

static void ide_delay_ms(uint32_t delay_ms) {
    uint64_t start = get_uptime_ms();
    int yield_count = 0;

    while ((uint64_t)(get_uptime_ms() - start) < delay_ms) {
        ide_wait_backoff(&yield_count);
    }
}

static void ide_refresh_device_slot(uint8_t channel, uint8_t drive) {
    int slot = IDE_DEVICE_INDEX(channel, drive);
    uint16_t buf[256];
    int type = -1;
    uint64_t total_sectors;
    uint32_t sector_size = 512;
    uint8_t dma_forced_pio = 0;
    uint8_t reset_recovery_seen = 0;

    if (slot < 0 || slot >= MAX_IDE_DEVICES) {
        return;
    }

    dma_forced_pio = ide_devices[slot].dma_forced_pio;
    reset_recovery_seen = ide_devices[slot].reset_recovery_seen;

    memset(buf, 0, sizeof(buf));

    if (ide_identify_channel(channel, drive, buf) == 0) {
        type = 0;
    } else if (ide_identify_atapi_channel(channel, drive, buf) == 0) {
        type = 1;
    }

    if (type == -1) {
        return;
    }

    ide_parse_identify_data(&ide_devices[slot], buf, (uint8_t)type, channel, drive);
    ide_devices[slot].dma_forced_pio = dma_forced_pio;
    ide_devices[slot].reset_recovery_seen = reset_recovery_seen;
    total_sectors = ide_devices[slot].size;
    if (type == 1) {
        uint32_t lba;
        uint32_t blk_size;

        if (ide_atapi_read_capacity(channel, drive, &lba, &blk_size) == 0) {
            total_sectors = (uint64_t)lba + 1;
            sector_size = blk_size;
        } else {
            sector_size = 2048;
        }
    }

    ide_devices[slot].size = total_sectors;
    ide_devices[slot].offline = 0;
    ide_contexts[slot].type = (uint8_t)type;
    ide_blkdevs[slot].sector_size = sector_size;
    ide_blkdevs[slot].total_sectors = total_sectors;
    if (type == 0) {
        (void)ide_program_dma_mode(&ide_devices[slot]);
    }
}

static int ide_transfer_read_once(ide_drive_ctx_t *ctx, uint64_t sector,
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

static int ide_transfer_write_once(ide_drive_ctx_t *ctx, uint64_t sector,
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
        ide_disable_device_dma(dev, "write");
        return -1;
    }

    if (sector < 0x10000000ULL && count <= 256) {
        return ide_write_sectors(bus, drive, (uint32_t)sector, (uint8_t)count, buffer);
    }

    return ide_write_sectors_ext(bus, drive, sector, (uint16_t)count, buffer);
}

static void ide_mark_offline(ide_drive_ctx_t *ctx, const char *op) {
    ide_device_t *dev = &ide_devices[ctx->index];
    char msg[128];

    if (dev->offline) {
        return;
    }

    if (dev->reset_recovery_seen) {
        dev->offline = 1;
        snprintf(msg, sizeof(msg),
                 "ide: %s marking ide%u offline after repeated %s failures post-reset\n",
                 dev->model[0] ? dev->model : "(unknown)",
                 ctx->index,
                 op);
        kprint(msg);
        return;
    }

    if (ide_software_reset_channel(ctx->channel) == 0 && !dev->offline) {
        dev->reset_recovery_seen = 1;
        snprintf(msg, sizeof(msg),
                 "ide: ide%u recovered after channel reset during %s\n",
                 (unsigned int)ctx->index, op);
        kprint(msg);
        return;
    }

    dev->offline = 1;
    snprintf(msg, sizeof(msg),
             "ide: %s marking ide%u offline after repeated %s failures\n",
             dev->model[0] ? dev->model : "(unknown)",
             ctx->index,
             op);
    kprint(msg);
}

static void ide_disable_device_dma(ide_device_t *dev, const char *op) {
    char msg[128];

    if (dev == NULL || dev->type != 0) {
        return;
    }

    if (dev->dma_forced_pio) {
        return;
    }

    dev->dma_forced_pio = 1;
    dev->dma_mode = 0;
    ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
    snprintf(msg, sizeof(msg),
             "ide: ide%u disabling DMA after %s failure; falling back to PIO\n",
             IDE_DEVICE_INDEX(dev->channel, dev->drive),
             op ? op : "transfer");
    kprint(msg);
}

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

static int ide_channel_uses_default_legacy(uint8_t channel) {
    if (channel >= MAX_IDE_CHANNELS) {
        return 0;
    }

    return ide_channels[channel].io_base == ide_default_io_bases[channel] &&
           ide_channels[channel].ctrl_base == ide_default_ctrl_bases[channel] &&
           ide_channels[channel].irq == ide_default_irqs[channel] &&
           ide_channels[channel].bm_base == 0;
}

static int ide_legacy_channel_present(const char *name) {
    struct device *dev;

    for (dev = isa_first_device(); dev != NULL; dev = isa_next_device(dev)) {
        if (strcmp(dev->name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static void ide_configure_from_pci(void) {
    (void)ide_pci_configure_channels(ide_channels, ide_channel_irq_shared);
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

    if (ide_wait_bsy(channel, IDE_TIMEOUT_IDENTIFY_MS, "scan-identify") < 0) {
        return -1;
    }

    if (ide_read_reg(channel, ATA_REG_LBA_MID) != 0 ||
        ide_read_reg(channel, ATA_REG_LBA_HIGH) != 0) {
        return -2;
    }

    if (ide_wait_drq(channel, IDE_TIMEOUT_IDENTIFY_MS, "scan-identify") < 0) {
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

    if (ide_wait_bsy(channel, IDE_TIMEOUT_IDENTIFY_MS, "scan-identify-atapi") < 0) {
        return -1;
    }
    if (ide_wait_drq(channel, IDE_TIMEOUT_IDENTIFY_MS, "scan-identify-atapi") < 0) {
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

static int ide_irq_dispatch(unsigned int irq, void *dev_id, void *frame) {
    ide_channel_t *chan = (ide_channel_t *)dev_id;
    uint8_t channel;
    uint8_t status;
    uint8_t bm_status;

    (void)frame;

    if (chan == NULL || irq >= 256U) {
        return 0;
    }

    channel = (uint8_t)(chan - ide_channels);
    if (channel >= MAX_IDE_CHANNELS || ide_channels[channel].irq != (uint8_t)irq) {
        return 0;
    }

    if (ide_channels[channel].dma_capable) {
        bm_status = ide_bm_status(channel);
        if ((bm_status & BM_STAT_INTERRUPT) == 0) {
            return 0;
        }
    }

    status = ide_read_reg(channel, ATA_REG_STATUS);
    (void)status;
    ide_bm_clear_interrupt(channel);
    ide_irq_complete[channel] = 1;
    sched_wakeup((void *)&ide_irq_complete[channel]);
    return 1;
}

static void ide_register_irqs(void) {
    char name[16];

    for (uint8_t channel = 0; channel < MAX_IDE_CHANNELS; channel++) {
        unsigned long flags = 0;

        if (!ide_channels[channel].dma_capable || ide_channel_irq_registered[channel]) {
            continue;
        }

        if (ide_channel_irq_shared[channel]) {
            flags |= IRQF_SHARED;
        }

        snprintf(name, sizeof(name), "ide%u", (unsigned int)channel);
        if (request_irq(ide_channels[channel].irq, ide_irq_dispatch, flags,
                        name, &ide_channels[channel]) == 0) {
            ide_channel_irq_registered[channel] = 1;
        }
    }
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

static int ide_wait_bsy(uint8_t channel, uint32_t timeout_ms, const char *op) {
    uint64_t start = get_uptime_ms();
    int spins = 0;
    int yield_count = 0;
    while (ide_read_reg(channel, ATA_REG_STATUS) & ATA_SR_BSY) {
        if (spins++ > 1000) {
            if (get_uptime_ms() - start > timeout_ms) {
                uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
                uint8_t error = ide_read_reg(channel, ATA_REG_ERROR);
                char decoded[64];

                ide_decode_error(error, decoded, sizeof(decoded));
                kprintf("ide: %s timeout waiting for BSY status=%02x error=%02x (%s)\n",
                        op ? op : "command", status, error, decoded);
                return -1;
            }
            ide_wait_backoff(&yield_count);
        } else {
            __asm__ volatile("pause");
        }
    }

    return 0;
}

static int ide_wait_drq(uint8_t channel, uint32_t timeout_ms, const char *op) {
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
            kprintf("ide: %s DRQ failed status=%02x error=%02x (%s)\n",
                    op ? op : "command", status, error, decoded);
            return -1;
        }

        if (get_uptime_ms() - start > timeout_ms) {
            uint8_t error = ide_read_reg(channel, ATA_REG_ERROR);
            char decoded[64];

            ide_decode_error(error, decoded, sizeof(decoded));
            kprintf("ide: %s timeout waiting for DRQ status=%02x error=%02x (%s)\n",
                    op ? op : "command", status, error, decoded);
            return -1;
        }

        if (spins++ > 1000) {
            ide_wait_backoff(&yield_count);
        } else {
            __asm__ volatile("pause");
        }
    }
}

static int ide_wait_irq_completion(uint8_t channel, uint32_t timeout_ms,
                                   const char *op) {
    uint64_t ticks = ((uint64_t)timeout_ms * get_hz() + 999ULL) / 1000ULL;
    uint64_t deadline = get_ticks() + (ticks ? ticks : 1);
    uint64_t last_ticks = get_ticks();
    uint32_t stalled_polls = 0;
    int yield_count = 0;

    while (!ide_irq_complete[channel]) {
        if (ide_channels[channel].dma_capable) {
            uint8_t bm_status = ide_bm_status(channel);
            uint8_t ata_status = ide_read_reg(channel, ATA_REG_STATUS);
            if (bm_status & (BM_STAT_INTERRUPT | BM_STAT_ERROR)) {
                ide_irq_complete[channel] = 1;
                break;
            }
            if (ata_status & (ATA_SR_ERR | ATA_SR_DF)) {
                char decoded[64];
                uint8_t error = ide_read_reg(channel, ATA_REG_ERROR);

                ide_decode_error(error, decoded, sizeof(decoded));
                kprintf("ide: %s aborted status=%02x bm=%02x error=%02x (%s) on channel %u\n",
                        op ? op : "dma", ata_status, bm_status, error, decoded,
                        channel);
                ide_bm_stop(channel);
                ide_bm_clear_interrupt(channel);
                return -1;
            }
        }

        if ((int64_t)(get_ticks() - deadline) >= 0) {
            kprintf("ide: %s timeout waiting for DMA completion on channel %u (status=%02x bm=%02x)\n",
                    op ? op : "dma", channel,
                    ide_read_reg(channel, ATA_REG_STATUS),
                    ide_bm_status(channel));
            ide_bm_stop(channel);
            ide_bm_clear_interrupt(channel);
            return -1;
        }

        if (get_ticks() == last_ticks) {
            if (++stalled_polls >= 500000U) {
                kprintf("ide: %s timeout waiting for DMA completion on channel %u (timer stalled status=%02x bm=%02x)\n",
                        op ? op : "dma", channel,
                        ide_read_reg(channel, ATA_REG_STATUS),
                        ide_bm_status(channel));
                ide_bm_stop(channel);
                ide_bm_clear_interrupt(channel);
                return -1;
            }
        } else {
            last_ticks = get_ticks();
            stalled_polls = 0;
        }

        ide_wait_backoff(&yield_count);
    }

    return 0;
}

static int ide_software_reset_channel(uint8_t channel) {
    uint8_t ctrl;

    if (channel >= MAX_IDE_CHANNELS) {
        return -1;
    }

    ctrl = ide_channels[channel].no_intr ? ATA_CTRL_NIEN : 0;
    ide_write_ctrl(channel, ctrl | ATA_CTRL_SRST);
    ide_delay_ms(5);
    ide_write_ctrl(channel, ctrl);
    ide_delay_ms(2);

    for (uint8_t drive = 0; drive < 2; drive++) {
        ide_select_drive(channel, drive);
        if (ide_wait_bsy(channel, IDE_TIMEOUT_IDENTIFY_MS, "soft-reset") < 0) {
            return -1;
        }
    }

    for (uint8_t drive = 0; drive < 2; drive++) {
        ide_refresh_device_slot(channel, drive);
    }

    return 0;
}

static void ide_bm_set_drive_dma_capable(uint8_t channel, uint8_t drive, int enabled) {
    uint8_t status;
    uint8_t bit;

    if (channel >= MAX_IDE_CHANNELS || drive > 1 || !ide_channels[channel].dma_capable) {
        return;
    }

    bit = (drive == 0) ? BM_STAT_DRIVE0_DMA : BM_STAT_DRIVE1_DMA;
    status = ide_bm_status(channel);
    if (enabled) {
        status |= bit;
    } else {
        status &= (uint8_t)~bit;
    }
    ide_bm_write8(channel, BM_REG_STATUS, status);
}

static int ide_program_dma_mode(ide_device_t *dev) {
    uint8_t mode;
    uint8_t status;

    if (dev == NULL || !dev->present || dev->type != 0) {
        return -1;
    }

    if (dev->dma_forced_pio) {
        ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
        dev->dma_mode = 0;
        return -1;
    }

    if (!ide_channels[dev->channel].dma_capable ||
        (dev->feature_flags & IDE_FEATURE_DMA) == 0 ||
        ide_select_dma_transfer_mode(dev, &mode) < 0) {
        ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
        dev->dma_mode = 0;
        return -1;
    }

    ide_select_drive(dev->channel, dev->drive);
    if (ide_wait_ready(dev->channel, IDE_TIMEOUT_READY_MS) < 0) {
        return -1;
    }

    ide_write_reg(dev->channel, ATA_REG_FEATURES, ATA_FEAT_SET_TRANSFER_MODE);
    ide_write_reg(dev->channel, ATA_REG_SEC_COUNT, mode);
    ide_write_reg(dev->channel, ATA_REG_COMMAND, ATA_CMD_SET_FEATURES);

    if (ide_wait_bsy(dev->channel, IDE_TIMEOUT_READY_MS, "set-features") < 0) {
        ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
        return -1;
    }

    status = ide_read_reg(dev->channel, ATA_REG_STATUS);
    if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
        ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
        return -1;
    }

    ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 1);
    dev->dma_mode = (mode >= ATA_XFER_MODE_UDMA_BASE) ? 1 : 2;
    return 0;
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

/*
 * Perform DMA read
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
    
    /* Select drive and setup registers */
    ide_select_drive(channel, drive);
    if (ide_wait_bsy(channel, IDE_TIMEOUT_READY_MS, "dma-read") < 0) {
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

/*
 * Perform DMA write
 */
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
    
    /* Select drive */
    ide_select_drive(channel, drive);
    if (ide_wait_bsy(channel, IDE_TIMEOUT_READY_MS, "dma-write") < 0) {
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

/*
 * ============================================================
 * PIO Transfer Operations (Original Implementation)
 * ============================================================
 */

int ide_read_sectors(uint16_t bus, uint8_t drive, uint32_t lba, 
                     uint8_t count, void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    
    ide_write_reg(channel, ATA_REG_DEVICE, 
                  0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, count);
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_READ_PIO);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-read-ext") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-read-ext") < 0) {
            return -1;
        }
        insw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_read_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, 
                         uint16_t count, void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    
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
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-read") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-read") < 0) {
            return -1;
        }
        insw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_write_sectors(uint16_t bus, uint8_t drive, uint32_t lba, 
                      uint8_t count, const void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    
    ide_write_reg(channel, ATA_REG_DEVICE, 
                  0xE0 | (drive << 4) | ((lba >> 24) & 0x0F));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, count);
    ide_write_reg(channel, ATA_REG_LBA_LOW, (uint8_t)lba);
    ide_write_reg(channel, ATA_REG_LBA_MID, (uint8_t)(lba >> 8));
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (uint8_t)(lba >> 16));
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-write-ext") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-write-ext") < 0) {
            return -1;
        }
        outsw(bus + ATA_REG_DATA, buf, 256);
        buf += 256;
    }
    return 0;
}

int ide_write_sectors_ext(uint16_t bus, uint8_t drive, uint64_t lba, 
                          uint16_t count, const void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    
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
        if (ide_wait_bsy(channel, IDE_TIMEOUT_DATA_MS, "pio-write") < 0) {
            return -1;
        }
        if (ide_wait_drq(channel, IDE_TIMEOUT_DATA_MS, "pio-write") < 0) {
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
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, 0);
    ide_write_reg(channel, ATA_REG_LBA_LOW, 0);
    ide_write_reg(channel, ATA_REG_LBA_MID, 0);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0) return -1;

    if (ide_wait_bsy(channel, IDE_TIMEOUT_IDENTIFY_MS, "identify") < 0) {
        return -1;
    }
    
    /* Check for ATAPI signature */
    if (ide_read_reg(channel, ATA_REG_LBA_MID) != 0 || 
        ide_read_reg(channel, ATA_REG_LBA_HIGH) != 0) {
        return -2;  /* Not ATA (might be ATAPI) */
    }

    if (ide_wait_drq(channel, IDE_TIMEOUT_IDENTIFY_MS, "identify") < 0) {
        return -1;
    }

    insw(bus + ATA_REG_DATA, buffer, 256);
    return 0;
}

int ide_identify_atapi(uint16_t bus, uint8_t drive, void *buffer) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }
    
    ide_write_reg(channel, ATA_REG_DEVICE, 0xA0 | (drive << 4));
    ide_write_reg(channel, ATA_REG_SEC_COUNT, 0);
    ide_write_reg(channel, ATA_REG_LBA_LOW, 0);
    ide_write_reg(channel, ATA_REG_LBA_MID, 0);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, 0);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_IDENTIFY_ATAPI);

    uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0) return -1;

    if (ide_wait_bsy(channel, IDE_TIMEOUT_IDENTIFY_MS, "identify-atapi") < 0) {
        return -1;
    }
    if (ide_wait_drq(channel, IDE_TIMEOUT_IDENTIFY_MS, "identify-atapi") < 0) {
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
    if (ide_wait_ready(channel, IDE_TIMEOUT_PACKET_MS) < 0) return -1;
    
    /* Set byte count limit (in LBA_MID and LBA_HIGH) */
    /* This tells the device the maximum transfer size */
    ide_write_reg(channel, ATA_REG_LBA_MID, buffer_len & 0xFF);
    ide_write_reg(channel, ATA_REG_LBA_HIGH, (buffer_len >> 8) & 0xFF);
    
    /* Issue PACKET command */
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_PACKET);
    
    /* Wait for DRQ to send the command packet */
    if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-command") < 0) {
        return -1;
    }
    
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
        if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-nodata") < 0) {
            return -1;
        }
        status = ide_read_reg(channel, ATA_REG_STATUS);
        return (status & ATA_SR_ERR) ? -1 : 0;
    }
    
    /* Data transfer phase */
    uint16_t *buf = (uint16_t *)buffer;
    uint32_t transferred = 0;
    
    while (transferred < buffer_len) {
        /* Wait for DRQ or completion */
        if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-data") < 0) {
            return -1;
        }
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
    if (ide_wait_bsy(channel, IDE_TIMEOUT_PACKET_MS, "packet-complete") < 0) {
        return -1;
    }
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
    ide_device_t *ide_dev;
    int ret;

    if (ctx == NULL) {
        return -1;
    }
    ide_dev = &ide_devices[ctx->index];
    if (ide_dev->offline) {
        return -1;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        ret = ide_transfer_read_once(ctx, sector, count, buffer);
        if (ret >= 0) {
            ide_dev->offline = 0;
            ide_dev->reset_recovery_seen = 0;
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
    return -1;
}

static int ide_blkdev_write(blkdev_t *dev, uint64_t sector, uint32_t count, 
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

    for (int attempt = 0; attempt < 3; attempt++) {
        ret = ide_transfer_write_once(ctx, sector, count, buffer);
        if (ret >= 0) {
            ide_dev->offline = 0;
            ide_dev->reset_recovery_seen = 0;
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
    return -1;
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
    blkdev_t *partition_scan[MAX_IDE_DEVICES];
    size_t partition_scan_count = 0;
    
    /* Setup channel structures */
    memset(ide_devices, 0, sizeof(ide_devices));
    memset(ide_contexts, 0, sizeof(ide_contexts));
    memset(ide_blkdevs, 0, sizeof(ide_blkdevs));
    memset(ide_channel_irq_registered, 0, sizeof(ide_channel_irq_registered));
    memset(ide_channel_irq_shared, 0, sizeof(ide_channel_irq_shared));
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
    ide_register_irqs();
    
    /* Disable interrupts during probe */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        ide_write_ctrl((uint8_t)ch, ATA_CTRL_NIEN);
    }

    /* Probe all channels and drives */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        if (use_legacy_hints) {
            if (ch < 2 && !legacy_hints[ch] &&
                ide_channel_uses_default_legacy((uint8_t)ch)) {
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

            if (type != -1) {
                int slot = IDE_DEVICE_INDEX(ch, d);
                uint64_t total_sectors;
                uint32_t sector_size = 512;
                blkdev_t *bdev;

                if (slot < 0 || slot >= MAX_IDE_DEVICES) {
                    continue;
                }

                if (!ide_devices[slot].present) {
                    ide_device_count++;
                }

                ide_parse_identify_data(&ide_devices[slot], buf,
                                        (uint8_t)type, (uint8_t)ch,
                                        (uint8_t)d);
                if (type == 0) {
                    (void)ide_program_dma_mode(&ide_devices[slot]);
                }
                total_sectors = ide_devices[slot].size;

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
                
                ide_devices[slot].size = total_sectors;
                
                /* Setup context */
                ide_contexts[slot].channel = ch;
                ide_contexts[slot].drive = d;
                ide_contexts[slot].index = (uint8_t)slot;
                ide_contexts[slot].type = type;
                
                /* Setup blkdev */
                bdev = &ide_blkdevs[slot];
                memset(bdev, 0, sizeof(blkdev_t));
                
                bdev->name[0] = 'i';
                bdev->name[1] = 'd';
                bdev->name[2] = 'e';
                bdev->name[3] = '0' + slot;
                bdev->name[4] = '\0';
                
                bdev->sector_size = sector_size;
                bdev->total_sectors = total_sectors;
                bdev->priv = &ide_contexts[slot];
                bdev->read = ide_blkdev_read;
                bdev->write = ide_blkdev_write;
                
                blkdev_register(bdev);
                kprint("  ");
                kprint(bdev->name);
                kprint(": ");
                kprint(ide_devices[slot].model);
                kprint(" (");
                kprint(ide_channel_labels[ch]);
                kprint(" ");
                kprint(ide_drive_labels[d]);
                if (type == 1) kprint(", ATAPI");
                kprint(")\n");

                if (type == 0 && partition_scan_count < MAX_IDE_DEVICES) {
                    partition_scan[partition_scan_count++] = bdev;
                }
            }
        }
    }
    
    /* Re-enable interrupts */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        ide_write_ctrl((uint8_t)ch, 0);
    }

    for (size_t i = 0; i < partition_scan_count; i++) {
        blkdev_scan_partitions(partition_scan[i]);
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
