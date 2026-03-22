#include <drivers/storage/floppy/floppy.h>

#include <arch/x86-common/io.h>
#include <drivers/storage/blkdev.h>
#include <kern/console.h>
#include <kern/isa.h>
#include <kern/resource.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <rtc.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <sys/lock.h>
#include <sys/param.h>
#include <vm/phys_mem.h>
#include <vm/vm_page.h>

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FDC_DMA_ADDR_PORT 0x04
#define FDC_DMA_COUNT_PORT 0x05
#define FDC_DMA_PAGE_PORT 0x81
#define FDC_DMA_MASK_PORT 0x0A
#define FDC_DMA_MODE_PORT 0x0B
#define FDC_DMA_FF_PORT 0x0C

#define FDC_DMA_MODE_READ 0x46
#define FDC_DMA_MODE_WRITE 0x4A

#define FDC_TIMEOUT_MS 1000U
#define FDC_MOTOR_SPINUP_MS 400U
#define FDC_RETRY_LIMIT 3
#define FDC_POLL_SPINS_PER_MS 100U

typedef struct fdc_controller {
    uint16_t base;
    uint8_t irq;
    uint8_t index;
    uint8_t present;
    uint8_t dor_shadow;
    volatile int irq_seen;
    spinlock_t lock;
    struct resource *io_res;
    vm_page_t *dma_page;
    uintptr_t dma_phys;
    uint8_t *dma_buf;
} fdc_controller_t;

typedef struct fdc_drive {
    uint8_t present;
    uint8_t controller_index;
    uint8_t drive_index;
    uint8_t current_cylinder;
    uint8_t motor_on;
    const fdc_geometry_t *geom;
    blkdev_t bdev;
} fdc_drive_t;

static fdc_controller_t fdc_controllers[FDC_MAX_CONTROLLERS] = {
    { .base = FDC_PRIMARY_BASE, .irq = FDC_PRIMARY_IRQ, .index = 0 },
    { .base = FDC_SECONDARY_BASE, .irq = FDC_SECONDARY_IRQ, .index = 1 },
};
static fdc_drive_t fdc_drives[FDC_MAX_DRIVES];
static int floppy_initialized;

static inline uint16_t fdc_reg(const fdc_controller_t *ctlr, uint8_t offset) {
    return (uint16_t)(ctlr->base + offset);
}

static uint32_t fdc_spin_budget(uint32_t timeout_ms) {
    uint32_t spins = timeout_ms * FDC_POLL_SPINS_PER_MS;

    if (spins < FDC_POLL_SPINS_PER_MS) {
        spins = FDC_POLL_SPINS_PER_MS;
    }
    return spins;
}

static int fdc_command_result_ready(const fdc_controller_t *ctlr) {
    uint8_t msr = inb(fdc_reg(ctlr, FDC_MSR_OFFSET));
    return (msr & (FDC_MSR_RQM | FDC_MSR_DIO)) == (FDC_MSR_RQM | FDC_MSR_DIO);
}

static int fdc_delay_ms(uint32_t delay_ms) {
    uint32_t spins = fdc_spin_budget(delay_ms);

    while (spins-- > 0) {
        __asm__ volatile("pause");
    }
    return 0;
}

static int fdc_wait_fifo_ready(const fdc_controller_t *ctlr, int want_read, uint32_t timeout_ms) {
    uint64_t deadline = (uint64_t)get_uptime_ms() + timeout_ms;
    uint32_t spins = fdc_spin_budget(timeout_ms);

    for (;;) {
        uint8_t msr = inb(fdc_reg(ctlr, FDC_MSR_OFFSET));

        if ((msr & FDC_MSR_RQM) != 0) {
            if (want_read) {
                if ((msr & FDC_MSR_DIO) != 0) {
                    return 0;
                }
            } else if ((msr & FDC_MSR_DIO) == 0) {
                return 0;
            }
        }

        if ((uint64_t)get_uptime_ms() >= deadline || spins-- == 0) {
            return -ETIMEDOUT;
        }

        __asm__ volatile("pause");
    }
}

