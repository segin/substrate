/*
 * ahci.c - AHCI (Advanced Host Controller Interface) Driver
 *
 * Full SATA host controller driver implementing:
 * - PCI-based AHCI controller detection and initialization
 * - Per-port command list, FIS receive, and command table DMA setup
 * - SATA device detection via port signature (COMRESET)
 * - ATA IDENTIFY for disk geometry
 * - DMA read/write via ATA READ/WRITE DMA EXT commands
 * - Block device registration for SATA disks (blkdev_t)
 * - SCSI transport registration for SATAPI devices (scsi_link_t)
 * - Integration with Substrate driver model (PCI bus type)
 *
 * Architecture: polling mode (no IRQ). Single command slot (slot 0).
 *
 * References:
 * - Serial ATA AHCI 1.3.1 Specification
 * - ATA/ATAPI Command Set (ACS-3)
 */

#include <string.h>
#include <stdio.h>
#include <kern/console.h>
#include <kern/pci.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/resource.h>
#include <kern/time.h>
#include <sys/dma.h>
#include <drivers/storage/ahci/ahci.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/scsi/scsi.h>

/*
 * ============================================================
 * Per-Port Driver State
 * ============================================================
 */

#define AHCI_PORT_TYPE_NONE     0
#define AHCI_PORT_TYPE_SATA     1
#define AHCI_PORT_TYPE_SATAPI   2
#define AHCI_PORT_TYPE_SEMB     3
#define AHCI_PORT_TYPE_PM       4

typedef struct ahci_port {
    int                port_num;
    int                type;        /* AHCI_PORT_TYPE_* */
    hba_port_t        *regs;        /* MMIO port registers */

    /* DMA areas */
    hba_cmd_header_t  *cmd_list;    /* Command list (32 entries, 1024 bytes) */
    dma_addr_t         cmd_list_dma;
    hba_fis_t         *fis_recv;    /* FIS receive area (256 bytes) */
    dma_addr_t         fis_recv_dma;
    hba_cmd_table_t   *cmd_table;   /* Command table for slot 0 */
    dma_addr_t         cmd_table_dma;

    /* Disk identity (from IDENTIFY) */
    uint64_t           sectors;
    uint32_t           sector_size;
    char               model[41];
    char               serial[21];
    char               firmware[9];
    int                lba48;

    /* Block device (for SATA disks) */
    blkdev_t           bdev;
    int                disk_index;  /* sata0, sata1, ... */
} ahci_port_t;

/*
 * ============================================================
 * Controller State
 * ============================================================
 */
typedef struct ahci_controller {
    hba_mem_t      *abar;           /* MMIO base (ioremap'd BAR5) */
    pci_device_t   *pci_dev;
    uint32_t        cap;            /* Cached CAP register */
    uint32_t        pi;             /* Ports Implemented bitmap */
    int             num_ports;      /* Number of ports (from CAP) */
    int             num_cmd_slots;  /* Number of command slots (from CAP) */
    ahci_port_t     ports[AHCI_MAX_PORTS];
    int             port_count;     /* Number of active ports */
    int             disk_count;     /* Running sata disk index */
} ahci_controller_t;

static ahci_controller_t ahci_ctrl;
static int ahci_initialized;

/* SCSI link for SATAPI devices */
static scsi_link_t ahci_scsi_link;
static int ahci_scsi_registered;

/*
 * ============================================================
 * Timing Helpers
 * ============================================================
 */

static inline uint64_t ahci_time_ms(void) {
    return (uint64_t)get_uptime_ms();
}

/*
 * ============================================================
 * Port Command Engine Control
 * ============================================================
 */

