/*
 * uas.c — USB Attached SCSI (UAS) transport driver.
 *
 * UAS (interface class 0x08 / subclass 0x06 / protocol 0x62) is the modern,
 * faster alternative to Bulk-Only Transport (usb_msc.c).  Instead of the serial
 * CBW -> data -> CSW dance on two bulk pipes, it uses four pipes carrying
 * Information Units (IUs):
 *   - Command pipe  (bulk OUT): Command IU (the SCSI CDB + LUN + task tag)
 *   - Status pipe   (bulk IN) : Sense / Read-Ready / Write-Ready / Response IUs
 *   - Data-In pipe  (bulk IN) : read data
 *   - Data-Out pipe (bulk OUT): write data
 * The pipes are identified by class-specific Pipe Usage descriptors (0x24,
 * bPipeID 1..4) that follow each endpoint in the config descriptor.
 *
 * On USB 3.0 UAS multiplexes commands with bulk streams; this driver runs the
 * stream-less USB 2.0 variant (one command outstanding at a time, which the
 * substrate EHCI/xHCI HCDs and the SCSI mid-layer's queue depth of 1 expect).
 * It plugs into the SCSI mid-layer via scsi_link_t exactly like usb_msc.c.
 */
#include <string.h>
#include <stdio.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <drivers/usb/usb.h>
#include <drivers/storage/scsi/scsi.h>

/* UAS Information Unit IDs (first byte of every IU). */
#define UAS_IU_COMMAND      0x01
#define UAS_IU_SENSE        0x03
#define UAS_IU_RESPONSE     0x04
#define UAS_IU_TASK_MGMT    0x05
#define UAS_IU_READ_READY   0x06
#define UAS_IU_WRITE_READY  0x07

/* Pipe Usage descriptor. */
#define UAS_PIPE_USAGE_DT   0x24
#define UAS_PIPE_COMMAND    1
#define UAS_PIPE_STATUS     2
#define UAS_PIPE_DATA_IN    3
#define UAS_PIPE_DATA_OUT   4

#define UAS_MAX_DEVICES     4
#define UAS_CMD_IU_SIZE     32     /* header(8) + LUN(8) + CDB(16) */
#define UAS_STATUS_IU_SIZE  64     /* Sense IU header + sense data */

typedef struct uas_dev {
    int              active;
    usb_device_t    *udev;
    usb_endpoint_t  *ep_cmd;
    usb_endpoint_t  *ep_status;
    usb_endpoint_t  *ep_data_in;
    usb_endpoint_t  *ep_data_out;
    uint16_t         tag;
    mutex_t          lock;
    uint8_t         *cmd_iu;      /* UAS_CMD_IU_SIZE */
    uint8_t         *status_iu;   /* UAS_STATUS_IU_SIZE */
    scsi_link_t      scsi_link;
} uas_dev_t;

static uas_dev_t uas_devices[UAS_MAX_DEVICES];

static usb_endpoint_t *uas_ep_by_addr(usb_device_t *dev, uint8_t addr)
{
    for (int i = 0; i < dev->num_endpoints; i++)
        if (dev->endpoints[i].address == addr)
            return &dev->endpoints[i];
    return NULL;
}

/* Walk the raw config descriptor mapping each bulk endpoint to its UAS pipe
 * via the Pipe Usage descriptor (0x24) that follows it. */
static int uas_find_pipes(usb_device_t *dev, uas_dev_t *u)
{
    uint8_t *p = dev->config_data;
    uint8_t *end = p + dev->config_len;
    uint8_t last_ep_addr = 0;
    int have_ep = 0;

    while (p + 2 <= end) {
        uint8_t len = p[0], type = p[1];
        if (len < 2 || p + len > end)
            break;
        if (type == USB_DT_ENDPOINT && len >= 7) {
            last_ep_addr = p[2];   /* bEndpointAddress */
            have_ep = 1;
        } else if (type == UAS_PIPE_USAGE_DT && len >= 3 && have_ep) {
            usb_endpoint_t *ep = uas_ep_by_addr(dev, last_ep_addr);
            if (ep) {
                switch (p[2]) {   /* bPipeID */
                case UAS_PIPE_COMMAND:  u->ep_cmd = ep;      break;
                case UAS_PIPE_STATUS:   u->ep_status = ep;   break;
                case UAS_PIPE_DATA_IN:  u->ep_data_in = ep;  break;
                case UAS_PIPE_DATA_OUT: u->ep_data_out = ep; break;
                }
            }
            have_ep = 0;
        }
        p += len;
    }
    return (u->ep_cmd && u->ep_status && u->ep_data_in && u->ep_data_out) ? 0 : -1;
}