static int fdc_write_fifo(const fdc_controller_t *ctlr, uint8_t value) {
    int ret = fdc_wait_fifo_ready(ctlr, 0, FDC_TIMEOUT_MS);
    if (ret < 0) {
        return ret;
    }
    outb(fdc_reg(ctlr, FDC_FIFO_OFFSET), value);
    return 0;
}

static int fdc_read_fifo(const fdc_controller_t *ctlr, uint8_t *value) {
    int ret;

    if (value == NULL) {
        return -EINVAL;
    }
    ret = fdc_wait_fifo_ready(ctlr, 1, FDC_TIMEOUT_MS);
    if (ret < 0) {
        return ret;
    }
    *value = inb(fdc_reg(ctlr, FDC_FIFO_OFFSET));
    return 0;
}

static int fdc_wait_irq(fdc_controller_t *ctlr, uint32_t timeout_ms) {
    uint64_t deadline = (uint64_t)get_uptime_ms() + timeout_ms;
    uint32_t spins = fdc_spin_budget(timeout_ms);

    while (!ctlr->irq_seen) {
        if (fdc_command_result_ready(ctlr)) {
            return 0;
        }
        if ((uint64_t)get_uptime_ms() >= deadline || spins-- == 0) {
            return fdc_command_result_ready(ctlr) ? 0 : -ETIMEDOUT;
        }
        __asm__ volatile("pause");
    }

    ctlr->irq_seen = 0;
    return 0;
}

static int fdc_sense_interrupt(fdc_controller_t *ctlr, uint8_t *st0, uint8_t *cyl) {
    int ret;

    ret = fdc_write_fifo(ctlr, FDC_CMD_SENSE_INTERRUPT);
    if (ret < 0) {
        return ret;
    }
    ret = fdc_read_fifo(ctlr, st0);
    if (ret < 0) {
        return ret;
    }
    return fdc_read_fifo(ctlr, cyl);
}

static void fdc_update_dor(fdc_controller_t *ctlr, uint8_t drive_select) {
    outb(fdc_reg(ctlr, FDC_DOR_OFFSET),
         (uint8_t)((ctlr->dor_shadow & (uint8_t)~FDC_DOR_DRIVE_MASK) |
                   (drive_select & FDC_DOR_DRIVE_MASK)));
}

static void fdc_motor_set(fdc_drive_t *drive, int on) {
    fdc_controller_t *ctlr = &fdc_controllers[drive->controller_index];
    uint8_t motor_bit = (uint8_t)(FDC_DOR_MOTOR0 << drive->drive_index);

    if (on) {
        if ((ctlr->dor_shadow & motor_bit) == 0) {
            ctlr->dor_shadow |= motor_bit;
            drive->motor_on = 1;
            fdc_update_dor(ctlr, drive->drive_index);
            (void)fdc_delay_ms(FDC_MOTOR_SPINUP_MS);
        }
    } else if ((ctlr->dor_shadow & motor_bit) != 0) {
        ctlr->dor_shadow &= (uint8_t)~motor_bit;
        drive->motor_on = 0;
        fdc_update_dor(ctlr, drive->drive_index);
    }
}

static void fdc_set_rate(fdc_controller_t *ctlr, uint8_t rate) {
    outb(fdc_reg(ctlr, FDC_CCR_OFFSET), (uint8_t)(rate & 0x03));
}

static int fdc_configure_controller(fdc_controller_t *ctlr) {
    int ret;

    ret = fdc_write_fifo(ctlr, FDC_CMD_CONFIGURE);
    if (ret < 0) {
        return ret;
    }
    ret = fdc_write_fifo(ctlr, 0x00);
    if (ret < 0) {
        return ret;
    }
    ret = fdc_write_fifo(ctlr, 0x57);
    if (ret < 0) {
        return ret;
    }
    return fdc_write_fifo(ctlr, 0x00);
}

static int fdc_specify_controller(fdc_controller_t *ctlr) {
    int ret;

    ret = fdc_write_fifo(ctlr, FDC_CMD_SPECIFY);
    if (ret < 0) {
        return ret;
    }
    ret = fdc_write_fifo(ctlr, 0xDF);
    if (ret < 0) {
        return ret;
    }
    return fdc_write_fifo(ctlr, 0x02);
}

