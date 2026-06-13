/*
 * ide_wait.c - Drive readiness / completion wait helpers.
 */

#include <stdio.h>

#include <drivers/storage/ide/ide.h>
#include <drivers/storage/ide/ide_priv.h>

#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>

/*
 * Decide whether the calling context may block (yield / sleep) while
 * polling, or must busy-wait.  Early boot and PID<=1 kernel tasks still
 * exercise fragile context-switch paths, so keep polling local there.
 */
int ide_can_block_wait(void) {
    if (!current_thread || !current_process) {
        return 0;
    }

    if (current_process->is_kernel_task && current_process->pid <= 1) {
        return 0;
    }

    return 1;
}

void ide_wait_backoff(int *yield_count) {
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

void ide_delay_ms(uint32_t delay_ms) {
    uint64_t start = get_uptime_ms();
    int yield_count = 0;

    while ((uint64_t)(get_uptime_ms() - start) < delay_ms) {
        ide_wait_backoff(&yield_count);
    }
}

/* 400ns settle delay: read the alternate status register four times. */
void ide_400ns(uint8_t channel) {
    for (int i = 0; i < 4; i++) {
        ide_read_ctrl(channel);
    }
}

/* Wait for BSY to clear. */
int ide_wait_bsy(uint8_t channel, uint32_t timeout_ms, const char *op) {
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

/* Wait for DRQ to assert. */
int ide_wait_drq(uint8_t channel, uint32_t timeout_ms, const char *op) {
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

/*
 * Wait for the drive to become ready to accept a command: BSY clear AND
 * DRDY set.  Returns 0 on success, -1 on timeout or on ERR/DF.
 *
 * Requiring DRDY (not just !BSY) is the correctness fix for back-to-back
 * write bursts: a freshly-completed write leaves the drive momentarily
 * !BSY but not yet DRDY, and issuing the next command there meant DRQ
 * never came and the per-sector wait burned its full timeout.
 */
int ide_wait_ready(uint8_t channel, int timeout_ms, const char *op) {
    uint64_t start_ms = get_uptime_ms();
    int spins = 0;
    int yield_count = 0;

    /* 400ns settle delay */
    ide_400ns(channel);

    for (;;) {
        uint8_t status = ide_read_reg(channel, ATA_REG_STATUS);

        if (status & (ATA_SR_ERR | ATA_SR_DF)) {
            uint8_t error = ide_read_reg(channel, ATA_REG_ERROR);
            char decoded[64];

            ide_decode_error(error, decoded, sizeof(decoded));
            kprintf("ide: %s ready failed status=%02x error=%02x (%s)\n",
                    op ? op : "command", status, error, decoded);
            return -1;
        }

        if ((status & ATA_SR_BSY) == 0 && (status & ATA_SR_DRDY) != 0) {
            return 0;
        }

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
}

int ide_wait_irq_completion(uint8_t channel, uint32_t timeout_ms,
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
