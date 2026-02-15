/*
 * scsi_ctl.c - SCSI Controller Interface
 *
 * Provides ioctl interface for bus enumeration and management.
 * Creates device node hierarchy:
 *   /dev/storage/scsi/B:T:L - Direct generic SCSI access (Bus:Target:LUN)
 *   /dev/storage/scsi/B     - Bus controller ioctl endpoint
 *   /dev/storage/scsiN      - High-level block device alias
 */

#include <string.h>
#include <stdio.h>
#include <kern/console.h>
#include <vfs/vfs.h>
#include <drivers/storage/blkdev.h>
#include <sys/kern_syscalls.h>
#include <sys/errno.h>
#include <vm/vm_kmem.h>
#include "scsi.h"

/* Forward declarations from scsi_dev.c */
extern int scsi_dev_attach(scsi_device_t *scsi_dev);
extern void scsi_dev_init(void);

/*
 * ============================================================
 * SCSI Control IOCTLs
 * ============================================================
 */

/* IOCTL commands */
#define SCSI_IOCTL_SCAN_BUS     0x5301  /* Rescan SCSI bus */
#define SCSI_IOCTL_GET_INFO     0x5302  /* Get device info */
#define SCSI_IOCTL_RESET_BUS    0x5303  /* Reset SCSI bus */
#define SCSI_IOCTL_GET_COUNT    0x5304  /* Get device count */
#define SCSI_IOCTL_SEND_CMD     0x5305  /* Send raw SCSI command */

/*
 * Device info structure returned by SCSI_IOCTL_GET_INFO
 */
typedef struct scsi_ioctl_info {
    uint8_t  bus;
    uint8_t  target;
    uint8_t  lun;
    uint8_t  type;
    char     vendor[9];
    char     product[17];
    char     revision[5];
    uint64_t capacity;
    uint32_t sector_size;
} scsi_ioctl_info_t;

/*
 * Raw SCSI command structure for SCSI_IOCTL_SEND_CMD
 */
typedef struct scsi_ioctl_cmd {
    uint8_t  cdb[16];
    uint8_t  cdb_len;
    uint8_t  direction;     /* 0=none, 1=read, 2=write */
    uint32_t data_len;
    void    *data;
    uint8_t  sense[18];
    uint8_t  status;
    int      error;
} scsi_ioctl_cmd_t;

/*
 * ============================================================
 * Per-Device Generic SCSI Node (B:T:L)
 * ============================================================
 */

typedef struct scsi_generic_node {
    fs_node_t node;
    scsi_device_t *dev;
    struct scsi_generic_node *next;
} scsi_generic_node_t;

static scsi_generic_node_t *sg_list = NULL;
static scsi_generic_node_t sg_pool[64];
static uint32_t sg_count = 0;

static int sg_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    scsi_generic_node_t *sg = (scsi_generic_node_t *)node->impl;
    if (!sg || !sg->dev) return -1;
    
    switch (request) {
    case SCSI_IOCTL_GET_INFO: {
        if (!arg) return -EINVAL;
        scsi_ioctl_info_t kinfo;
        memset(&kinfo, 0, sizeof(kinfo));
        scsi_device_t *dev = sg->dev;
        
        kinfo.bus = dev->bus;
        kinfo.target = dev->target;
        kinfo.lun = (uint8_t)dev->lun;
        kinfo.type = dev->type;
        strncpy(kinfo.vendor, dev->vendor, 8);
        kinfo.vendor[8] = '\0';
        strncpy(kinfo.product, dev->product, 16);
        kinfo.product[16] = '\0';
        strncpy(kinfo.revision, dev->revision, 4);
        kinfo.revision[4] = '\0';
        kinfo.capacity = dev->capacity;
        kinfo.sector_size = dev->sector_size;

        if (copyout(&kinfo, arg, sizeof(kinfo)) != 0) return -EFAULT;
        return 0;
    }
    
    case SCSI_IOCTL_SEND_CMD: {
        if (!arg) return -EINVAL;
        scsi_ioctl_cmd_t kcmd;
        if (copyin(arg, &kcmd, sizeof(kcmd)) != 0) return -EFAULT;

        /* Limit data length to avoid excessive kernel allocation (64KB) */
        if (kcmd.data_len > 65536) return -EINVAL;

        void *kdata = NULL;
        if (kcmd.data_len > 0) {
            kdata = kmalloc(kcmd.data_len);
            if (!kdata) return -ENOMEM;

            if (kcmd.direction == 2) { /* WRITE */
                if (copyin(kcmd.data, kdata, kcmd.data_len) != 0) {
                    kfree(kdata, kcmd.data_len);
                    return -EFAULT;
                }
            }
        }
        
        uint16_t flags = 0;
        if (kcmd.direction == 1) flags = SCSI_REQ_READ;
        else if (kcmd.direction == 2) flags = SCSI_REQ_WRITE;
        
        int ret = scsi_execute_sync(sg->dev, kcmd.cdb, kcmd.cdb_len,
                                     kdata, kcmd.data_len,
                                     flags, 30000);

        kcmd.error = ret;

        if (ret >= 0 && kcmd.direction == 1 && kcmd.data_len > 0) { /* READ */
            if (copyout(kdata, kcmd.data, kcmd.data_len) != 0) {
                if (kdata) kfree(kdata, kcmd.data_len);
                return -EFAULT;
            }
        }

        if (copyout(&kcmd, arg, sizeof(kcmd)) != 0) {
            if (kdata) kfree(kdata, kcmd.data_len);
            return -EFAULT;
        }

        if (kdata) kfree(kdata, kcmd.data_len);
        return ret;
    }
    
    default:
        return -1;
    }
}