static int fdc_reset_controller(fdc_controller_t *ctlr) {
    uint8_t st0;
    uint8_t cyl;
    int ret;
    int i;

    ctlr->irq_seen = 0;
    ctlr->dor_shadow = FDC_DOR_DMA_IRQ;
    outb(fdc_reg(ctlr, FDC_DOR_OFFSET), ctlr->dor_shadow);
    io_wait();
    ctlr->dor_shadow = FDC_DOR_DMA_IRQ | FDC_DOR_RESET;
    outb(fdc_reg(ctlr, FDC_DOR_OFFSET), ctlr->dor_shadow);

    /*
     * Treat controller reset as a pure settle delay instead of requiring the
     * reset IRQ path. On QEMU's ISA FDC this is enough to bring the controller
     * to command-accepting state, while waiting for IRQ6 here can wedge
     * indefinitely.
     */
    fdc_delay_ms(20);
    for (i = 0; i < FDC_MAX_DRIVES; i++) {
        ret = fdc_sense_interrupt(ctlr, &st0, &cyl);
        if (ret < 0) {
            kprintf("fdc: controller %u sense interrupt %d failed during reset\n",
                    ctlr->index, i);
            break;
        }
    }

    ret = fdc_configure_controller(ctlr);
    if (ret < 0) {
        kprintf("fdc: controller %u configure failed\n", ctlr->index);
        return ret;
    }
    ret = fdc_specify_controller(ctlr);
    if (ret < 0) {
        kprintf("fdc: controller %u specify failed\n", ctlr->index);
        return ret;
    }
    fdc_set_rate(ctlr, 0);
    return 0;
}

static int fdc_program_dma(fdc_controller_t *ctlr, int write_to_drive, size_t len) {
    uintptr_t phys = ctlr->dma_phys;
    uint16_t count;
    uint8_t mode;

    if (ctlr->dma_buf == NULL || !fdc_dma_window_valid(phys, len)) {
        return -EINVAL;
    }

    count = (uint16_t)(len - 1U);
    mode = write_to_drive ? FDC_DMA_MODE_WRITE : FDC_DMA_MODE_READ;

    outb(FDC_DMA_MASK_PORT, (uint8_t)(0x04 | FDC_DMA_CHANNEL));
    outb(FDC_DMA_FF_PORT, 0x00);
    outb(FDC_DMA_ADDR_PORT, (uint8_t)(phys & 0xFF));
    outb(FDC_DMA_ADDR_PORT, (uint8_t)((phys >> 8) & 0xFF));
    outb(FDC_DMA_PAGE_PORT, (uint8_t)((phys >> 16) & 0xFF));
    outb(FDC_DMA_FF_PORT, 0x00);
    outb(FDC_DMA_COUNT_PORT, (uint8_t)(count & 0xFF));
    outb(FDC_DMA_COUNT_PORT, (uint8_t)((count >> 8) & 0xFF));
    outb(FDC_DMA_MODE_PORT, mode);
    outb(FDC_DMA_MASK_PORT, FDC_DMA_CHANNEL);
    return 0;
}

static int fdc_seek_drive(fdc_drive_t *drive, uint8_t cylinder) {
    fdc_controller_t *ctlr = &fdc_controllers[drive->controller_index];
    uint8_t st0;
    uint8_t sensed_cyl;
    int ret;

    if (drive->current_cylinder == cylinder) {
        return 0;
    }

    ret = fdc_write_fifo(ctlr, FDC_CMD_SEEK);
    if (ret < 0) {
        return ret;
    }
    ret = fdc_write_fifo(ctlr, (uint8_t)(drive->drive_index & 0x03));
    if (ret < 0) {
        return ret;
    }
    ret = fdc_write_fifo(ctlr, cylinder);
    if (ret < 0) {
        return ret;
    }

    ret = fdc_wait_irq(ctlr, FDC_TIMEOUT_MS);
    if (ret < 0) {
        fdc_delay_ms(10);
    }
    ret = fdc_sense_interrupt(ctlr, &st0, &sensed_cyl);
    if (ret < 0) {
        return ret;
    }
    if ((st0 & FDC_ST0_IC_MASK) != FDC_ST0_IC_NORMAL || sensed_cyl != cylinder) {
        return -EIO;
    }

    drive->current_cylinder = cylinder;
    return 0;
}