/* Stop command engine on a port */
static int ahci_port_stop(hba_port_t *port) {
    uint64_t deadline;

    /* Clear ST (command engine) */
    port->cmd &= ~HBA_PXCMD_ST;

    /* Wait for CR to clear (command list not running) */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_CR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port stop timeout (CR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    /* Clear FRE (FIS receive) */
    port->cmd &= ~HBA_PXCMD_FRE;

    /* Wait for FR to clear */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_FR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port stop timeout (FR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    return 0;
}

/* Start command engine on a port */
static void ahci_port_start(hba_port_t *port) {
    /* Wait until CR clears before setting ST */
    while (port->cmd & HBA_PXCMD_CR) {
        __asm__ volatile("pause");
    }

    port->cmd |= HBA_PXCMD_FRE;
    port->cmd |= HBA_PXCMD_ST;
}

/*
 * ============================================================
 * Port Initialization
 * ============================================================
 */

static int ahci_port_alloc(ahci_port_t *ap) {
    /* Allocate Command List (1024 bytes, 1024-byte aligned) */
    ap->cmd_list = dma_alloc_coherent(sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS,
                                       &ap->cmd_list_dma);
    if (!ap->cmd_list) {
        kprint("ahci: failed to alloc command list\n");
        return -1;
    }

    /* Allocate FIS Receive Area (256 bytes, 256-byte aligned) */
    ap->fis_recv = dma_alloc_coherent(sizeof(hba_fis_t), &ap->fis_recv_dma);
    if (!ap->fis_recv) {
        kprint("ahci: failed to alloc FIS receive\n");
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->cmd_list = NULL;
        return -1;
    }

    /* Allocate Command Table for slot 0 (128-byte aligned) */
    ap->cmd_table = dma_alloc_coherent(sizeof(hba_cmd_table_t), &ap->cmd_table_dma);
    if (!ap->cmd_table) {
        kprint("ahci: failed to alloc command table\n");
        dma_free_coherent(ap->fis_recv, sizeof(hba_fis_t));
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->fis_recv = NULL;
        ap->cmd_list = NULL;
        return -1;
    }

    /* Point command header slot 0 at the command table */
    ap->cmd_list[AHCI_CMD_SLOT].ctba = (uint32_t)ap->cmd_table_dma;
    ap->cmd_list[AHCI_CMD_SLOT].ctbau = 0; /* 32-bit addresses */
    ap->cmd_list[AHCI_CMD_SLOT].prdtl = AHCI_MAX_PRDT_ENTRIES;

    return 0;
}

static int ahci_port_init(ahci_port_t *ap, hba_port_t *port_regs, int port_num) {
    ap->port_num = port_num;
    ap->regs = port_regs;
    ap->type = AHCI_PORT_TYPE_NONE;

    /* Stop command engine */
    if (ahci_port_stop(port_regs) < 0) {
        return -1;
    }

    /* Allocate DMA memory */
    if (ahci_port_alloc(ap) < 0) {
        return -1;
    }

    /* Program CLB and FB registers */
    port_regs->clb  = (uint32_t)ap->cmd_list_dma;
    port_regs->clbu = 0;
    port_regs->fb   = (uint32_t)ap->fis_recv_dma;
    port_regs->fbu  = 0;

    /* Clear pending interrupts and errors */
    port_regs->serr = 0xFFFFFFFF;   /* Write-1-to-clear all error bits */
    port_regs->is   = 0xFFFFFFFF;   /* Write-1-to-clear all interrupt bits */

    /* Start command engine */
    ahci_port_start(port_regs);

    return 0;
}

/*
 * ============================================================
 * Device Detection
 * ============================================================
 */

static int ahci_port_detect_device(ahci_port_t *ap) {
    uint32_t ssts = ap->regs->ssts;
    uint8_t  det = ssts & HBA_PXSSTS_DET_MASK;
    uint8_t  ipm = (ssts & HBA_PXSSTS_IPM_MASK) >> 8;

    if (det != 0x03 || ipm != 0x01) {
        ap->type = AHCI_PORT_TYPE_NONE;
        return 0;   /* No device or not active */
    }

    switch (ap->regs->sig) {
    case SATA_SIG_ATA:
        ap->type = AHCI_PORT_TYPE_SATA;
        break;
    case SATA_SIG_ATAPI:
        ap->type = AHCI_PORT_TYPE_SATAPI;
        break;
    case SATA_SIG_SEMB:
        ap->type = AHCI_PORT_TYPE_SEMB;
        break;
    case SATA_SIG_PM:
        ap->type = AHCI_PORT_TYPE_PM;
        break;
    default:
        ap->type = AHCI_PORT_TYPE_SATA;  /* Assume SATA for unknown sigs */
        break;
    }

    return 1;   /* Device present */
}

/*
 * ============================================================
 * Command Execution (Polling)
 * ============================================================
 */

/*
 * Issue a command in slot 0 and wait for completion.
 * The caller must have filled in cmd_list[0] and cmd_table before calling.
 */
static int ahci_port_issue_cmd(ahci_port_t *ap, uint32_t timeout_ms) {
    hba_port_t *port = ap->regs;
    uint64_t deadline;

    /* Clear interrupt status */
    port->is = 0xFFFFFFFF;

    /* Issue command in slot 0 */
    port->ci = (1U << AHCI_CMD_SLOT);

    /* Poll for completion */
    deadline = ahci_time_ms() + timeout_ms;
    while (1) {
        /* Check if slot is done */
        if ((port->ci & (1U << AHCI_CMD_SLOT)) == 0) {
            break;
        }

        /* Check for fatal errors */
        if (port->is & HBA_PXIS_FATAL) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "ahci: port %d fatal error IS=0x%08x SERR=0x%08x TFD=0x%08x\n",
                     ap->port_num, port->is, port->serr, port->tfd);
            kprint(buf);
            /* Clear errors */
            port->serr = port->serr;
            port->is = port->is;
            return -1;
        }

        /* Timeout check */
        if (ahci_time_ms() > deadline) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "ahci: port %d command timeout\n", ap->port_num);
            kprint(buf);
            return -1;
        }

        __asm__ volatile("pause");
    }

    /* Check Task File for errors */
    if (port->tfd & (HBA_PXTFD_ERR | HBA_PXTFD_BSY)) {
        return -1;
    }

    return 0;
}

