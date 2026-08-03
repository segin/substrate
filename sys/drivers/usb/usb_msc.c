/*
 * usb_msc.c - USB Mass Storage Class Driver (Bulk-Only Transport)
 *
 * Implements the USB Mass Storage Bulk-Only Transport (BOT) protocol
 * per USB Mass Storage Class Specification, and bridges to the SCSI
 * mid-layer via scsi_link_t for transparent disk access.
 *
 * Protocol:
 *   Command Phase:  Send CBW (31 bytes) via Bulk-OUT
 *   Data Phase:     Transfer via Bulk-IN or Bulk-OUT (optional)
 *   Status Phase:   Receive CSW (13 bytes) via Bulk-IN
 *
 * References:
 *   USB Mass Storage Class - Bulk Only Transport 1.0 (usb.org)
 *   SCSI Primary Commands (SPC-3)
 */

#include <stdio.h>
#include <arch/i386/pmm.h>
#include <string.h>

#include <drivers/storage/scsi/scsi.h>
#include <drivers/usb/usb.h>
#include <kern/console.h>
#include <sys/dma.h>
#include <sys/lock.h>

/*
 * ============================================================
 * BOT Protocol Constants
 * ============================================================
 */

#define CBW_SIGNATURE   0x43425355U     /* 'USBC' */
#define CSW_SIGNATURE   0x53425355U     /* 'USBS' */
#define CBW_SIZE        31
#define CSW_SIZE        13

/* CSW Status */
#define CSW_STATUS_PASSED       0x00
#define CSW_STATUS_FAILED       0x01
#define CSW_STATUS_PHASE_ERROR  0x02

/* Class-specific requests */
#define USB_MSC_REQ_RESET       0xFF    /* Bulk-Only Mass Storage Reset */
#define USB_MSC_REQ_GET_MAX_LUN 0xFE

/*
 * ============================================================
 * BOT Structures (on-wire, packed)
 * ============================================================
 */

struct usb_msc_cbw {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;            /* Bit 7: 0=OUT, 1=IN */
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} __attribute__((packed));

struct usb_msc_csw {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} __attribute__((packed));

/*
 * ============================================================
 * Per-device State
 * ============================================================
 */

#define USB_MSC_MAX_DEVICES     4
/*
 * Bounce buffer is only used when the caller supplies a buffer that is
 * NOT in the kernel direct-mapped window (e.g. a user-mode buffer routed
 * here without remapping).  The fast path skips it entirely for kernel
 * pointers.  Sizing it at 64KB matches the direct-path chunk size so the
 * fall-back doesn't double the round-trip count for very large I/Os.
 */
#define USB_MSC_BOUNCE_SIZE     65536
/* Max bytes per Bulk transfer handed to the HCD.  This must not exceed the
 * smallest per-transfer limit of any host controller the request may land on:
 * EHCI bounces through a 20 KiB (5-page) buffer and rejects anything larger,
 * xHCI through 64 KiB; UHCI streams arbitrary lengths as max-packet TDs.  A
 * larger chunk simply failed outright on EHCI/xHCI, so large I/O never worked
 * on USB2/USB3 storage.  16 KiB is block-aligned (32 * 512) and clears the
 * 20 KiB EHCI ceiling with margin; bigger transfers are split across the loop
 * below.  Submit_lock serializes everything, so concurrent control transfers
 * can't race for TDs. [DRV-16] */
#define USB_MSC_MAX_XFER        16384
#define USB_MSC_KERN_BASE       0xC0000000U

typedef struct usb_msc_dev {
    usb_device_t    *udev;
    usb_endpoint_t  *ep_in;         /* Bulk-IN endpoint */
    usb_endpoint_t  *ep_out;        /* Bulk-OUT endpoint */
    scsi_link_t      scsi_link;
    mutex_t          lock;          /* Transfer serialization lock */
    uint32_t         tag;           /* Incrementing CBW tag */
    uint8_t          max_lun;
    uint8_t          active;

    /* DMA-safe buffers for BOT protocol (avoid stack DMA) */
    struct usb_msc_cbw *cbw;        /* DMA-coherent CBW buffer */
    struct usb_msc_csw *csw;        /* DMA-coherent CSW buffer */
    void               *bounce_buf; /* DMA-coherent bounce buffer */
    dma_addr_t          cbw_dma;
    dma_addr_t          csw_dma;
    dma_addr_t          bounce_dma;
} usb_msc_dev_t;

