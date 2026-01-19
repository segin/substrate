/*
 * atapi_scsi.c - ATAPI SCSI Transport Adapter
 *
 * Bridges the SCSI mid-layer to IDE/ATAPI devices.
 * Encapsulates SCSI CDBs into ATA Packet Commands per ATA-PI-7.
 *
 * Transport path: SCSI mid-layer -> scsi_link -> atapi_execute -> ide_atapi_packet
 */

#include "scsi.h"
#include "../ide/ide.h"
#include "../../../kern/console.h"
#include "../../../arch/x86-common/include/io.h"
#include <string.h>
#include <stdio.h>

/*
 * ============================================================
 * ATAPI Transport Private Data
 * ============================================================
 */

typedef struct atapi_target {
    uint8_t channel;            /* IDE channel (0=primary, 1=secondary) */
    uint8_t drive;              /* Drive on channel (0=master, 1=slave) */
    uint8_t present;            /* Device is present */
    uint8_t type;               /* ATAPI device type from INQUIRY */
} atapi_target_t;

typedef struct atapi_link {
    scsi_link_t link;           /* SCSI transport link (must be first) */
    uint8_t bus_id;             /* Virtual bus ID */
    atapi_target_t targets[4];  /* Max 4 ATAPI devices (2 channels x 2 drives) */
    uint8_t target_count;       /* Number of attached targets */
} atapi_link_t;

/* Global state */
static atapi_link_t atapi_link;
static int atapi_initialized = 0;

/*
 * ============================================================
 * ATAPI Transport Callbacks
 * ============================================================
 */

/*
 * Execute SCSI command via ATAPI
 *
 * Encapsulates the SCSI CDB into an ATA Packet Command.
 * Handles CDB padding (ATAPI requires 12-byte CDBs).
 */
static int atapi_execute(scsi_link_t *link, scsi_request_t *req) {
    (void)link;  /* Unused - we use req->device to find target */
    
    if (!req || !req->device) return -1;
    
    scsi_device_t *dev = req->device;
    
    /* Map target ID to channel/drive */
    uint8_t channel = dev->target / 2;
    uint8_t drive = dev->target % 2;
    
    /* Check if target is valid */
    if (channel >= 2) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        return -1;
    }
    
    /* Prepare 12-byte CDB (ATAPI standard) */
    uint8_t atapi_cdb[12];
    memset(atapi_cdb, 0, 12);
    
    /* Copy CDB, padding with zeros if shorter than 12 bytes */
    uint8_t cdb_len = req->cdb_len;
    if (cdb_len > 12) cdb_len = 12;
    memcpy(atapi_cdb, req->cdb, cdb_len);
    
    /* Determine transfer direction */
    int write = (req->flags & SCSI_REQ_WRITE) ? 1 : 0;
    
    /* Execute via IDE ATAPI interface */
    int ret = ide_atapi_packet(channel, drive,
                                atapi_cdb, 12,
                                req->data, req->data_len, write);
    
    if (ret < 0) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        return ret;
    }
    
    req->status = SCSI_STATUS_GOOD;
    req->data_xfer = req->data_len;  /* Assume complete transfer */
    
    return 0;
}

/*
 * Reset ATAPI target device
 */
static int atapi_reset_device(scsi_link_t *link, scsi_device_t *dev) {
    (void)link;
    if (!dev) return -1;
    
    uint8_t channel = dev->target / 2;
    
    /* Software reset via Device Control Register */
    ide_write_ctrl(channel, 0x04);  /* Set SRST */
    for (volatile int i = 0; i < 10000; i++);
    ide_write_ctrl(channel, 0x00);  /* Clear SRST */
    for (volatile int i = 0; i < 100000; i++);
    
    char log_buf[64];
    sprintf(log_buf, "atapi_scsi: reset device %d:%d\n", dev->target, dev->lun);
    kprint(log_buf);
    return 0;
}

/*
 * Reset ATAPI bus
 */