/*
 * Execute one SCSI command over UAS.  Returns 0 on GOOD status, 1 on a device
 * error (CHECK CONDITION), <0 on a transport error.
 */
static int uas_transfer(uas_dev_t *u, uint8_t lun, const uint8_t *cdb,
                        uint8_t cdb_len, void *data, uint32_t data_len,
                        int is_read, uint32_t *residue)
{
    uint32_t actual = 0;
    (void)is_read;   /* direction comes from the device's Read/Write-Ready IU */
    if (residue) *residue = 0;

    uint16_t tag = ++u->tag;
    if (tag == 0) tag = ++u->tag;

    /* ---- Command IU ---- */
    uint8_t *ciu = u->cmd_iu;
    memset(ciu, 0, UAS_CMD_IU_SIZE);
    ciu[0] = UAS_IU_COMMAND;
    ciu[2] = (uint8_t)(tag >> 8);
    ciu[3] = (uint8_t)(tag & 0xFF);
    ciu[4] = 0;                    /* simple task attribute */
    ciu[6] = 0;                    /* no additional CDB */
    ciu[9] = lun;                  /* single-level LUN (byte 9 for lun < 256) */
    memcpy(&ciu[16], cdb, cdb_len > 16 ? 16 : cdb_len);
    if (usb_bulk_transfer(u->udev, u->ep_cmd, ciu, UAS_CMD_IU_SIZE, &actual) != USB_XFER_OK)
        return -1;

    /* ---- Status / data phases ---- */
    for (int iter = 0; iter < 4; iter++) {
        uint8_t *siu = u->status_iu;
        memset(siu, 0, UAS_STATUS_IU_SIZE);
        int r = usb_bulk_transfer(u->udev, u->ep_status, siu, UAS_STATUS_IU_SIZE, &actual);
        if (r != USB_XFER_OK && r != USB_XFER_SHORT)
            return -1;

        switch (siu[0]) {
        case UAS_IU_READ_READY:
            if (data_len) {
                r = usb_bulk_transfer(u->udev, u->ep_data_in, data, data_len, &actual);
                if (r != USB_XFER_OK && r != USB_XFER_SHORT)
                    return -1;
                if (residue) *residue = (data_len > actual) ? (data_len - actual) : 0;
            }
            break;   /* next status IU should be the Sense IU */
        case UAS_IU_WRITE_READY:
            if (data_len) {
                r = usb_bulk_transfer(u->udev, u->ep_data_out, data, data_len, &actual);
                if (r != USB_XFER_OK && r != USB_XFER_SHORT)
                    return -1;
                if (residue) *residue = (data_len > actual) ? (data_len - actual) : 0;
            }
            break;
        case UAS_IU_SENSE:
            return (siu[6] == SCSI_STATUS_GOOD) ? 0 : 1;   /* byte 6 = SCSI status */
        case UAS_IU_RESPONSE:
        default:
            return -1;
        }
    }
    return -1;
}

/* ---- SCSI mid-layer glue ---- */
static int uas_scsi_execute(scsi_link_t *link, scsi_request_t *req)
{
    uas_dev_t *u = link->priv;
    uint32_t residue = 0;
    if (!u || !u->active || !req)
        return -1;

    mutex_lock(&u->lock);
    int is_read = (req->flags & SCSI_REQ_READ) ? 1 : 0;
    int ret = uas_transfer(u, (uint8_t)req->device->lun, req->cdb, req->cdb_len,
                           req->data, req->data_len, is_read, &residue);
    mutex_unlock(&u->lock);

    if (ret == 0) {
        req->status = SCSI_STATUS_GOOD;
        req->data_xfer = req->data_len - residue;
        return 0;
    }
    if (ret == 1) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        /* Auto-request sense (UAS carries sense in the Sense IU, but the SCSI
         * mid-layer expects it in req->sense; fetch it explicitly). */
        if (!(req->flags & SCSI_REQ_NO_SENSE) && req->cdb[0] != SCSI_CMD_REQUEST_SENSE) {
            uint8_t sense_cdb[6] = { SCSI_CMD_REQUEST_SENSE, 0, 0, 0, 18, 0 };
            uint8_t sense_buf[18];
            uint32_t sr;
            mutex_lock(&u->lock);
            int sret = uas_transfer(u, (uint8_t)req->device->lun, sense_cdb, 6,
                                    sense_buf, sizeof(sense_buf), 1, &sr);
            mutex_unlock(&u->lock);
            if (sret == 0) {
                uint8_t clen = 18 > SCSI_MAX_SENSE_LEN ? SCSI_MAX_SENSE_LEN : 18;
                memcpy(req->sense, sense_buf, clen);
                req->sense_len = clen;
            }
        }
        return -1;
    }
    req->status = SCSI_STATUS_CHECK_CONDITION;
    return -1;
}