static int fdc_recalibrate_drive(fdc_drive_t *drive) {
    fdc_controller_t *ctlr = &fdc_controllers[drive->controller_index];
    uint8_t st0;
    uint8_t sensed_cyl;
    int ret;

    ret = fdc_write_fifo(ctlr, FDC_CMD_RECALIBRATE);
    if (ret < 0) {
        return ret;
    }
    ret = fdc_write_fifo(ctlr, (uint8_t)(drive->drive_index & 0x03));
    if (ret < 0) {
        return ret;
    }

    ret = fdc_wait_irq(ctlr, FDC_TIMEOUT_MS);
    if (ret < 0) {
        fdc_delay_ms(10);
    }
    ret = fdc_sense_interrupt(ctlr, &st0, &sensed_cyl);
    if (ret < 0) {
        return ret;
    }
    if ((st0 & FDC_ST0_IC_MASK) != FDC_ST0_IC_NORMAL) {
        return -EIO;
    }

    drive->current_cylinder = 0;
    return sensed_cyl == 0 ? 0 : -EIO;
}

static const char *fdc_decode_st1(uint8_t st1, char *buf, size_t size) {
    size_t off = 0;

    if (st1 == 0) {
        (void)snprintf(buf, size, "none");
        return buf;
    }
    if ((st1 & 0x01) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "MAM ");
    if ((st1 & 0x02) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "WP ");
    if ((st1 & 0x04) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "ND ");
    if ((st1 & 0x10) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "OR ");
    if ((st1 & 0x20) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "CRC ");
    if ((st1 & 0x80) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "EOC ");
    return buf;
}

static const char *fdc_decode_st2(uint8_t st2, char *buf, size_t size) {
    size_t off = 0;

    if (st2 == 0) {
        (void)snprintf(buf, size, "none");
        return buf;
    }
    if ((st2 & 0x01) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "MDAM ");
    if ((st2 & 0x02) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "BCYL ");
    if ((st2 & 0x10) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "WCYL ");
    if ((st2 & 0x20) != 0) off += (size_t)snprintf(buf + off, off < size ? size - off : 0, "CRC ");
    return buf;
}

static int fdc_transfer_sector(fdc_drive_t *drive, uint32_t lba, uint8_t *buffer,
                               int write_to_drive) {
    fdc_controller_t *ctlr = &fdc_controllers[drive->controller_index];
    fdc_chs_t chs;
    uint8_t result[7];
    uint8_t command;
    uint8_t gap3;
    int ret;
    int attempt;

    if (drive->geom == NULL || buffer == NULL) {
        return -EINVAL;
    }
    if (fdc_lba_to_chs(drive->geom, lba, &chs) < 0) {
        return -EINVAL;
    }

    command = write_to_drive ? FDC_CMD_WRITE_DATA : FDC_CMD_READ_DATA;
    gap3 = drive->geom->data_rate == 0 ? FDC_GAP3_HD : FDC_GAP3_DD;

    spinlock_acquire(&ctlr->lock);
    for (attempt = 0; attempt < FDC_RETRY_LIMIT; attempt++) {
        fdc_motor_set(drive, 1);
        fdc_set_rate(ctlr, drive->geom->data_rate);

        if (attempt == 0) {
            ret = fdc_seek_drive(drive, (uint8_t)chs.cylinder);
        } else {
            ret = fdc_recalibrate_drive(drive);
            if (ret == 0) {
                ret = fdc_seek_drive(drive, (uint8_t)chs.cylinder);
            }
        }
        if (ret < 0) {
            continue;
        }

        if (write_to_drive) {
            memcpy(ctlr->dma_buf, buffer, 512);
        }

        ret = fdc_program_dma(ctlr, write_to_drive, 512);
        if (ret < 0) {
            continue;
        }

        ctlr->irq_seen = 0;
        ret = fdc_write_fifo(ctlr, command);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, (uint8_t)((chs.head << 2) | (drive->drive_index & 0x03)));
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, (uint8_t)chs.cylinder);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, chs.head);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, chs.sector);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, FDC_SECTOR_SHIFT_512);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, drive->geom->sectors_per_track);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, gap3);
        if (ret < 0) {
            continue;
        }
        ret = fdc_write_fifo(ctlr, 0xFF);
        if (ret < 0) {
            continue;
        }

        ret = fdc_wait_irq(ctlr, FDC_TIMEOUT_MS);
        if (ret < 0) {
            continue;
        }

        for (ret = 0; ret < 7; ret++) {
            if (fdc_read_fifo(ctlr, &result[ret]) < 0) {
                ret = -EIO;
                break;
            }
        }
        if (ret < 0) {
            continue;
        }

        if ((result[0] & FDC_ST0_IC_MASK) == FDC_ST0_IC_NORMAL && result[1] == 0 && result[2] == 0) {
            if (!write_to_drive) {
                memcpy(buffer, ctlr->dma_buf, 512);
            }
            fdc_motor_set(drive, 0);
            spinlock_release(&ctlr->lock);
            return 0;
        }

        {
            char st1[32];
            char st2[32];
            kprintf("fdc: fd%u %s failed st0=%02x st1=%02x (%s) st2=%02x (%s) c=%u h=%u s=%u\n",
                    drive->drive_index,
                    write_to_drive ? "write" : "read",
                    result[0], result[1], fdc_decode_st1(result[1], st1, sizeof(st1)),
                    result[2], fdc_decode_st2(result[2], st2, sizeof(st2)),
                    chs.cylinder, chs.head, chs.sector);
        }
    }
    fdc_motor_set(drive, 0);
    spinlock_release(&ctlr->lock);
    return -EIO;
}

