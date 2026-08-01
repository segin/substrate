/*
 * ide_probe.c - Device identification, DMA-mode programming, channel
 * reset/recovery, IRQ registration, controller scan, power management,
 * and the device/driver framework glue.
 */

#include <stdio.h>
#include <string.h>

#include <arch/i386/cpu.h>
#include <arch/x86-common/io.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/isa.h>
#include <kern/pci.h>
#include <kern/sched.h>
#include <sys/irq.h>

/*
 * ============================================================
 * Device Slot Refresh / Recovery Helpers
 * ============================================================
 */

void ide_refresh_device_slot(uint8_t channel, uint8_t drive) {
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

        /*
         * IDE-06: validate what the device reports.  blk_size was stored
         * verbatim, so a drive answering 0 (or a parse error yielding 0)
         * published sector_size 0 to the block layer, which divides by it in
         * every geometry calculation.  An absurd value is equally a lie.
         * Same class as SCSI-09; fall back to the ATAPI default rather than
         * trusting it.
         */
        if (ide_atapi_read_capacity(channel, drive, &lba, &blk_size) == 0 &&
            blk_size >= 512U && blk_size <= 65536U &&
            (blk_size & (blk_size - 1U)) == 0U) {
            total_sectors = (uint64_t)lba + 1;
            sector_size = blk_size;
        } else {
            sector_size = ATAPI_DEFAULT_SECTOR_SIZE;
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

void ide_mark_offline(ide_drive_ctx_t *ctx, const char *op) {
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

void ide_disable_device_dma(ide_device_t *dev, const char *op) {
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

/*
 * ============================================================
 * Legacy / PCI Channel Configuration Helpers
 * ============================================================
 */

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

/*
 * ============================================================
 * Device Identification (channel-indexed, internal)
 * ============================================================
 */

int ide_identify_channel(uint8_t channel, uint8_t drive, void *buffer) {
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

int ide_identify_atapi_channel(uint8_t channel, uint8_t drive, void *buffer) {
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

/*
 * ============================================================
 * Channel Reset and DMA Mode Programming
 * ============================================================
 */

int ide_software_reset_channel(uint8_t channel) {
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

void ide_bm_set_drive_dma_capable(uint8_t channel, uint8_t drive, int enabled) {
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

int ide_program_dma_mode(ide_device_t *dev) {
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
    if (ide_wait_ready_ex(dev->channel, IDE_TIMEOUT_READY_MS, "set-features", 0) < 0) {
        dev->dma_mode = 0;   /* [IDE-10] never programmed: don't claim a mode */
        return -1;
    }

    ide_write_reg(dev->channel, ATA_REG_FEATURES, ATA_FEAT_SET_TRANSFER_MODE);
    ide_write_reg(dev->channel, ATA_REG_SEC_COUNT, mode);
    ide_write_reg(dev->channel, ATA_REG_COMMAND, ATA_CMD_SET_FEATURES);

    if (ide_wait_bsy(dev->channel, IDE_TIMEOUT_READY_MS, "set-features") < 0) {
        dev->dma_mode = 0;   /* [IDE-10] */
        ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
        return -1;
    }

    status = ide_read_reg(dev->channel, ATA_REG_STATUS);
    if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
        dev->dma_mode = 0;   /* [IDE-10] SET FEATURES was rejected */
        ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 0);
        return -1;
    }

    ide_bm_set_drive_dma_capable(dev->channel, dev->drive, 1);
    dev->dma_mode = (mode >= ATA_XFER_MODE_UDMA_BASE) ? 1 : 2;
    return 0;
}

/*
 * ============================================================
 * IRQ Handling / Registration
 * ============================================================
 */

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

void ide_register_irqs(void) {
    char name[16];

    for (uint8_t channel = 0; channel < MAX_IDE_CHANNELS; channel++) {
        unsigned long flags = 0;

        /*
         * [IDE-04] This used to require dma_capable, which NOTHING ever sets
         * -- ide_dma_init()/ide_dma_init_pair() have no callers anywhere in
         * sys/ -- so no handler was ever registered for IRQ 14/15.  Probe
         * then cleared nIEN at the end (see below), leaving device
         * interrupts ENABLED with nothing to service them: every PIO
         * completion raised an unhandled interrupt, and on a shared
         * level-triggered PCI line that is an IRQ storm (the same failure
         * mode as the reverted AHCI/NIC shared-INTx work).
         *
         * Registration no longer depends on DMA.  A channel needs a usable
         * IRQ line and a device present; whether it goes on to do DMA is a
         * separate question.
         */
        if (ide_channel_irq_registered[channel]) {
            continue;
        }
        if (ide_channels[channel].io_base == 0 ||
            ide_channels[channel].irq == 0 ||
            ide_channels[channel].irq == 0xFF) {
            continue;
        }

        /*
         * Share unless this is a legacy ISA line.
         *
         * ide_channel_irq_shared[] is set by ide_pci_apply_channel() when a
         * channel takes its IRQ from PCI, but a channel can reach here with
         * a PCI-routed line by other paths and then claim it EXCLUSIVELY --
         * which locks every other device on that line out of request_irq()
         * with -EBUSY.  Observed: "ide: channel 2 IRQ 11 handler installed"
         * took IRQ 11 first and the e1000 on the same line got no handler,
         * so the interface never registered.
         *
         * Only IRQ 14 and 15 are the legacy ATA lines, which are ISA-style
         * edge-triggered and genuinely unshareable.  Anything else came from
         * PCI interrupt routing and is level-triggered and shared by design.
         */
        if (ide_channel_irq_shared[channel] ||
            (ide_channels[channel].irq != 14 && ide_channels[channel].irq != 15)) {
            flags |= IRQF_SHARED;
        }

        snprintf(name, sizeof(name), "ide%u", (unsigned int)channel);
        if (request_irq(ide_channels[channel].irq, ide_irq_dispatch, flags,
                        name, &ide_channels[channel]) == 0) {
            ide_channel_irq_registered[channel] = 1;
            kprintf("ide: channel %u IRQ %u handler installed%s\n",
                    (unsigned)channel, (unsigned)ide_channels[channel].irq,
                    (flags & IRQF_SHARED) ? " (shared)" : "");
        } else {
            /* [IDE-04] Say so: the channel will be left with nIEN set and
             * driven purely by polling, which is a materially different
             * operating mode and used to be invisible. */
            kprintf("ide: channel %u could not claim IRQ %u; "
                    "interrupts stay masked (polled)\n",
                    (unsigned)channel, (unsigned)ide_channels[channel].irq);
        }
    }

    /*
     * [IDE-04] Enable bus-master DMA, now that a handler exists to complete
     * it.
     *
     * dma_capable was never set by anything -- ide_dma_init() and
     * ide_dma_init_pair() have no callers -- so ide_dma_read()/write()
     * returned -1 at their first line and every transfer went through PIO,
     * even though ide_pci_configure_channels() had already found the
     * bus-master base and set PCI_COMMAND_MASTER on the controller.
     *
     * Two conditions, both necessary:
     *   - a bus-master I/O base from PCI BAR 4, which is what the PRDT and
     *     the BM command/status registers are addressed through;
     *   - an installed IRQ handler, because ide_dma_read()/write() wait on
     *     ide_wait_irq_completion().  On a polled channel that wait can only
     *     ever time out, so leaving DMA off there is not a limitation, it is
     *     the only correct choice.
     *
     * A device that then fails a DMA transfer is demoted to PIO by
     * ide_disable_device_dma(); a transfer this driver's PRDT simply cannot
     * describe falls back to PIO for that request alone (IDE_DMA_UNSUPPORTED,
     * see IDE-16) without condemning the drive.
     */
    for (uint8_t channel = 0; channel < MAX_IDE_CHANNELS; channel++) {
        if (ide_channels[channel].bm_base == 0)
            continue;
        if (!ide_channel_irq_registered[channel])
            continue;
        if (ide_channels[channel].dma_capable)
            continue;

        ide_channels[channel].dma_capable = 1;
        kprintf("ide: channel %u bus-master DMA enabled (bm_base 0x%x)\n",
                (unsigned)channel, (unsigned)ide_channels[channel].bm_base);
    }
}

/*
 * ============================================================
 * Public Identification API (I/O-base indexed)
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
 * Power Management
 * ============================================================
 */

int ide_standby_immediate(uint16_t bus, uint8_t drive) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    return ide_issue_non_data_command(channel, drive,
                                      ATA_CMD_STANDBY_IMMEDIATE,
                                      "standby-immediate");
}

int ide_idle_immediate(uint16_t bus, uint8_t drive) {
    uint8_t channel;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    return ide_issue_non_data_command(channel, drive,
                                      ATA_CMD_IDLE_IMMEDIATE,
                                      "idle-immediate");
}

int ide_check_power_mode(uint16_t bus, uint8_t drive, uint8_t *mode) {
    uint8_t channel;
    uint8_t status;

    if (mode == NULL) {
        return -1;
    }

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    ide_select_drive(channel, drive);
    status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return -1;
    }

    if (ide_wait_ready_ex(channel, IDE_TIMEOUT_READY_MS, "check-power-mode", 0) < 0) {
        return -1;
    }

    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_CHECK_POWER_MODE);
    if (ide_wait_bsy(channel, IDE_TIMEOUT_READY_MS, "check-power-mode") < 0) {
        return -1;
    }

    status = ide_read_reg(channel, ATA_REG_STATUS);
    if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
        return -1;
    }

    *mode = ide_read_reg(channel, ATA_REG_SEC_COUNT);
    return 0;
}

int ide_configure_spindown_timer(uint16_t bus, uint8_t drive, uint8_t timer_code) {
    uint8_t channel;
    uint8_t status;

    if (ide_channel_index_from_io(bus, &channel) < 0) {
        return -1;
    }

    ide_select_drive(channel, drive);
    status = ide_read_reg(channel, ATA_REG_STATUS);
    if (status == 0 || status == 0xFF) {
        return -1;
    }

    if (ide_wait_ready_ex(channel, IDE_TIMEOUT_READY_MS, "standby-timer", 0) < 0) {
        return -1;
    }

    ide_write_reg(channel, ATA_REG_SEC_COUNT, timer_code);
    ide_write_reg(channel, ATA_REG_COMMAND, ATA_CMD_STANDBY);
    if (ide_wait_bsy(channel, IDE_TIMEOUT_READY_MS, "standby-timer") < 0) {
        return -1;
    }

    status = ide_read_reg(channel, ATA_REG_STATUS);
    if ((status & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
        return -1;
    }

    return 0;
}

/*
 * ============================================================
 * Controller Scan
 * ============================================================
 */

int ide_scan_controller(void) {
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
        mutex_init(&ide_channels[ch].lock, "ide_chan");
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
                    /* IDE-06: same validation as the refresh path above --
                     * a reported block size of 0 publishes sector_size 0 to
                     * the block layer, which divides by it. */
                    if (ide_atapi_read_capacity(ch, d, &lba, &blk_size) == 0 &&
                        blk_size >= 512U && blk_size <= 65536U &&
                        (blk_size & (blk_size - 1U)) == 0U) {
                        total_sectors = (uint64_t)lba + 1;
                        sector_size = blk_size;
                    } else {
                        /* Default for CD-ROM if no media or a bogus report */
                        sector_size = ATAPI_DEFAULT_SECTOR_SIZE;
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

                /*
                 * ATAPI (optical) media carries no MBR/GPT/BSD label -- it
                 * is ISO9660 -- so register it without a partition scan.
                 * blkdev_register_disk() would scan, which is both pointless
                 * and, before the sniffers were bounded, fatal: a 2048-byte
                 * sector read overran their 512-byte stack buffers.  ATA
                 * disks are scanned below via partition_scan[].
                 */
                if (type == 1) {
                    blkdev_register(bdev);
                } else {
                    blkdev_register_disk(bdev);
                }
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

    /*
     * [IDE-04] Unmask device interrupts ONLY on channels whose handler was
     * actually installed.  Clearing nIEN unconditionally is what turned an
     * unregistered IRQ into a storm; a channel with no handler stays masked
     * and is driven purely by polling, which is what the PIO paths already
     * do.
     */
    for (int ch = 0; ch < MAX_IDE_CHANNELS; ch++) {
        if (ide_channel_irq_registered[ch]) {
            ide_write_ctrl((uint8_t)ch, 0);
        } else {
            ide_write_ctrl((uint8_t)ch, ATA_CTRL_NIEN);
        }
    }

    for (size_t i = 0; i < partition_scan_count; i++) {
        blkdev_scan_partitions(partition_scan[i]);
    }

    ide_attached = 1;
    return 0;
}

/*
 * ============================================================
 * Device / Driver Framework Glue
 * ============================================================
 */

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

struct driver ide_isa_driver = {
    .name = "ide-isa",
    .match_func = ide_isa_match,
    .attach = ide_attach_via_framework,
};

struct driver ide_pci_driver = {
    .name = "ide-pci",
    .id_table = ide_pci_ids,
    .attach = ide_attach_via_framework,
};