static usb_msc_dev_t msc_devices[USB_MSC_MAX_DEVICES];

/*
 * ============================================================
 * BOT Reset Recovery
 * ============================================================
 */

static int usb_msc_reset_recovery(usb_msc_dev_t *msc)
{
    int ret;

    /* Bulk-Only Mass Storage Reset: class-specific request 0xFF */
    ret = usb_control_transfer(msc->udev,
                               USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                               USB_MSC_REQ_RESET,
                               0, 0, NULL, 0);
    if (ret != USB_XFER_OK)
        kprintf("usb_msc: reset request failed (%d)\n", ret);

    /* Clear HALT on both bulk endpoints */
    usb_clear_halt(msc->udev, msc->ep_in);
    usb_clear_halt(msc->udev, msc->ep_out);

    return ret;
}

/*
 * ============================================================
 * BOT Transfer: CBW → Data → CSW
 * ============================================================
 */

static int usb_msc_bot_transfer(usb_msc_dev_t *msc, uint8_t lun,
                                uint8_t *cdb, uint8_t cdb_len,
                                void *data, uint32_t data_len,
                                int is_read, uint32_t *residue)
{
    struct usb_msc_cbw *cbw;
    struct usb_msc_csw *csw;
    uint32_t actual;
    int ret;
    int status = -1;

    mutex_lock(&msc->lock);

    /* Re-check under the lock: a concurrent detach (USB unplug) may have
     * quiesced the device and freed the DMA buffers after our caller's
     * lock-less active check.  The detach holds this same lock while it tears
     * down, so once we hold it the state is stable. [DRV-01] */
    if (!msc->active || !msc->udev || !msc->cbw || !msc->csw) {
        mutex_unlock(&msc->lock);
        return -1;
    }
    cbw = msc->cbw;
    csw = msc->csw;

    /* Build CBW in DMA-coherent buffer */
    memset(cbw, 0, sizeof(*cbw));
    cbw->dCBWSignature = CBW_SIGNATURE;
    cbw->dCBWTag = ++msc->tag;
    cbw->dCBWDataTransferLength = data_len;
    cbw->bmCBWFlags = is_read ? 0x80 : 0x00;
    cbw->bCBWLUN = lun;
    /* Clamp BEFORE recording it: the wire field used to carry the unclamped
     * length while only 16 bytes were copied.  BOT restricts bCBWCBLength to
     * 1-16 and a device receiving more must treat the CBW as invalid, stalling
     * both endpoints and dragging us through reset recovery. [USB-15] */
    if (cdb_len > 16)
        cdb_len = 16;
    cbw->bCBWCBLength = cdb_len;
    memcpy(cbw->CBWCB, cdb, cdb_len);

    /* Send CBW via Bulk-OUT */
    ret = usb_bulk_transfer(msc->udev, msc->ep_out,
                            cbw, CBW_SIZE, &actual);
    if (ret != USB_XFER_OK) {
        kprintf("usb_msc: CBW phase failed (ret=%d)\n", ret);
        usb_msc_reset_recovery(msc);
        goto cleanup;
    }

    /* Data phase (if any).
     *
     * Fast path: caller's buffer is in the kernel direct-mapped window
     * (kmalloc / pmm_alloc_contiguous output), which is the case for all
     * SCSI mid-layer requests originating from disk I/O.  Hand the buffer
     * straight to the HCD; dma_map_single will return its physical address
     * with no copy.  We still chunk at USB_MSC_MAX_XFER to bound TD
     * pool usage in UHCI (each Bulk transfer allocates max_packet-sized
     * TDs, e.g. 64KB / 64B = 1024 TDs out of UHCI_MAX_TDS=2048).
     *
     * Fall-back path: caller's buffer is outside the direct map.  Use the
     * pre-allocated DMA bounce buffer with the original chunked memcpy.
     * This keeps correctness for unusual callers without penalising the
     * common case.
     */
    if (data && data_len > 0) {
        usb_endpoint_t *data_ep = is_read ? msc->ep_in : msc->ep_out;
        uint32_t remaining = data_len;
        uint8_t *ptr = (uint8_t *)data;
        /*
         * The direct-map test needs both bounds.  Testing only ">= KERN_BASE"
         * accepts every kernel address, but the direct map stops at
         * PMM_DIRECTMAP_PHYS_LIMIT -- kernel VAs above that are kmem/vmalloc
         * mappings whose physical pages are neither contiguous nor at
         * (va - PMM_PHYS_VIRT_BASE).  Handing one to the HCD gave DMA the
         * wrong physical address, and touching it here could fault, inside
         * the msc->lock region.  On a machine whose root filesystem is this
         * device that fault pages in through the same path and deadlocks on
         * the lock we already hold: usb_msc_bot_transfer re-entered from
         * usb_msc_bot_transfer, same thread.
         *
         * pmm_virt_is_direct_mapped() is the predicate that was always meant
         * here; anything else takes the bounce-buffer path, which is correct
         * for kmem buffers and merely slower.
         */
        int direct = pmm_virt_is_direct_mapped((uintptr_t)data);

        if (direct) {
            while (remaining > 0) {
                uint32_t chunk_size = (remaining > USB_MSC_MAX_XFER) ?
                                      USB_MSC_MAX_XFER : remaining;

                ret = usb_bulk_transfer(msc->udev, data_ep,
                                        ptr, chunk_size, &actual);

                if (ret == USB_XFER_STALL) {
                    usb_clear_halt(msc->udev, data_ep);
                    break;
                } else if (ret != USB_XFER_OK && ret != USB_XFER_SHORT) {
                    kprintf("usb_msc: data phase failed (%d)\n", ret);
                    usb_msc_reset_recovery(msc);
                    goto cleanup;
                }

                if (actual > chunk_size)
                    actual = chunk_size;   /* never walk past the request [USB-17] */
                ptr += actual;
                remaining -= actual;

                if (actual < chunk_size)
                    break;  /* short packet ends transfer */
            }
        } else {
            while (remaining > 0) {
                /* Cap the bounce chunk at the same HCD-safe limit; the bounce
                 * buffer itself is larger but the transfer must still fit the
                 * controller's per-transfer ceiling. [DRV-16] */
                uint32_t chunk_size = (remaining > USB_MSC_MAX_XFER) ?
                                      USB_MSC_MAX_XFER : remaining;

                if (!is_read)
                    memcpy(msc->bounce_buf, ptr, chunk_size);

                ret = usb_bulk_transfer(msc->udev, data_ep,
                                        msc->bounce_buf, chunk_size, &actual);

                if (is_read && (ret == USB_XFER_OK || ret == USB_XFER_SHORT))
                    memcpy(ptr, msc->bounce_buf, actual);

                if (ret == USB_XFER_STALL) {
                    usb_clear_halt(msc->udev, data_ep);
                    break;
                } else if (ret != USB_XFER_OK && ret != USB_XFER_SHORT) {
                    kprintf("usb_msc: data phase failed (%d)\n", ret);
                    usb_msc_reset_recovery(msc);
                    goto cleanup;
                }

                if (actual > chunk_size)
                    actual = chunk_size;   /* [USB-17] */
                ptr += actual;
                remaining -= actual;

                if (actual < chunk_size)
                    break;
            }
        }
    }

    /* Receive CSW via Bulk-IN (into DMA-coherent buffer) */
    ret = usb_bulk_transfer(msc->udev, msc->ep_in,
                            csw, CSW_SIZE, &actual);
    if (ret == USB_XFER_STALL) {
        /* Stall on CSW — clear halt and retry once */
        usb_clear_halt(msc->udev, msc->ep_in);
        ret = usb_bulk_transfer(msc->udev, msc->ep_in,
                                csw, CSW_SIZE, &actual);
    }

    if (ret != USB_XFER_OK || actual < CSW_SIZE) {
        kprintf("usb_msc: CSW phase failed (%d, actual=%u)\n", ret, actual);
        usb_msc_reset_recovery(msc);
        goto cleanup;
    }

    /* Validate CSW */
    if (csw->dCSWSignature != CSW_SIGNATURE) {
        kprintf("usb_msc: invalid CSW signature 0x%08x\n", csw->dCSWSignature);
        usb_msc_reset_recovery(msc);
        goto cleanup;
    }

    if (csw->dCSWTag != cbw->dCBWTag) {
        kprintf("usb_msc: CSW tag mismatch (expected %u, got %u)\n",
                cbw->dCBWTag, csw->dCSWTag);
        usb_msc_reset_recovery(msc);
        goto cleanup;
    }

    /* BOT requires dCSWDataResidue <= dCBWDataTransferLength.  A device
     * reporting more makes the caller's (data_len - residue) underflow into a
     * ~4 GB transfer count, so clamp rather than trust. [USB-16] */
    if (csw->dCSWDataResidue > data_len) {
        kprintf("usb_msc: CSW residue %u exceeds transfer length %u; clamping\n",
                csw->dCSWDataResidue, data_len);
        csw->dCSWDataResidue = data_len;
    }
    if (residue)
        *residue = csw->dCSWDataResidue;

    if (csw->bCSWStatus != CSW_STATUS_PASSED || csw->dCSWDataResidue > 0) {
        kprintf("usb_msc: BOT xfer: op=0x%02x, len=%u, status=%u, residue=%u\n",
                cbw->CBWCB[0], cbw->dCBWDataTransferLength, 
                csw->bCSWStatus, csw->dCSWDataResidue);
    }

    switch (csw->bCSWStatus) {
    case CSW_STATUS_PASSED:
        status = 0;
        break;

    case CSW_STATUS_FAILED:
        /* Command failed — caller should issue REQUEST SENSE */
        status = 1;
        break;

    case CSW_STATUS_PHASE_ERROR:
        /* Phase error — perform reset recovery */
        usb_msc_reset_recovery(msc);
        status = -1;
        break;

    default:
        kprintf("usb_msc: unknown CSW status %u\n", csw->bCSWStatus);
        usb_msc_reset_recovery(msc);
        status = -1;
        break;
    }

cleanup:
    mutex_unlock(&msc->lock);
    return status;
}