static int uas_probe(usb_device_t *dev)
{
    if (dev->if_class == USB_CLASS_MASS_STORAGE &&
        dev->if_subclass == USB_MSC_SUBCLASS_SCSI &&
        dev->if_protocol == USB_MSC_PROTO_UAS)
        return 0;
    return -1;
}

static int uas_attach(usb_device_t *dev)
{
    uas_dev_t *u = NULL;
    for (int i = 0; i < UAS_MAX_DEVICES; i++) {
        if (!uas_devices[i].active) { u = &uas_devices[i]; break; }
    }
    if (!u) { kprintf("uas: no free device slots\n"); return -1; }

    memset(u, 0, sizeof(*u));
    u->udev = dev;
    if (uas_find_pipes(dev, u) != 0) {
        kprintf("uas: could not identify all four UAS pipes\n");
        return -1;
    }
    mutex_init(&u->lock, "uas");
    u->cmd_iu    = kmalloc(UAS_CMD_IU_SIZE);
    u->status_iu = kmalloc(UAS_STATUS_IU_SIZE);
    if (!u->cmd_iu || !u->status_iu) {
        if (u->cmd_iu) kfree(u->cmd_iu, UAS_CMD_IU_SIZE);
        if (u->status_iu) kfree(u->status_iu, UAS_STATUS_IU_SIZE);
        u->cmd_iu = u->status_iu = NULL;
        return -1;
    }
    u->active = 1;

    int slot = (int)(u - uas_devices);
    memset(&u->scsi_link, 0, sizeof(scsi_link_t));
    snprintf(u->scsi_link.name, sizeof(u->scsi_link.name), "uas%d", slot);
    u->scsi_link.bus_id = 6 + (uint8_t)slot;   /* after ATAPI/AHCI/usb_msc buses */
    u->scsi_link.max_targets = 1;
    u->scsi_link.max_luns = 1;
    u->scsi_link.adapter_queue_depth = 1;      /* stream-less: one command */
    u->scsi_link.execute = uas_scsi_execute;
    u->scsi_link.priv = u;
    if (scsi_register_link(&u->scsi_link) != 0) {
        kprintf("uas: failed to register SCSI link\n");
        u->active = 0;
        return -1;
    }
    dev->driver_data = u;
    kprintf("uas: attached %04x:%04x -> scsi bus %u (USB Attached SCSI)\n",
            dev->vendor_id, dev->product_id, u->scsi_link.bus_id);
    scsi_scan_bus(&u->scsi_link, u->scsi_link.bus_id);
    return 0;
}

static void uas_detach(usb_device_t *dev)
{
    uas_dev_t *u = dev->driver_data;
    if (!u || !u->active)
        return;
    scsi_unregister_link(&u->scsi_link);
    if (u->cmd_iu)    { kfree(u->cmd_iu, UAS_CMD_IU_SIZE); u->cmd_iu = NULL; }
    if (u->status_iu) { kfree(u->status_iu, UAS_STATUS_IU_SIZE); u->status_iu = NULL; }
    u->active = 0;
    u->udev = NULL;
    dev->driver_data = NULL;
    kprintf("uas: detached device\n");
}

static usb_class_driver_t uas_driver = {
    .name        = "uas",
    .if_class    = USB_CLASS_MASS_STORAGE,
    .if_subclass = USB_MSC_SUBCLASS_SCSI,
    .if_protocol = USB_MSC_PROTO_UAS,
    .probe       = uas_probe,
    .attach      = uas_attach,
    .detach      = uas_detach,
};

void uas_init(void)
{
    memset(uas_devices, 0, sizeof(uas_devices));
    usb_register_class_driver(&uas_driver);
}