static int atapi_reset_bus(scsi_link_t *link) {
    (void)link;
    
    /* Software reset all channels */
    for (int ch = 0; ch < 2; ch++) {
        ide_write_ctrl(ch, 0x04);  /* Set SRST */
        for (volatile int i = 0; i < 10000; i++);
        ide_write_ctrl(ch, 0x00);  /* Clear SRST */
        for (volatile int i = 0; i < 100000; i++);
    }
    
    kprint("atapi_scsi: bus reset complete\n");
    return 0;
}

/*
 * ============================================================
 * ATAPI Device Probing
 * ============================================================
 */

/*
 * Probe for ATAPI devices on both IDE channels
 */
static int atapi_probe_devices(atapi_link_t *alink) {
    uint16_t buses[2] = { ATA_PRIMARY_IO, ATA_SECONDARY_IO };
    const char *bus_names[2] = { "Primary", "Secondary" };
    const char *drive_names[2] = { "Master", "Slave" };
    
    alink->target_count = 0;
    
    for (int ch = 0; ch < 2; ch++) {
        /* Check for floating bus */
        if (inb(buses[ch] + ATA_REG_STATUS) == 0xFF) continue;
        
        for (int d = 0; d < 2; d++) {
            uint16_t buf[256];
            memset(buf, 0, 512);
            
            /* Try IDENTIFY ATAPI */
            int ret = ide_identify_atapi(buses[ch], d, buf);
            if (ret != 0) continue;
            
            /* Found an ATAPI device */
            atapi_target_t *target = &alink->targets[alink->target_count];
            target->channel = ch;
            target->drive = d;
            target->present = 1;
            
            /* Get device type from IDENTIFY word 0, bits 12-8 */
            uint8_t cmd_set = (buf[0] >> 8) & 0x1F;
            target->type = cmd_set;  /* Maps to SCSI peripheral type */
            
            /* Extract model string */
            char model[41];
            for (int i = 0; i < 20; i++) {
                uint16_t w = buf[27 + i];
                model[i * 2] = (w >> 8) & 0xFF;
                model[i * 2 + 1] = w & 0xFF;
            }
            model[40] = 0;
            
            /* Trim trailing spaces */
            for (int i = 39; i >= 0; i--) {
                if (model[i] == ' ' || model[i] == 0) model[i] = 0;
                else break;
            }
            
            char log_buf[96];
            sprintf(log_buf, "atapi_scsi: %s %s: %s (type 0x%02x)\n",
                    bus_names[ch], drive_names[d], model, target->type);
            kprint(log_buf);
            
            alink->target_count++;
            
            if (alink->target_count >= 4) break;
        }
        if (alink->target_count >= 4) break;
    }
    
    return alink->target_count;
}

/*
 * ============================================================
 * Transport Registration
 * ============================================================
 */

/*
 * Initialize ATAPI SCSI transport
 */
void atapi_scsi_init(void) {
    if (atapi_initialized) return;
    
    memset(&atapi_link, 0, sizeof(atapi_link));
    
    /* Setup transport operations */
    atapi_link.link.name = "atapi";
    atapi_link.link.execute = atapi_execute;
    atapi_link.link.reset_device = atapi_reset_device;
    atapi_link.link.reset_bus = atapi_reset_bus;
    atapi_link.bus_id = 0;  /* First SCSI bus */
    
    /* Probe for devices */
    int count = atapi_probe_devices(&atapi_link);
    
    if (count > 0) {
        /* Register with SCSI mid-layer */
        if (scsi_register_link(&atapi_link.link) == 0) {
            char log_buf[64];
            sprintf(log_buf, "atapi_scsi: registered with SCSI mid-layer (%d devices)\n", count);
            kprint(log_buf);
            
            /* Scan the bus to discover devices */
            scsi_scan_bus(&atapi_link.link, atapi_link.bus_id);
        }
    } else {
        kprint("atapi_scsi: no ATAPI devices found\n");
    }
    
    atapi_initialized = 1;
}

/*
 * Get ATAPI link for direct access
 */
scsi_link_t *atapi_get_link(void) {
    return atapi_initialized ? &atapi_link.link : NULL;
}