/*
 * Build a Register H2D FIS for an ATA command.
 */
static void ahci_build_h2d_fis(struct fis_reg_h2d *fis, uint8_t command,
                                 uint64_t lba, uint32_t count, uint8_t device) {
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;   /* C bit = 1 → this is a command FIS */
    fis->command  = command;
    fis->device   = device;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);
}

/*
 * Execute a single ATA DMA command (read or write) on a port.
 * data_virt must be a kernel virtual address suitable for DMA.
 */
static int ahci_ata_dma_cmd(ahci_port_t *ap, uint8_t command,
                             uint64_t lba, uint32_t sector_count,
                             void *data_virt, int is_write) {
    hba_cmd_header_t *hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hba_cmd_table_t *tbl  = ap->cmd_table;
    struct fis_reg_h2d *fis;
    dma_addr_t data_dma;
    uint32_t byte_count;

    byte_count = sector_count * ap->sector_size;

    /* Clear command table */
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    /* Build command FIS */
    fis = (struct fis_reg_h2d *)tbl->cfis;
    ahci_build_h2d_fis(fis, command, lba, sector_count, 0x40); /* LBA mode */

    /* Setup PRDT - single entry for the entire transfer */
    data_dma = dma_map_single(data_virt, byte_count,
                               is_write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
    tbl->prdt[0].dba  = (uint32_t)data_dma;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc  = byte_count - 1;  /* 0-based */
    tbl->prdt[0].i    = 0;

    /* Setup command header */
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;  /* 5 DWORDs */
    hdr->a     = 0;
    hdr->w     = is_write ? 1 : 0;
    hdr->p     = 1;    /* Prefetchable */
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 1;    /* Clear BSY on R_OK */
    hdr->pmp   = 0;
    hdr->prdtl = 1;    /* 1 PRDT entry */
    hdr->prdbc = 0;

    /* Issue and wait */
    int ret = ahci_port_issue_cmd(ap, AHCI_TIMEOUT_CMD);

    dma_unmap_single(data_dma, byte_count,
                      is_write ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

    return ret;
}

/*
 * ============================================================
 * IDENTIFY DEVICE
 * ============================================================
 */

static int ahci_identify(ahci_port_t *ap) {
    uint16_t *id_buf;
    dma_addr_t id_dma;
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    struct fis_reg_h2d *fis;
    uint8_t cmd;
    int ret;
    int i;

    id_buf = dma_alloc_coherent(512, &id_dma);
    if (!id_buf) {
        kprint("ahci: failed to alloc identify buffer\n");
        return -1;
    }

    memset(ap->cmd_table, 0, sizeof(hba_cmd_table_t));

    /* Choose IDENTIFY command based on device type */
    cmd = (ap->type == AHCI_PORT_TYPE_SATAPI)
              ? AHCI_ATA_CMD_IDENTIFY_PACKET
              : AHCI_ATA_CMD_IDENTIFY;

    /* Build command FIS */
    tbl = ap->cmd_table;
    fis = (struct fis_reg_h2d *)tbl->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = cmd;
    fis->device   = 0;

    /* Single PRDT entry for 512-byte identify data */
    tbl->prdt[0].dba  = (uint32_t)id_dma;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc  = 511;  /* 0-based */
    tbl->prdt[0].i    = 0;

    /* Command header */
    hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->a     = 0;
    hdr->w     = 0;     /* D2H (read) */
    hdr->p     = 1;
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 1;
    hdr->pmp   = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    ret = ahci_port_issue_cmd(ap, AHCI_TIMEOUT_IDENTIFY);
    if (ret < 0) {
        dma_free_coherent(id_buf, 512);
        return -1;
    }

    /* Parse IDENTIFY data */
    /* Model string: words 27-46 (byte-swapped pairs) */
    for (i = 0; i < 20; i++) {
        uint16_t w = id_buf[27 + i];
        ap->model[i * 2]     = (char)(w >> 8);
        ap->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->model[40] = '\0';
    /* Trim trailing spaces */
    for (i = 39; i >= 0 && ap->model[i] == ' '; i--) {
        ap->model[i] = '\0';
    }

    /* Serial number: words 10-19 */
    for (i = 0; i < 10; i++) {
        uint16_t w = id_buf[10 + i];
        ap->serial[i * 2]     = (char)(w >> 8);
        ap->serial[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->serial[20] = '\0';
    for (i = 19; i >= 0 && ap->serial[i] == ' '; i--) {
        ap->serial[i] = '\0';
    }

    /* Firmware revision: words 23-26 */
    for (i = 0; i < 4; i++) {
        uint16_t w = id_buf[23 + i];
        ap->firmware[i * 2]     = (char)(w >> 8);
        ap->firmware[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->firmware[8] = '\0';
    for (i = 7; i >= 0 && ap->firmware[i] == ' '; i--) {
        ap->firmware[i] = '\0';
    }

    /* Sector size */
    ap->sector_size = AHCI_SECTOR_SIZE;
    /* Check for logical sector size > 512 (word 106 bit 12) */
    if ((id_buf[106] & (1 << 14)) && !(id_buf[106] & (1 << 15))) {
        if (id_buf[106] & (1 << 12)) {
            ap->sector_size = ((uint32_t)id_buf[117] |
                               ((uint32_t)id_buf[118] << 16)) * 2;
        }
    }

    /* LBA48 support: word 83 bit 10, word 86 bit 10 */
    if ((id_buf[83] & (1 << 10)) && (id_buf[86] & (1 << 10))) {
        ap->lba48 = 1;
        ap->sectors = (uint64_t)id_buf[100] |
                      ((uint64_t)id_buf[101] << 16) |
                      ((uint64_t)id_buf[102] << 32) |
                      ((uint64_t)id_buf[103] << 48);
    } else {
        ap->lba48 = 0;
        ap->sectors = (uint64_t)id_buf[60] | ((uint64_t)id_buf[61] << 16);
    }

    dma_free_coherent(id_buf, 512);
    return 0;
}

/*
 * ============================================================
 * Block Device Interface (SATA Disks)
 * ============================================================
 */

static ahci_port_t *ahci_port_from_bdev(blkdev_t *bdev) {
    return (ahci_port_t *)bdev->priv;
}

static int ahci_bdev_read(blkdev_t *bdev, uint64_t sector,
                           uint32_t count, void *buffer) {
    ahci_port_t *ap = ahci_port_from_bdev(bdev);
    uint8_t *buf = (uint8_t *)buffer;
    uint32_t max_sectors;
    uint32_t chunk;

    if (!ap || ap->type != AHCI_PORT_TYPE_SATA) {
        return -1;
    }

    /* Max sectors per command: limited by PRDT (single entry, ~4MB) */
    max_sectors = (4 * 1024 * 1024) / ap->sector_size;
    if (max_sectors > 65535) {
        max_sectors = 65535;
    }

    while (count > 0) {
        chunk = (count > max_sectors) ? max_sectors : count;

        if (ahci_ata_dma_cmd(ap, AHCI_ATA_CMD_READ_DMA_EXT,
                              sector, chunk, buf, 0) < 0) {
            return -1;
        }

        sector += chunk;
        count  -= chunk;
        buf    += (uint64_t)chunk * ap->sector_size;
    }

    return 0;
}

static int ahci_bdev_write(blkdev_t *bdev, uint64_t sector,
                            uint32_t count, const void *buffer) {
    ahci_port_t *ap = ahci_port_from_bdev(bdev);
    const uint8_t *buf = (const uint8_t *)buffer;
    uint32_t max_sectors;
    uint32_t chunk;

    if (!ap || ap->type != AHCI_PORT_TYPE_SATA) {
        return -1;
    }

    max_sectors = (4 * 1024 * 1024) / ap->sector_size;
    if (max_sectors > 65535) {
        max_sectors = 65535;
    }

    while (count > 0) {
        chunk = (count > max_sectors) ? max_sectors : count;

        if (ahci_ata_dma_cmd(ap, AHCI_ATA_CMD_WRITE_DMA_EXT,
                              sector, chunk, (void *)buf, 1) < 0) {
            return -1;
        }

        sector += chunk;
        count  -= chunk;
        buf    += (uint64_t)chunk * ap->sector_size;
    }

    return 0;
}

static void ahci_register_disk(ahci_port_t *ap) {
    char buf[128];

    ap->disk_index = ahci_ctrl.disk_count++;

    memset(&ap->bdev, 0, sizeof(blkdev_t));
    snprintf(ap->bdev.name, sizeof(ap->bdev.name), "sata%d", ap->disk_index);
    ap->bdev.sector_size   = ap->sector_size;
    ap->bdev.total_sectors = ap->sectors;
    ap->bdev.priv          = ap;
    ap->bdev.read          = ahci_bdev_read;
    ap->bdev.write         = ahci_bdev_write;

    blkdev_register(&ap->bdev);

    snprintf(buf, sizeof(buf),
             "ahci: port %d: %s (%llu sectors, %u bytes/sect) -> /dev/storage/%s\n",
             ap->port_num, ap->model,
             (unsigned long long)ap->sectors, ap->sector_size,
             ap->bdev.name);
    kprint(buf);
}

/*
 * ============================================================
 * SCSI Transport Interface (SATAPI Devices)
 * ============================================================
 */

static int ahci_scsi_execute(scsi_link_t *link, scsi_request_t *req) {
    ahci_port_t *ap;
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    struct fis_reg_h2d *fis;
    dma_addr_t data_dma;
    int port_idx;
    int ret;

    (void)link;

    if (!req || !req->device) {
        return -1;
    }

    /* Map SCSI target to AHCI port.
     * Target ID is the port index in our registration order. */
    port_idx = req->device->target;
    ap = NULL;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ctrl.ports[i].type == AHCI_PORT_TYPE_SATAPI) {
            if (port_idx == 0) {
                ap = &ahci_ctrl.ports[i];
                break;
            }
            port_idx--;
        }
    }

    if (!ap) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        return -1;
    }

    /* Build ATAPI command via AHCI */
    tbl = ap->cmd_table;
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    /* Command FIS: ATA PACKET command */
    fis = (struct fis_reg_h2d *)tbl->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = AHCI_ATA_CMD_PACKET;
    fis->featurel = (req->flags & SCSI_REQ_WRITE) ? 0x00 : 0x04; /* DMA bit */
    fis->lba1     = (uint8_t)(req->data_len);         /* Byte count low */
    fis->lba2     = (uint8_t)(req->data_len >> 8);    /* Byte count high */

    /* Copy SCSI CDB into ACMD field */
    memcpy(tbl->acmd, req->cdb,
           req->cdb_len > 16 ? 16 : req->cdb_len);

    /* Setup PRDT if there's data */
    hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hdr->prdtl = 0;

    if (req->data && req->data_len > 0) {
        data_dma = dma_map_single(req->data, req->data_len,
                                   (req->flags & SCSI_REQ_WRITE)
                                       ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
        tbl->prdt[0].dba  = (uint32_t)data_dma;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc  = req->data_len - 1;
        tbl->prdt[0].i    = 0;
        hdr->prdtl = 1;
    }

    /* Command header */
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->a     = 1;    /* ATAPI command */
    hdr->w     = (req->flags & SCSI_REQ_WRITE) ? 1 : 0;
    hdr->p     = 1;
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 1;
    hdr->pmp   = 0;
    hdr->prdbc = 0;

    ret = ahci_port_issue_cmd(ap, req->timeout_ms ? req->timeout_ms : AHCI_TIMEOUT_CMD);

    if (req->data && req->data_len > 0) {
        dma_unmap_single(dma_map_single(req->data, req->data_len, DMA_BIDIRECTIONAL),
                          req->data_len,
                          (req->flags & SCSI_REQ_WRITE)
                              ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
    }

    if (ret < 0) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        return -1;
    }

    req->status = SCSI_STATUS_GOOD;
    req->data_xfer = req->data_len;
    return 0;
}

static int ahci_scsi_reset_device(scsi_link_t *link, scsi_device_t *dev) {
    (void)link;
    (void)dev;
    /* Port reset could be implemented via COMRESET */
    return 0;
}

static int ahci_scsi_reset_bus(scsi_link_t *link) {
    (void)link;
    return 0;
}

static void ahci_register_satapi_devices(void) {
    int satapi_count = 0;

    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ctrl.ports[i].type == AHCI_PORT_TYPE_SATAPI) {
            satapi_count++;
        }
    }

    if (satapi_count == 0) {
        return;
    }

    memset(&ahci_scsi_link, 0, sizeof(ahci_scsi_link));
    snprintf(ahci_scsi_link.name, sizeof(ahci_scsi_link.name), "ahci0");
    ahci_scsi_link.bus_id              = 1;  /* After ATAPI bus 0 */
    ahci_scsi_link.max_targets         = (uint8_t)satapi_count;
    ahci_scsi_link.max_luns            = 1;
    ahci_scsi_link.adapter_queue_depth = 1;
    ahci_scsi_link.execute             = ahci_scsi_execute;
    ahci_scsi_link.reset_device        = ahci_scsi_reset_device;
    ahci_scsi_link.reset_bus           = ahci_scsi_reset_bus;

    if (scsi_register_link(&ahci_scsi_link) == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "ahci: registered SATAPI transport (%d devices)\n",
                 satapi_count);
        kprint(buf);
        ahci_scsi_registered = 1;
    }
}

/*
 * ============================================================
 * HBA Initialization
 * ============================================================
 */

static int ahci_hba_init(ahci_controller_t *ctrl) {
    hba_mem_t *abar = ctrl->abar;
    uint32_t version;
    char buf[128];

    /* Enable AHCI mode (GHC.AE) */
    abar->ghc |= HBA_GHC_AE;

    /* Read capabilities */
    ctrl->cap = abar->cap;
    ctrl->pi  = abar->pi;
    ctrl->num_ports = (ctrl->cap & HBA_CAP_NP_MASK) + 1;
    ctrl->num_cmd_slots = ((ctrl->cap & HBA_CAP_NCS_MASK) >> HBA_CAP_NCS_SHIFT) + 1;
    version = abar->vs;

    snprintf(buf, sizeof(buf),
             "ahci: version %d.%d%d, %d ports, %d cmd slots, PI=0x%08x\n",
             (int)((version >> 16) & 0xFFFF),
             (int)((version >> 8) & 0xFF),
             (int)(version & 0xFF),
             ctrl->num_ports,
             ctrl->num_cmd_slots,
             ctrl->pi);
    kprint(buf);

    if (ctrl->cap & HBA_CAP_S64A) {
        kprint("ahci: 64-bit addressing supported (using 32-bit)\n");
    }
    if (ctrl->cap & HBA_CAP_SNCQ) {
        kprint("ahci: NCQ supported (not used)\n");
    }

    return 0;
}

/*
 * ============================================================
 * Port Probing
 * ============================================================
 */

static void ahci_probe_ports(ahci_controller_t *ctrl) {
    uint32_t pi = ctrl->pi;
    int port;
    char buf[128];

    ctrl->port_count = 0;
    ctrl->disk_count = 0;

    for (port = 0; port < AHCI_MAX_PORTS; port++) {
        ahci_port_t *ap;
        const char *type_str;

        if (!(pi & (1U << port))) {
            continue;
        }

        ap = &ctrl->ports[port];

        if (ahci_port_init(ap, &ctrl->abar->ports[port], port) < 0) {
            snprintf(buf, sizeof(buf),
                     "ahci: port %d init failed\n", port);
            kprint(buf);
            continue;
        }

        if (!ahci_port_detect_device(ap)) {
            continue;
        }

        switch (ap->type) {
        case AHCI_PORT_TYPE_SATA:   type_str = "SATA disk";   break;
        case AHCI_PORT_TYPE_SATAPI: type_str = "SATAPI";      break;
        case AHCI_PORT_TYPE_SEMB:   type_str = "SEMB";        break;
        case AHCI_PORT_TYPE_PM:     type_str = "Port Mult";   break;
        default:                    type_str = "unknown";      break;
        }

        snprintf(buf, sizeof(buf),
                 "ahci: port %d: %s detected (sig=0x%08x)\n",
                 port, type_str, ap->regs->sig);
        kprint(buf);

        ctrl->port_count++;

        /* IDENTIFY for SATA disks */
        if (ap->type == AHCI_PORT_TYPE_SATA) {
            if (ahci_identify(ap) == 0) {
                ahci_register_disk(ap);
            } else {
                snprintf(buf, sizeof(buf),
                         "ahci: port %d: IDENTIFY failed\n", port);
                kprint(buf);
            }
        }
    }

    /* Register SATAPI devices with SCSI mid-layer */
    ahci_register_satapi_devices();

    if (ctrl->port_count == 0) {
        kprint("ahci: no devices found\n");
    }
}

/*
 * ============================================================
 * PCI Driver Model Integration
 * ============================================================
 */

static int ahci_pci_attach(struct device *dev) {
    pci_device_t *pdev;
    void *mmio_base;
    size_t bar_sz;

    pdev = pci_find_device_by_kdev(dev);
    if (!pdev) {
        return -1;
    }

    /* Verify BAR5 is memory-mapped */
    if (pci_bar_type(pdev, 5) != PCI_BAR_MEM32) {
        kprint("ahci: BAR5 is not a 32-bit memory BAR\n");
        return -1;
    }

    bar_sz = pci_bar_size(pdev, 5);
    if (bar_sz == 0) {
        kprint("ahci: BAR5 size is 0\n");
        return -1;
    }

    /* Enable bus mastering and memory space */
    pci_write_config16(pdev->bus, pdev->slot, pdev->func,
                       PCI_CONFIG_COMMAND,
                       pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                         PCI_CONFIG_COMMAND) |
                       PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    /* Map AHCI registers */
    mmio_base = pci_iomap(pdev, 5, bar_sz);
    if (!mmio_base) {
        kprint("ahci: failed to iomap BAR5\n");
        return -1;
    }

    ahci_ctrl.abar    = (hba_mem_t *)mmio_base;
    ahci_ctrl.pci_dev = pdev;

    if (ahci_hba_init(&ahci_ctrl) < 0) {
        kprint("ahci: HBA init failed\n");
        return -1;
    }

    ahci_probe_ports(&ahci_ctrl);
    return 0;
}

/* PCI ID table: Class 01h (Mass Storage), Subclass 06h (SATA), ProgIF 01h (AHCI) */
static const device_id_t ahci_pci_ids[] = {
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x00010601U, 0x00FFFFFF, 0 },
    { 0, 0, 0, 0, 0 },
};

static struct driver ahci_pci_driver = {
    .name     = "ahci",
    .id_table = ahci_pci_ids,
    .attach   = ahci_pci_attach,
};

/*
 * ============================================================
 * Module Entry Point
 * ============================================================
 */

void ahci_init(void) {
    if (ahci_initialized) {
        return;
    }

    memset(&ahci_ctrl, 0, sizeof(ahci_ctrl));

    if (!pci_present()) {
        return;
    }

    (void)driver_register(&ahci_pci_driver, &pci_bus_type);
    ahci_initialized = 1;
}