static int fdc_irq_handler(unsigned int irq, void *dev_id, void *frame) {
    fdc_controller_t *ctlr = (fdc_controller_t *)dev_id;
    (void)frame;

    if (ctlr == NULL || ctlr->irq != irq) {
        return 0;
    }
    ctlr->irq_seen = 1;
    sched_wakeup((void *)&ctlr->irq_seen);
    return 1;
}

static int floppy_read(blkdev_t *dev, uint64_t sector, uint32_t count, void *buffer) {
    fdc_drive_t *drive = (fdc_drive_t *)dev->priv;
    uint8_t *cursor = (uint8_t *)buffer;
    uint32_t i;

    if (drive == NULL || !drive->present || cursor == NULL) {
        return -EINVAL;
    }
    if (sector + count > dev->total_sectors) {
        return -EINVAL;
    }

    for (i = 0; i < count; i++) {
        if (fdc_transfer_sector(drive, (uint32_t)(sector + i), cursor + (i * 512U), 0) < 0) {
            return -EIO;
        }
    }
    return 0;
}

static int floppy_write(blkdev_t *dev, uint64_t sector, uint32_t count, const void *buffer) {
    fdc_drive_t *drive = (fdc_drive_t *)dev->priv;
    const uint8_t *cursor = (const uint8_t *)buffer;
    uint32_t i;

    if (drive == NULL || !drive->present || cursor == NULL) {
        return -EINVAL;
    }
    if (sector + count > dev->total_sectors) {
        return -EINVAL;
    }

    for (i = 0; i < count; i++) {
        if (fdc_transfer_sector(drive, (uint32_t)(sector + i), (uint8_t *)(uintptr_t)(cursor + (i * 512U)), 1) < 0) {
            return -EIO;
        }
    }
    return 0;
}

static int fdc_controller_dma_init(fdc_controller_t *ctlr) {
    vm_page_t *page;

    page = vm_phys_alloc_page_below(FDC_DMA_LIMIT);
    if (page == NULL) {
        return -ENOMEM;
    }
    ctlr->dma_page = page;
    ctlr->dma_phys = vm_page_to_phys(page);
    ctlr->dma_buf = (uint8_t *)(uintptr_t)(ctlr->dma_phys + KERN_BASE);
    memset(ctlr->dma_buf, 0, 4096);
    return fdc_dma_window_valid(ctlr->dma_phys, 4096) ? 0 : -EINVAL;
}