/*
 * ============================================================
 * SCSI Mid-Layer Transport Callbacks
 * ============================================================
 */

static int usb_msc_scsi_execute(scsi_link_t *link, scsi_request_t *req)
{
    usb_msc_dev_t *msc = link->priv;
    uint32_t residue = 0;
    int is_read;
    int ret;

    if (!msc || !msc->active || !req)
        return -1;

    is_read = (req->flags & SCSI_REQ_READ) ? 1 : 0;

    ret = usb_msc_bot_transfer(msc, (uint8_t)req->device->lun,
                               req->cdb, req->cdb_len,
                               req->data, req->data_len,
                               is_read, &residue);

    if (ret == 0) {
        /* Success */
        req->status = SCSI_STATUS_GOOD;
        req->data_xfer = req->data_len - residue;
        return 0;
    } else if (ret == 1) {
        /* Command failed — auto-request sense if allowed */
        req->status = SCSI_STATUS_CHECK_CONDITION;

        if (!(req->flags & SCSI_REQ_NO_SENSE) && req->cdb[0] != SCSI_CMD_REQUEST_SENSE) {
            uint8_t sense_cdb[6] = {
                SCSI_CMD_REQUEST_SENSE, 0, 0, 0, 18, 0
            };
            uint8_t sense_buf[18];
            uint32_t sr;

            if (usb_msc_bot_transfer(msc, (uint8_t)req->device->lun,
                                     sense_cdb, 6,
                                     sense_buf, 18, 1, &sr) == 0) {
                uint8_t copy_len = 18;
                if (copy_len > SCSI_MAX_SENSE_LEN)
                    copy_len = SCSI_MAX_SENSE_LEN;
                memcpy(req->sense, sense_buf, copy_len);
                req->sense_len = copy_len;
            }
        }

        return -1;
    }

    /* Transport error */
    req->status = SCSI_STATUS_CHECK_CONDITION;
    req->error = -1;
    return -1;
}

