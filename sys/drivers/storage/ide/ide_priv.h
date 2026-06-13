#ifndef _IDE_PRIV_H
#define _IDE_PRIV_H

/*
 * ide_priv.h - Internal shared state and prototypes for the modular
 * ATA/IDE driver.  Definitions live in ide.c; every other module in
 * the ide directory pulls them in as extern declarations.  Nothing
 * here is part
 * of the public driver API (see ide.h for that).
 */

#include <stdint.h>
#include <stddef.h>

#include <drivers/storage/blkdev.h>
#include <drivers/storage/ide/ide.h>

/*
 * ============================================================
 * Shared State (defined in ide.c)
 * ============================================================
 */

/* Block device integration context */
typedef struct {
    uint8_t channel;
    uint8_t drive;
    uint8_t index;
    uint8_t type;  /* 0=ATA, 1=ATAPI */
} ide_drive_ctx_t;

extern ide_channel_t ide_channels[MAX_IDE_CHANNELS];
extern prdt_entry_t ide_prdts[MAX_IDE_CHANNELS][MAX_PRD_ENTRIES];
extern ide_device_t ide_devices[MAX_IDE_DEVICES];
extern int ide_device_count;
extern ide_drive_ctx_t ide_contexts[MAX_IDE_DEVICES];
extern blkdev_t ide_blkdevs[MAX_IDE_DEVICES];
extern int ide_attached;
extern volatile int ide_irq_complete[MAX_IDE_CHANNELS];

extern uint8_t ide_channel_irq_registered[MAX_IDE_CHANNELS];
extern uint8_t ide_channel_irq_shared[MAX_IDE_CHANNELS];

/* Label / default tables */
extern const char *const ide_isa_channel_names[MAX_IDE_CHANNELS];
extern const char *const ide_channel_labels[MAX_IDE_CHANNELS];
extern const char *const ide_drive_labels[2];
extern const uint16_t ide_default_io_bases[MAX_IDE_CHANNELS];
extern const uint16_t ide_default_ctrl_bases[MAX_IDE_CHANNELS];
extern const uint8_t ide_default_irqs[MAX_IDE_CHANNELS];

/*
 * ============================================================
 * Internal Helpers
 * ============================================================
 */

int ide_debug_enabled(void);

/* ide_regs.c */
void ide_bm_write8(uint8_t channel, uint8_t reg, uint8_t data);
uint8_t ide_bm_read8(uint8_t channel, uint8_t reg);
void ide_bm_write32(uint8_t channel, uint8_t reg, uint32_t data);

/* ide_wait.c */
void ide_wait_backoff(int *yield_count);
int ide_can_block_wait(void);
void ide_delay_ms(uint32_t delay_ms);
void ide_400ns(uint8_t channel);
int ide_wait_bsy(uint8_t channel, uint32_t timeout_ms, const char *op);
int ide_wait_drq(uint8_t channel, uint32_t timeout_ms, const char *op);
int ide_wait_ready(uint8_t channel, int timeout_ms, const char *op);
int ide_wait_irq_completion(uint8_t channel, uint32_t timeout_ms,
                            const char *op);

/* ide_cmd.c */
void ide_select_drive(uint8_t channel, uint8_t drive);
int ide_issue_non_data_command(uint8_t channel, uint8_t drive,
                               uint8_t command, const char *op);
int ide_issue_rw(uint8_t channel, uint8_t drive, uint64_t lba,
                 uint16_t count, uint8_t command, int lba48,
                 const char *op);

/* ide_probe.c */
int ide_identify_channel(uint8_t channel, uint8_t drive, void *buffer);
int ide_identify_atapi_channel(uint8_t channel, uint8_t drive, void *buffer);
int ide_program_dma_mode(ide_device_t *dev);
void ide_bm_set_drive_dma_capable(uint8_t channel, uint8_t drive, int enabled);
int ide_software_reset_channel(uint8_t channel);
void ide_refresh_device_slot(uint8_t channel, uint8_t drive);
void ide_mark_offline(ide_drive_ctx_t *ctx, const char *op);
void ide_disable_device_dma(ide_device_t *dev, const char *op);
void ide_register_irqs(void);
int ide_scan_controller(void);

/* ide_probe.c driver structs (registered by ide_init in ide.c) */
struct driver;
extern struct driver ide_isa_driver;
extern struct driver ide_pci_driver;

/* ide.c block-device callbacks (referenced by ide_scan_controller) */
int ide_blkdev_read(blkdev_t *dev, uint64_t sector, uint32_t count,
                    void *buffer);
int ide_blkdev_write(blkdev_t *dev, uint64_t sector, uint32_t count,
                     const void *buffer);

/* ide.c */
int ide_transfer_read_once(ide_drive_ctx_t *ctx, uint64_t sector,
                           uint32_t count, void *buffer);
int ide_transfer_write_once(ide_drive_ctx_t *ctx, uint64_t sector,
                            uint32_t count, const void *buffer);

#endif /* _IDE_PRIV_H */