/*
 * Create /dev/storage/scsi/B:T:L node for a device
 */
static int scsi_create_generic_node(scsi_device_t *dev) {
    if (!dev || sg_count >= 64) return -1;
    
    scsi_generic_node_t *sg = &sg_pool[sg_count];
    memset(sg, 0, sizeof(*sg));
    
    sg->dev = dev;
    
    /* Name format: B:T:L */
    snprintf(sg->node.name, sizeof(sg->node.name), "%d:%d:%d",
             dev->bus, dev->target, dev->lun);
    
    sg->node.flags = FS_CHARDEVICE;
    sg->node.impl = (uint32_t)(uintptr_t)sg;
    sg->node.ioctl = sg_ioctl;
    
    /* Register - path would be /dev/storage/scsi/B:T:L */
    devfs_register_device(&sg->node);
    
    sg->next = sg_list;
    sg_list = sg;
    sg_count++;
    
    return 0;
}

/*
 * ============================================================
 * Bus Controller Node (/dev/storage/scsi/B)
 * ============================================================
 */

typedef struct scsi_bus_node {
    fs_node_t node;
    scsi_link_t *link;
    uint8_t bus_id;
} scsi_bus_node_t;

static scsi_bus_node_t bus_nodes[8];
static uint32_t bus_count = 0;

static int bus_ioctl(fs_node_t *node, uint32_t request, void *arg) {
    scsi_bus_node_t *bn = (scsi_bus_node_t *)node->impl;
    if (!bn || !bn->link) return -1;
    
    switch (request) {
    case SCSI_IOCTL_SCAN_BUS: {
        char log_buf[48];
        snprintf(log_buf, sizeof(log_buf), "scsi: rescanning bus %d\n", bn->bus_id);
        kprint(log_buf);
        scsi_scan_bus(bn->link, bn->bus_id);
        return 0;
    }
        
    case SCSI_IOCTL_RESET_BUS:
        kprint("scsi: bus reset not yet implemented\n");
        return -1;
        
    case SCSI_IOCTL_GET_COUNT: {
        if (!arg) return -EINVAL;
        /* Count devices on this bus */
        int count = 0;
        for (uint8_t t = 0; t < 16; t++) {
            for (uint8_t l = 0; l < 8; l++) {
                if (scsi_device_lookup(bn->bus_id, t, l)) count++;
            }
        }
        if (copyout(&count, arg, sizeof(int)) != 0) return -EFAULT;
        return 0;
    }
    
    default:
        return -1;
    }
}

/*
 * Create /dev/storage/scsi/B node for a bus
 */
int scsi_create_bus_node(scsi_link_t *link, uint8_t bus_id) {
    if (!link || bus_count >= 8) return -1;
    
    scsi_bus_node_t *bn = &bus_nodes[bus_count];
    memset(bn, 0, sizeof(*bn));
    
    bn->link = link;
    bn->bus_id = bus_id;
    snprintf(bn->node.name, sizeof(bn->node.name), "%d", bus_id);
    bn->node.flags = FS_CHARDEVICE;
    bn->node.impl = (uint32_t)(uintptr_t)bn;
    bn->node.ioctl = bus_ioctl;
    
    devfs_register_device(&bn->node);
    bus_count++;
    
    char log_buf[64];
    snprintf(log_buf, sizeof(log_buf), "scsi: registered bus controller /dev/storage/scsi/%d\n", bus_id);
    kprint(log_buf);
    return 0;
}

/*
 * ============================================================
 * Initialization and Auto-Attach
 * ============================================================
 */

void scsi_ctl_init(void) {
    scsi_dev_init();
    kprint("scsi: SCSI control interface initialized\n");
}

/*
 * Auto-attach callback - called when SCSI devices are discovered
 * Creates both B:T:L generic node and scsiN block device
 */
void scsi_auto_attach(scsi_device_t *dev) {
    if (!dev) return;
    
    /* Create generic SCSI access node: /dev/storage/scsi/B:T:L */
    scsi_create_generic_node(dev);
    
    /* Create block device if applicable: /dev/storage/scsiN */
    scsi_dev_attach(dev);
}