static int usb_msc_scsi_reset_device(scsi_link_t *link, scsi_device_t *dev)
{
    usb_msc_dev_t *msc = link->priv;
    (void)dev;

    if (!msc)
        return -1;

    return usb_msc_reset_recovery(msc);
}

static int usb_msc_scsi_reset_bus(scsi_link_t *link)
{
    (void)link;
    return 0;
}

/*
 * ============================================================
 * GET MAX LUN
 * ============================================================
 */

static uint8_t usb_msc_get_max_lun(usb_device_t *udev)
{
    uint8_t max_lun = 0;
    int ret;

    ret = usb_control_transfer(udev,
                               USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                               USB_MSC_REQ_GET_MAX_LUN,
                               0, 0, &max_lun, 1);
    if (ret != USB_XFER_OK) {
        /* Many devices STALL this — max_lun = 0 (single LUN) */
        max_lun = 0;
    }

    return max_lun;
}

/*
 * ============================================================
 * USB Class Driver Interface
 * ============================================================
 */

static int usb_msc_probe(usb_device_t *dev)
{
    /* Accept Mass Storage / SCSI / BOT devices */
    if (dev->if_class == USB_CLASS_MASS_STORAGE &&
        dev->if_subclass == USB_MSC_SUBCLASS_SCSI &&
        dev->if_protocol == USB_MSC_PROTO_BOT)
        return 0;

    return -1;
}