static void fdc_register_drive(fdc_drive_t *drive) {
    uint64_t total_sectors;

    memset(&drive->bdev, 0, sizeof(drive->bdev));
    snprintf(drive->bdev.name, sizeof(drive->bdev.name), "fd%u", drive->drive_index);
    total_sectors = (uint64_t)drive->geom->cylinders * drive->geom->heads * drive->geom->sectors_per_track;
    drive->bdev.sector_size = 512;
    drive->bdev.total_sectors = total_sectors;
    drive->bdev.priv = drive;
    drive->bdev.read = floppy_read;
    drive->bdev.write = floppy_write;
    blkdev_register_disk(&drive->bdev);
    kprintf("fdc: %s %s registered (%llu sectors)\n",
            drive->bdev.name,
            drive->geom->name,
            (unsigned long long)total_sectors);
}

static int fdc_controller_present(const fdc_controller_t *ctlr, const char *isa_name) {
    if (!isa_device_present(isa_name)) {
        return 0;
    }
    return inb(fdc_reg(ctlr, FDC_MSR_OFFSET)) != 0xFF;
}

static uint8_t fdc_read_cmos_drive_reg(void) {
    outb(CMOS_ADDRESS, 0x10);
    return inb(CMOS_DATA);
}

void floppy_init(void) {
    uint8_t drive_types[2] = {0, 0};
    uint8_t cmos_reg;
    size_t i;

    if (floppy_initialized) {
        return;
    }
    floppy_initialized = 1;

    cmos_reg = fdc_read_cmos_drive_reg();
    fdc_parse_cmos_drive_types(cmos_reg, drive_types);

    for (i = 0; i < 2; i++) {
        fdc_controller_t *ctlr = &fdc_controllers[i];
        const char *isa_name = (i == 0) ? "floppy-primary" : "floppy-secondary";

        if (!fdc_controller_present(ctlr, isa_name)) {
            continue;
        }

        ctlr->io_res = request_region(ctlr->base, 8, i == 0 ? "fdc0" : "fdc1");
        if (ctlr->io_res == NULL) {
            kprintf("fdc: io region 0x%x busy\n", ctlr->base);
            continue;
        }
        spinlock_init(&ctlr->lock, i == 0 ? "fdc0" : "fdc1");
        ctlr->dor_shadow = FDC_DOR_RESET | FDC_DOR_DMA_IRQ;
        ctlr->present = 1;

        if (request_irq(ctlr->irq, fdc_irq_handler, 0, i == 0 ? "fdc0" : "fdc1", ctlr) != 0) {
            kprintf("fdc: irq %u busy\n", ctlr->irq);
            ctlr->present = 0;
            release_region(ctlr->base, 8);
            ctlr->io_res = NULL;
            continue;
        }

        if (fdc_controller_dma_init(ctlr) < 0) {
            kprintf("fdc: controller %u dma init failed\n", ctlr->index);
            ctlr->present = 0;
            free_irq(ctlr->irq, ctlr);
            release_region(ctlr->base, 8);
            ctlr->io_res = NULL;
            continue;
        }
        if (fdc_reset_controller(ctlr) < 0) {
            kprintf("fdc: controller %u init failed\n", ctlr->index);
            ctlr->present = 0;
            free_irq(ctlr->irq, ctlr);
            release_region(ctlr->base, 8);
            ctlr->io_res = NULL;
            continue;
        }
    }

    for (i = 0; i < 2; i++) {
        fdc_drive_t *drive = &fdc_drives[i];
        const fdc_geometry_t *geom = fdc_geometry_from_cmos(drive_types[i]);

        if (geom == NULL || !fdc_controllers[0].present) {
            continue;
        }

        memset(drive, 0, sizeof(*drive));
        drive->present = 1;
        drive->controller_index = 0;
        drive->drive_index = (uint8_t)i;
        drive->current_cylinder = 0xFF;
        drive->geom = geom;
        if (fdc_recalibrate_drive(drive) < 0) {
            kprintf("fdc: fd%u recalibrate failed\n", drive->drive_index);
            drive->present = 0;
            continue;
        }
        fdc_register_drive(drive);
    }
}