static int usb_msc_attach(usb_device_t *dev)
{
    usb_msc_dev_t *msc = NULL;
    usb_endpoint_t *ep_in, *ep_out;
    int slot;

    /* Find Bulk-IN and Bulk-OUT endpoints */
    ep_in = usb_find_endpoint(dev, USB_EP_TYPE_BULK, USB_EP_DIR_IN);
    ep_out = usb_find_endpoint(dev, USB_EP_TYPE_BULK, USB_EP_DIR_OUT);

    if (!ep_in || !ep_out) {
        kprintf("usb_msc: missing bulk endpoints\n");
        return -1;
    }

    /* Allocate device slot */
    for (slot = 0; slot < USB_MSC_MAX_DEVICES; slot++) {
        if (!msc_devices[slot].active) {
            msc = &msc_devices[slot];
            break;
        }
    }
    if (!msc) {
        kprintf("usb_msc: no free device slots\n");
        return -1;
    }

    memset(msc, 0, sizeof(*msc));
    msc->udev = dev;
    msc->ep_in = ep_in;
    msc->ep_out = ep_out;
    msc->tag = 0;
    mutex_init(&msc->lock, "usb_msc");

    /* Allocate DMA-coherent buffers for CBW/CSW (avoid stack DMA) */
    msc->cbw = dma_alloc_coherent(sizeof(struct usb_msc_cbw), &msc->cbw_dma);
    msc->csw = dma_alloc_coherent(sizeof(struct usb_msc_csw), &msc->csw_dma);
    msc->bounce_buf = dma_alloc_coherent(USB_MSC_BOUNCE_SIZE, &msc->bounce_dma);
    if (!msc->cbw || !msc->csw || !msc->bounce_buf) {
        kprintf("usb_msc: failed to allocate DMA buffers\n");
        /* Free what we got and zero the pointers so a future attach on
         * the same slot can't double-free a stale value. */
        if (msc->cbw) {
            dma_free_coherent(msc->cbw, sizeof(struct usb_msc_cbw));
            msc->cbw = NULL;
        }
        if (msc->csw) {
            dma_free_coherent(msc->csw, sizeof(struct usb_msc_csw));
            msc->csw = NULL;
        }
        if (msc->bounce_buf) {
            dma_free_coherent(msc->bounce_buf, USB_MSC_BOUNCE_SIZE);
            msc->bounce_buf = NULL;
        }
        return -1;
    }

    msc->active = 1;

    /* Query max LUN */
    msc->max_lun = usb_msc_get_max_lun(dev);

    /* Register with SCSI mid-layer */
    memset(&msc->scsi_link, 0, sizeof(scsi_link_t));
    snprintf(msc->scsi_link.name, sizeof(msc->scsi_link.name), "usb%d", slot);
    msc->scsi_link.bus_id = 2 + (uint8_t)slot;   /* After ATAPI(0) and AHCI(1) */
    msc->scsi_link.max_targets = 1;
    msc->scsi_link.max_luns = msc->max_lun + 1;
    msc->scsi_link.adapter_queue_depth = 1;    /* BOT is single-command */
    msc->scsi_link.execute = usb_msc_scsi_execute;
    msc->scsi_link.reset_device = usb_msc_scsi_reset_device;
    msc->scsi_link.reset_bus = usb_msc_scsi_reset_bus;
    msc->scsi_link.priv = msc;

    if (scsi_register_link(&msc->scsi_link) != 0) {
        kprintf("usb_msc: failed to register SCSI link\n");
        msc->active = 0;
        return -1;
    }

    dev->driver_data = msc;

    /*
    kprintf("usb_msc: attached %04x:%04x (max_lun=%u) -> scsi bus %u\n",
            dev->vendor_id, dev->product_id,
            msc->max_lun, msc->scsi_link.bus_id);
    */

    /* Scan for SCSI devices on this link */
    scsi_scan_bus(&msc->scsi_link, msc->scsi_link.bus_id);

    return 0;
}

static void usb_msc_detach(usb_device_t *dev)
{
    usb_msc_dev_t *msc = dev->driver_data;

    if (!msc || !msc->active)
        return;

    /* Unregister from the SCSI mid-layer first: this force-unmounts and drains
     * outstanding requests, and stops new ones from being dispatched.  These
     * final requests are real BOT transfers that still need active == 1 and the
     * DMA buffers, so they must run before we tear anything down. */
    scsi_unregister_link(&msc->scsi_link);

    /* Quiesce: take the transfer lock so any BOT transfer already past its
     * active check completes before we free the DMA buffers and drop udev.  A
     * transfer that blocked on the lock re-checks active/udev after acquiring
     * it and bails, so it can't touch the freed buffers. [DRV-01] */
    mutex_lock(&msc->lock);
    msc->active = 0;
    msc->udev = NULL;

    /* Free DMA-coherent BOT buffers */
    if (msc->cbw)
        dma_free_coherent(msc->cbw, sizeof(struct usb_msc_cbw));
    if (msc->csw)
        dma_free_coherent(msc->csw, sizeof(struct usb_msc_csw));
    if (msc->bounce_buf)
        dma_free_coherent(msc->bounce_buf, USB_MSC_BOUNCE_SIZE);
    msc->cbw = NULL;
    msc->csw = NULL;
    msc->bounce_buf = NULL;
    mutex_unlock(&msc->lock);

    dev->driver_data = NULL;

    kprintf("usb_msc: detached device\n");
}

/*
 * ============================================================
 * Class Driver Registration
 * ============================================================
 */

static usb_class_driver_t usb_msc_driver = {
    .name         = "usb-storage",
    .if_class     = USB_CLASS_MASS_STORAGE,
    .if_subclass  = USB_MSC_SUBCLASS_SCSI,
    .if_protocol  = USB_MSC_PROTO_BOT,
    .probe        = usb_msc_probe,
    .attach       = usb_msc_attach,
    .detach       = usb_msc_detach,
};

/*
 * Called from usb_init() or early in driver setup to register
 * the mass storage class driver.
 */
void usb_msc_init(void)
{
    memset(msc_devices, 0, sizeof(msc_devices));
    usb_register_class_driver(&usb_msc_driver);
}
