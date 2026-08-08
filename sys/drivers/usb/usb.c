/*
 * usb.c - USB Core Framework
 *
 * Device enumeration, descriptor parsing, control/bulk transfer wrappers,
 * and class driver matching for the Substrate kernel USB subsystem.
 */

#include <stdio.h>
#include <string.h>

#include <drivers/usb/usb.h>
#include <kern/bus.h>
#include <kern/console.h>
#include <kern/device.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <sys/kthread.h>
#include <sys/lock.h>
#include <vm/vm_kmem.h>

/* The USB bus in the kernel device tree (/proc/devtree).  Enumerated devices
 * are published here so userspace can see them alongside pci/isa. */
struct bus_type usb_bus_type = {
    .name = "usb",
};

/* Publish an enumerated USB device into /proc/devtree (bus "usb").
 * Best-effort: a failed registration just omits the device. */
static void usb_publish_device(usb_device_t *dev)
{
    char name[24];
    struct device *bd;
    uint8_t cls;

    snprintf(name, sizeof(name), "usb%u", dev->address);
    bd = device_create(name, NULL);
    if (bd == NULL) {
        return;
    }
    cls = dev->dev_desc.bDeviceClass;
    if (cls == 0) {
        cls = dev->if_class;     /* class is defined at the interface level */
    }
    bd->vendor_id = dev->vendor_id;
    bd->device_id = dev->product_id;
    bd->class = cls;
    bd->subclass = dev->if_subclass;
    bd->progif = dev->if_protocol;
    if (device_register(bd, &usb_bus_type) != 0) {
        device_put(bd);
    } else {
        /* Remember the node so a later disconnect can remove it (else the
         * /proc/devtree entry leaks one device per hot-plug cycle). [DRV-20] */
        dev->devtree_dev = bd;
    }
    usbdevfs_publish(dev);
}

/*
 * ============================================================
 * Global State
 * ============================================================
 */

static usb_device_t *usb_devices[USB_MAX_DEVICES];
/* Bitmap of allocated USB device addresses (1..127).  Bit 0 is unused
 * since address 0 is the default-address pseudo-state.  Replaces a
 * monotonic counter that would have run out of addresses after ~127
 * cumulative hot-plug attach cycles. */
static uint32_t      usb_addr_bitmap[4];   /* 128 bits */

/*
 * Guards usb_devices[] and usb_addr_bitmap[].  The hot-plug kthread walks the
 * table while enumeration adds to it and class-driver detach paths remove from
 * it, and none of that was serialised. [USB-19]
 *
 * Held only across the table and bitmap operations themselves -- never across
 * a control transfer or a class driver's probe/attach/detach, which sleep and
 * take locks of their own.  usb_disconnect_device() therefore snapshots a
 * device's children under the lock and recurses with it dropped.
 */
static mutex_t usb_devtab_lock;

/* Caller holds usb_devtab_lock. */
static inline int    usb_addr_alloc_locked(void) {
    for (int a = 1; a < 128; a++) {
        if (!(usb_addr_bitmap[a >> 5] & (1U << (a & 31)))) {
            usb_addr_bitmap[a >> 5] |= (1U << (a & 31));
            return a;
        }
    }
    return -1;
}
static inline int    usb_addr_alloc(void) {
    int a;
    mutex_lock(&usb_devtab_lock);
    a = usb_addr_alloc_locked();
    mutex_unlock(&usb_devtab_lock);
    return a;
}
static inline void   usb_addr_free(uint8_t a) {
    if (a == 0 || a >= 128) return;
    mutex_lock(&usb_devtab_lock);
    usb_addr_bitmap[a >> 5] &= ~(1U << (a & 31));
    mutex_unlock(&usb_devtab_lock);
}

static usb_hcd_t           *usb_hcd_list;
static usb_class_driver_t  *usb_class_drivers;

/*
 * ============================================================
 * HCD Registration
 * ============================================================
 */

int usb_register_hcd(usb_hcd_t *hcd)
{
    if (!hcd || !hcd->submit)
        return -1;

    hcd->next = usb_hcd_list;
    usb_hcd_list = hcd;

    kprintf("usb: registered HCD '%s' with %u port(s)\n",
            hcd->name, hcd->nports);
    return 0;
}

void usb_unregister_hcd(usb_hcd_t *hcd)
{
    usb_hcd_t **pp;

    if (!hcd)
        return;

    for (pp = &usb_hcd_list; *pp; pp = &(*pp)->next) {
        if (*pp == hcd) {
            *pp = hcd->next;
            break;
        }
    }
}

/*
 * ============================================================
 * Class Driver Registration
 * ============================================================
 */

int usb_register_class_driver(usb_class_driver_t *drv)
{
    if (!drv)
        return -1;

    drv->next = usb_class_drivers;
    usb_class_drivers = drv;

    kprintf("usb: registered class driver '%s'\n", drv->name);
    return 0;
}

void usb_unregister_class_driver(usb_class_driver_t *drv)
{
    usb_class_driver_t **pp;

    if (!drv)
        return;

    for (pp = &usb_class_drivers; *pp; pp = &(*pp)->next) {
        if (*pp == drv) {
            *pp = drv->next;
            break;
        }
    }
}

/*
 * ============================================================
 * Device Allocation
 * ============================================================
 */

usb_device_t *usb_alloc_device(usb_hcd_t *hcd)
{
    usb_device_t *dev;
    int slot;

    dev = kzalloc(sizeof(usb_device_t));
    if (!dev)
        return NULL;

    /* Claim a slot under the lock so two enumerations cannot pick the same
     * one; the allocation above is done first so it stays outside. */
    mutex_lock(&usb_devtab_lock);
    for (slot = 0; slot < USB_MAX_DEVICES; slot++) {
        if (!usb_devices[slot])
            break;
    }
    if (slot >= USB_MAX_DEVICES) {
        mutex_unlock(&usb_devtab_lock);
        kfree(dev, sizeof(usb_device_t));
        return NULL;
    }
    usb_devices[slot] = dev;
    mutex_unlock(&usb_devtab_lock);

    dev->hcd = hcd;
    dev->slot = (uint8_t)slot;
    dev->ep0.address = 0;
    dev->ep0.type = USB_EP_TYPE_CONTROL;
    dev->ep0.max_packet = 8;    /* Default until descriptor read */
    dev->ep0.mult = 1;          /* control endpoints are never high-bandwidth */
    dev->ep0.toggle = 0;

    return dev;
}

void usb_free_device(usb_device_t *dev)
{
    if (!dev)
        return;

    /* Return the USB address to the pool so a future enumeration can
     * reuse it.  Without this the system runs out of addresses after
     * ~127 cumulative attach cycles. */
    if (dev->address)
        usb_addr_free(dev->address);

    mutex_lock(&usb_devtab_lock);
    if (dev->slot < USB_MAX_DEVICES && usb_devices[dev->slot] == dev)
        usb_devices[dev->slot] = NULL;
    mutex_unlock(&usb_devtab_lock);

    if (dev->config_data) {
        kfree(dev->config_data, dev->config_len);
        dev->config_data = NULL;
        dev->config_len = 0;
    }

    kfree(dev, sizeof(usb_device_t));
}

/*
 * ============================================================
 * Transfer API (synchronous wrappers)
 * ============================================================
 */

int usb_control_transfer_actual(usb_device_t *dev,
                                uint8_t bmRequestType, uint8_t bRequest,
                                uint16_t wValue, uint16_t wIndex,
                                void *data, uint16_t wLength,
                                uint32_t *actual_length)
{
    usb_transfer_t xfer;
    int ret;

    if (actual_length)
        *actual_length = 0;
    if (!dev || !dev->hcd || !dev->hcd->submit)
        return USB_XFER_ERROR;

    memset(&xfer, 0, sizeof(xfer));
    xfer.dev = dev;
    xfer.ep = &dev->ep0;
    xfer.is_control = 1;
    xfer.data = data;
    xfer.length = wLength;

    xfer.setup.bmRequestType = bmRequestType;
    xfer.setup.bRequest = bRequest;
    xfer.setup.wValue = wValue;
    xfer.setup.wIndex = wIndex;
    xfer.setup.wLength = wLength;

    ret = dev->hcd->submit(dev->hcd, &xfer);

    if (actual_length)
        *actual_length = xfer.actual_length;
    return ret;
}

int usb_control_transfer(usb_device_t *dev,
                         uint8_t bmRequestType, uint8_t bRequest,
                         uint16_t wValue, uint16_t wIndex,
                         void *data, uint16_t wLength)
{
    return usb_control_transfer_actual(dev, bmRequestType, bRequest,
                                       wValue, wIndex, data, wLength, NULL);
}

int usb_interrupt_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                           void *data, uint32_t length,
                           uint32_t *actual_length, uint32_t timeout_ms)
{
    usb_transfer_t xfer;
    int ret;

    if (!dev || !dev->hcd || !dev->hcd->submit || !ep)
        return USB_XFER_ERROR;

    memset(&xfer, 0, sizeof(xfer));
    xfer.dev = dev;
    xfer.ep = ep;
    xfer.is_control = 0;
    xfer.data = data;
    xfer.length = length;
    xfer.timeout_ms = timeout_ms;

    ret = dev->hcd->submit(dev->hcd, &xfer);

    if (actual_length)
        *actual_length = xfer.actual_length;

    return ret;
}

int usb_bulk_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                      void *data, uint32_t length,
                      uint32_t *actual_length)
{
    usb_transfer_t xfer;
    int ret;

    if (!dev || !dev->hcd || !dev->hcd->submit || !ep)
        return USB_XFER_ERROR;

    memset(&xfer, 0, sizeof(xfer));
    xfer.dev = dev;
    xfer.ep = ep;
    xfer.is_control = 0;
    xfer.data = data;
    xfer.length = length;

    ret = dev->hcd->submit(dev->hcd, &xfer);

    if (actual_length)
        *actual_length = xfer.actual_length;

    return ret;
}

uint16_t usb_frame_number(usb_device_t *dev)
{
    if (!dev || !dev->hcd || !dev->hcd->frame_number)
        return 0;
    return dev->hcd->frame_number(dev->hcd);
}

int usb_iso_schedule(usb_device_t *dev, usb_endpoint_t *ep, uint16_t frame,
                     uint32_t buf_phys, uint16_t len, void **handle)
{
    if (handle)
        *handle = NULL;
    if (!dev || !dev->hcd || !dev->hcd->iso_schedule || !ep)
        return USB_XFER_ERROR;
    return dev->hcd->iso_schedule(dev->hcd, dev, ep, frame, buf_phys, len, handle);
}

void usb_iso_reclaim(usb_device_t *dev, void *handle)
{
    if (!dev || !dev->hcd || !dev->hcd->iso_reclaim || !handle)
        return;
    dev->hcd->iso_reclaim(dev->hcd, handle);
}

void usb_iso_stop(usb_device_t *dev, usb_endpoint_t *ep)
{
    if (!dev || !ep || !dev->hcd || !dev->hcd->iso_stop)
        return;
    dev->hcd->iso_stop(dev->hcd, dev, ep);
}

int usb_iso_in_status(usb_device_t *dev, void *handle, uint32_t *out_len)
{
    if (out_len)
        *out_len = 0;
    if (!dev || !handle || !dev->hcd || !dev->hcd->iso_in_status)
        return -1;
    return dev->hcd->iso_in_status(dev->hcd, handle, out_len);
}

int usb_iso_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                     void *data, uint32_t length,
                     uint32_t *actual_length)
{
    usb_transfer_t xfer;
    int ret;

    if (!dev || !dev->hcd || !dev->hcd->submit || !ep)
        return USB_XFER_ERROR;

    /* The HCD submit path dispatches on ep->type, so an ISO-typed endpoint
     * routes to the controller's isochronous handler. */
    memset(&xfer, 0, sizeof(xfer));
    xfer.dev = dev;
    xfer.ep = ep;
    xfer.is_control = 0;
    xfer.data = data;
    xfer.length = length;

    ret = dev->hcd->submit(dev->hcd, &xfer);

    if (actual_length)
        *actual_length = xfer.actual_length;

    return ret;
}

/*
 * ============================================================
 * Standard Device Requests
 * ============================================================
 */

int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                       void *buf, uint16_t size)
{
    return usb_control_transfer(dev,
                                USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                                USB_REQ_GET_DESCRIPTOR,
                                (uint16_t)((type << 8) | index),
                                0,
                                buf, size);
}

int usb_get_string(usb_device_t *dev, uint8_t index, char *buf, uint16_t bufsize)
{
    if (!buf || bufsize == 0)
        return -1;
    buf[0] = '\0';
    if (index == 0)             /* index 0 is the LANGID table, not a string */
        return 0;

    /* wValue = (STRING << 8 | index); wIndex = LANGID.  0x0409 (US English) is
     * effectively universal and what QEMU and real devices answer to. */
    uint16_t wValue = (uint16_t)((USB_DT_STRING << 8) | index);

    /* Step 1: read only the 2-byte header (bLength, bDescriptorType) to learn
     * the descriptor's true length.  Over-reading EP0 (asking for 255 up front)
     * makes some HC/device state machines mishandle the short-packet
     * completion, which corrupts EP0 for later transfers -- so read exactly. */
    uint8_t hdr[2];
    int ret = usb_control_transfer(dev,
                                   USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                                   USB_REQ_GET_DESCRIPTOR, wValue, 0x0409, hdr, 2);
    if (ret != USB_XFER_OK || hdr[1] != USB_DT_STRING || hdr[0] < 2)
        return 0;

    /* Step 2: read exactly bLength bytes (header + UTF-16LE body). */
    uint8_t raw[256];
    uint16_t dlen = hdr[0];
    ret = usb_control_transfer(dev,
                               USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                               USB_REQ_GET_DESCRIPTOR, wValue, 0x0409, raw, dlen);
    if (ret != USB_XFER_OK && ret != USB_XFER_SHORT)
        return 0;

    /* Decode the UTF-16LE body (raw[2..dlen-1]) to ASCII. */
    uint16_t out = 0;
    for (uint16_t i = 2; i + 1 < dlen && out + 1 < bufsize; i += 2) {
        uint16_t u = (uint16_t)(raw[i] | (raw[i + 1] << 8));
        buf[out++] = (u >= 0x20 && u < 0x7f) ? (char)u : '?';
    }
    buf[out] = '\0';
    return out;
}

int usb_set_address(usb_device_t *dev, uint8_t address)
{
    int ret;

    ret = usb_control_transfer(dev,
                               USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                               USB_REQ_SET_ADDRESS,
                               address,
                               0, NULL, 0);
    if (ret == USB_XFER_OK)
        dev->address = address;

    return ret;
}

int usb_set_configuration(usb_device_t *dev, uint8_t config)
{
    int ret;

    ret = usb_control_transfer(dev,
                               USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE,
                               USB_REQ_SET_CONFIG,
                               config,
                               0, NULL, 0);
    if (ret == USB_XFER_OK) {
        dev->config_value = config;
        /*
         * SET_CONFIGURATION(0) *un*configures the device -- it returns to
         * Address state with no interfaces active.  Recording that as
         * configured told every later caller the device was ready when it was
         * not. [USB-22]
         */
        dev->configured = (config != 0);
    }

    return ret;
}

int usb_set_interface(usb_device_t *dev, uint8_t iface, uint8_t alt)
{
    /* Standard SET_INTERFACE: selects an alternate setting, which is how a
     * USB audio streaming interface switches from its zero-bandwidth alt 0 to
     * an alt that exposes the isochronous endpoint. */
    return usb_control_transfer(dev,
                                USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE,
                                USB_REQ_SET_INTERFACE,
                                alt,        /* wValue = alternate setting */
                                iface,      /* wIndex = interface number  */
                                NULL, 0);
}

int usb_clear_halt(usb_device_t *dev, usb_endpoint_t *ep)
{
    int ret;

    if (!ep)
        return USB_XFER_ERROR;

    ret = usb_control_transfer(dev,
                               USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT,
                               USB_REQ_CLEAR_FEATURE,
                               USB_FEATURE_HALT,
                               ep->address,
                               NULL, 0);
    if (ret == USB_XFER_OK)
        ep->toggle = 0;

    return ret;
}

/*
 * ============================================================
 * Endpoint Lookup
 * ============================================================
 */

usb_endpoint_t *usb_find_endpoint_iface(usb_device_t *dev, uint8_t ifnum,
                                        uint8_t alt, uint8_t type, uint8_t dir)
{
    if (!dev)
        return NULL;

    for (int i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];

        if (iface->number != ifnum || iface->alt_setting != alt)
            continue;

        for (int j = 0; j < iface->ep_count; j++) {
            int idx = iface->ep_first + j;
            if (idx >= dev->num_endpoints)
                break;
            usb_endpoint_t *ep = &dev->endpoints[idx];
            if ((ep->address & USB_EP_DIR_MASK) == dir && ep->type == type)
                return ep;
        }
    }
    return NULL;
}

usb_endpoint_t *usb_find_endpoint(usb_device_t *dev, uint8_t type, uint8_t dir)
{
    usb_endpoint_t *ep;

    if (!dev)
        return NULL;

    /*
     * Prefer an endpoint belonging to the interface this device is bound to.
     * Before interfaces were tracked, this searched every endpoint in the
     * whole configuration in descriptor order, so on a composite device a
     * driver could be handed an endpoint owned by an interface it does not
     * drive.
     */
    ep = usb_find_endpoint_iface(dev, dev->if_number, 0, type, dir);
    if (ep)
        return ep;

    /*
     * Fall back to the flat scan.  A device whose interface descriptors we
     * failed to record (more than USB_MAX_INTERFACES, or a malformed config)
     * still gets the old behaviour rather than no endpoint at all.
     */
    for (int i = 0; i < dev->num_endpoints; i++) {
        usb_endpoint_t *e = &dev->endpoints[i];
        if ((e->address & USB_EP_DIR_MASK) == dir && e->type == type)
            return e;
    }
    return NULL;
}

/*
 * ============================================================
 * Bus Topology
 * ============================================================
 *
 * dev->parent / dev->port already describe where a device sits; these turn
 * that chain into the forms the controllers need.  The walks are bounded by
 * USB_MAX_ENUM_DEPTH so a corrupted parent pointer cannot loop forever.
 */

uint8_t usb_root_port(const usb_device_t *dev)
{
    const usb_device_t *d = dev;
    int guard;

    if (!d)
        return 0;
    for (guard = 0; d->parent && guard < USB_MAX_ENUM_DEPTH; guard++)
        d = d->parent;
    return d->port;
}

uint32_t usb_route_string(const usb_device_t *dev)
{
    uint8_t ports[USB_MAX_ENUM_DEPTH];
    int n = 0;
    uint32_t route = 0;
    const usb_device_t *d = dev;

    if (!d)
        return 0;

    /* Collect the port at each tier, nearest-the-device first.  The device's
     * own port only counts when it has a parent: a root-port device routes to
     * 0, which is what tells the controller "this port's direct attachment". */
    for (; d->parent && n < USB_MAX_ENUM_DEPTH; d = d->parent)
        ports[n++] = d->port;

    /*
     * ports[] is nearest-the-device first; the route string is the opposite
     * order.  Tier 1 (bits 3:0) is the TOPMOST hop -- the downstream port on
     * the hub attached to the root port -- and each deeper tier occupies the
     * next nibble up (xHCI 1.2 s8.9.1 via USB3 s8.9; FreeBSD builds it the
     * same way, `route |= port << (4 * (depth - 1))`).
     *
     * This used to fold ports[0] -- the device's own port, the DEEPEST tier
     * -- into bits 3:0, i.e. the nibbles came out tier-reversed.  A single
     * hub has one nibble and nothing to reverse, which is why every one-deep
     * test passed; the first device behind a second hub handed the controller
     * a route through the wrong ports and its Address Device failed outright
     * (QEMU: TRB Error, the walk hits an empty port). [P5-06]
     */
    for (int i = 0; i < n; i++) {
        uint8_t p = ports[i];
        if (p > 15)
            p = 15;             /* xHCI 1.1 s4.5.2: >15 encodes as 15 */
        route |= (uint32_t)p << (4 * (n - 1 - i));
    }
    return route & 0xFFFFF;     /* 20 bits, five tiers */
}

usb_device_t *usb_tt_hub(const usb_device_t *dev, uint8_t *ttport)
{
    const usb_device_t *child;
    usb_device_t *hub;
    int guard;

    if (ttport)
        *ttport = 0;
    if (!dev || dev->speed == USB_SPEED_HIGH)
        return NULL;            /* high-speed devices need no translator */

    /* Walk up until a high-speed hub is found; the child we came through is
     * the branch occupying its downstream port. */
    child = dev;
    hub = dev->parent;
    for (guard = 0; hub && guard < USB_MAX_ENUM_DEPTH; guard++) {
        if (hub->speed == USB_SPEED_HIGH && hub->is_hub) {
            if (ttport)
                *ttport = child->port;
            return hub;
        }
        child = hub;
        hub = hub->parent;
    }
    return NULL;                /* on a root port, or no HS hub in the path */
}

void usb_set_hub(usb_device_t *dev, uint8_t nports, uint8_t ttt)
{
    if (!dev)
        return;
    dev->is_hub = 1;
    dev->hub_nports = nports;
    dev->hub_ttt = ttt & 0x3;
    if (dev->hcd && dev->hcd->set_hub)
        (void)dev->hcd->set_hub(dev->hcd, dev, nports, dev->hub_ttt);
}

/*
 * ============================================================
 * Descriptor Parsing
 * ============================================================
 */

static void usb_parse_config(usb_device_t *dev)
{
    uint8_t *ptr = dev->config_data;
    uint8_t *end = ptr + dev->config_len;
    uint8_t first_iface_seen = 0;
    usb_interface_t *cur_iface = NULL;   /* interface currently being filled */

    dev->num_endpoints = 0;
    dev->num_interfaces = 0;

    if (!ptr)
        return;

    while (ptr + 2 <= end) {
        uint8_t bLength = ptr[0];
        uint8_t bType = ptr[1];

        if (bLength < 2 || ptr + bLength > end)
            break;

        if (bType == USB_DT_INTERFACE && bLength >= 9) {
            struct usb_interface_descriptor *iface =
                (struct usb_interface_descriptor *)ptr;

            /*
             * Record every interface, alternate settings included, so each
             * endpoint below can be attributed to the interface that declared
             * it.  Endpoints are appended to the flat dev->endpoints[] array
             * in descriptor order, so an interface's endpoints are exactly the
             * ones added between its descriptor and the next one -- which is
             * what ep_first/ep_count capture.
             */
            if (dev->num_interfaces < USB_MAX_INTERFACES) {
                cur_iface = &dev->interfaces[dev->num_interfaces++];
                cur_iface->number      = iface->bInterfaceNumber;
                cur_iface->alt_setting = iface->bAlternateSetting;
                cur_iface->if_class    = iface->bInterfaceClass;
                cur_iface->if_subclass = iface->bInterfaceSubClass;
                cur_iface->if_protocol = iface->bInterfaceProtocol;
                cur_iface->ep_first    = dev->num_endpoints;
                cur_iface->ep_count    = 0;
            } else {
                /* Out of interface slots: stop attributing endpoints rather
                 * than attributing them to the wrong interface. */
                cur_iface = NULL;
            }

            /*
             * dev->if_* still defaults to the first interface so a device
             * with no matching driver reports something meaningful, but
             * usb_match_driver() overwrites these with whichever interface
             * actually binds.  It used to be first-interface-only, which is
             * why a composite device whose interface 0 was not the functional
             * one never matched any driver.
             */
            if (!first_iface_seen) {
                dev->if_number   = iface->bInterfaceNumber;
                dev->if_class    = iface->bInterfaceClass;
                dev->if_subclass = iface->bInterfaceSubClass;
                dev->if_protocol = iface->bInterfaceProtocol;
                first_iface_seen = 1;
            }
        }

        if (bType == USB_DT_ENDPOINT && bLength >= 7) {
            struct usb_endpoint_descriptor *ep_desc =
                (struct usb_endpoint_descriptor *)ptr;

            if (dev->num_endpoints < USB_MAX_ENDPOINTS) {
                uint8_t addr = ep_desc->bEndpointAddress;
                /* Reject duplicate endpoint addresses — a malicious
                 * device could otherwise list the same address twice
                 * with different attributes, leaving the cache in an
                 * inconsistent state for usb_find_endpoint(). */
                int duplicate = 0;
                for (uint8_t k = 0; k < dev->num_endpoints; k++) {
                    if (dev->endpoints[k].address == addr) {
                        duplicate = 1;
                        break;
                    }
                }
                if (!duplicate) {
                    usb_endpoint_t *ep = &dev->endpoints[dev->num_endpoints];
                    uint16_t wmps = ep_desc->wMaxPacketSize;

                    ep->address = addr;
                    ep->type = ep_desc->bmAttributes & USB_EP_TYPE_MASK;
                    /*
                     * Split the size from the transaction count.  Only
                     * high-speed (and above) interrupt/isochronous endpoints
                     * use bits 12:11; everywhere else they are zero, so the
                     * mask is a no-op and mult comes out 1. [USB-06]
                     */
                    ep->max_packet = wmps & USB_EP_MPS_MASK;
                    if (dev->speed == USB_SPEED_HIGH &&
                        (ep->type == USB_EP_TYPE_INTERRUPT ||
                         ep->type == USB_EP_TYPE_ISO)) {
                        ep->mult = (uint8_t)USB_EP_MPS_MULT(wmps);
                    } else {
                        ep->mult = 1;
                    }
                    ep->interval = ep_desc->bInterval;
                    ep->toggle = 0;
                    ep->max_streams = 0;
                    ep->max_burst = 0;
                    dev->num_endpoints++;
                    if (cur_iface)
                        cur_iface->ep_count++;
                }
            }
        }
        /* SuperSpeed endpoint companion follows its endpoint (USB 3.2
         * s9.6.7): bMaxBurst at byte 2 is packets-per-burst minus one and
         * applies to every transfer type; for a bulk EP, bmAttributes bits
         * 4:0 are additionally the MaxStreams exponent (0 = no streams). */
        if (bType == USB_DT_SS_EP_COMP && bLength >= 6 && dev->num_endpoints > 0) {
            usb_endpoint_t *ep = &dev->endpoints[dev->num_endpoints - 1];
            ep->max_burst = ptr[2] & 0x0F;         /* bMaxBurst, 0..15 */
            if (ep->type == USB_EP_TYPE_BULK)
                ep->max_streams = ptr[3] & 0x1F;   /* bmAttributes.MaxStreams */
        }

        ptr += bLength;
    }
}

int usb_bulk_stream_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                             uint16_t stream_id, void *data, uint32_t length,
                             uint32_t *actual_length)
{
    usb_transfer_t xfer;
    int ret;
    if (!dev || !dev->hcd || !dev->hcd->submit || !ep)
        return USB_XFER_ERROR;
    memset(&xfer, 0, sizeof(xfer));
    xfer.dev = dev;
    xfer.ep = ep;
    xfer.is_control = 0;
    xfer.data = data;
    xfer.length = length;
    xfer.stream_id = stream_id;
    ret = dev->hcd->submit(dev->hcd, &xfer);
    if (actual_length)
        *actual_length = xfer.actual_length;
    return ret;
}

/*
 * ============================================================
 * Class Driver Matching
 * ============================================================
 */

/*
 * Try to bind one driver to one interface.  Returns 1 if the device is now
 * bound.  dev->if_* is published before probe() so the driver sees the
 * interface being offered -- probe() and attach() read dev->if_class and
 * friends, and every USB_RECIP_INTERFACE control request needs
 * dev->if_number as its wIndex.
 */
static int usb_try_bind(usb_device_t *dev, usb_class_driver_t *drv,
                        const usb_interface_t *iface)
{
    uint8_t save_number   = dev->if_number;
    uint8_t save_class    = dev->if_class;
    uint8_t save_subclass = dev->if_subclass;
    uint8_t save_protocol = dev->if_protocol;

    uint8_t if_class = iface->if_class;
    if (if_class == 0)
        if_class = dev->dev_desc.bDeviceClass;

    if (drv->if_class != if_class)
        return 0;
    if (drv->if_subclass != 0xFF && drv->if_subclass != iface->if_subclass)
        return 0;
    if (drv->if_protocol != 0xFF && drv->if_protocol != iface->if_protocol)
        return 0;

    dev->if_number   = iface->number;
    dev->if_class    = iface->if_class;
    dev->if_subclass = iface->if_subclass;
    dev->if_protocol = iface->if_protocol;

    if (drv->probe && drv->probe(dev) != 0)
        goto restore;

    if (drv->attach) {
        dev->driver_data = NULL;
        if (drv->attach(dev) == 0) {
            dev->driver = drv;       /* remember for disconnect dispatch */
            kprintf("usb: device %u:%u interface %u bound to driver '%s'\n",
                    dev->hcd->hcd_index, dev->address,
                    iface->number, drv->name);
            return 1;
        }
        /* Defensive: if attach failed but left a dangling pointer, clear it
         * so a future driver match doesn't dereference freed memory. */
        dev->driver_data = NULL;
    }

restore:
    dev->if_number   = save_number;
    dev->if_class    = save_class;
    dev->if_subclass = save_subclass;
    dev->if_protocol = save_protocol;
    return 0;
}

static void usb_match_driver(usb_device_t *dev)
{
    usb_class_driver_t *drv;

    /*
     * Offer every interface (alternate setting 0 only -- we never issue
     * SET_INTERFACE during enumeration, so the alt-0 descriptors describe the
     * device as it is right now) to every driver.  This used to consider only
     * the first interface descriptor in the configuration, so a composite
     * device -- a keyboard with a separate consumer-control interface, a
     * headset, a dock -- was matched on whatever function happened to be
     * listed first and its other functions were unreachable.
     */
    for (int i = 0; i < dev->num_interfaces; i++) {
        usb_interface_t *iface = &dev->interfaces[i];

        if (iface->alt_setting != 0)
            continue;

        for (drv = usb_class_drivers; drv; drv = drv->next) {
            if (usb_try_bind(dev, drv, iface))
                return;
        }
    }

    /*
     * Nothing matched by interface.  Fall back to the device-descriptor class
     * for devices that declare their class at the device level and leave the
     * interface class zero, and for the degenerate case of a configuration we
     * could not parse any interface out of at all.
     */
    for (drv = usb_class_drivers; drv; drv = drv->next) {
        uint8_t dev_class = dev->if_class;
        if (dev_class == 0) dev_class = dev->dev_desc.bDeviceClass;

        if (drv->if_class != dev_class)
            continue;
        if (drv->if_subclass != 0xFF &&
            drv->if_subclass != dev->if_subclass)
            continue;
        if (drv->if_protocol != 0xFF &&
            drv->if_protocol != dev->if_protocol)
            continue;

        /* Try probe */
        if (drv->probe && drv->probe(dev) != 0)
            continue;

        /* Attach.  Each driver's attach() is responsible for cleaning
         * up its own partial state on failure (allocated buffers,
         * spawned threads, etc.) — but reset driver_data here so a
         * subsequent driver in the match list can't see the failed
         * driver's pointer. */
        if (drv->attach) {
            dev->driver_data = NULL;
            if (drv->attach(dev) == 0) {
                dev->driver = drv;   /* remember for disconnect dispatch */
                kprintf("usb: device %u:%u bound to driver '%s'\n",
                        dev->hcd->hcd_index, dev->address, drv->name);
                return;
            }
            /* Defensive: if attach failed but left a dangling pointer,
             * clear it so a future driver match doesn't dereference
             * freed memory. */
            dev->driver_data = NULL;
        }
    }

    kprintf("usb: no driver for device %04x:%04x class %02x/%02x/%02x\n",
            dev->vendor_id, dev->product_id,
            dev->if_class, dev->if_subclass, dev->if_protocol);
}

/*
 * ============================================================
 * Device Enumeration
 * ============================================================
 */

/* USB spec allows at most 7 tiers of hubs.  usb_enumerate_device →
 * hub attach → enumerate_ports → usb_enumerate_device is the recursion
 * shape; each frame carries ~600-1000 bytes of locals (device/config
 * descriptors, port-status struct, snprintf buffers).  An 8 KB kernel
 * stack puts us within margin of overflow at the deepest legal tree;
 * a malicious or buggy hub claiming to be deeper still would push us
 * over.  Bound it explicitly. */
static int usb_enum_depth = 0;

static int usb_enumerate_device_inner(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                                      usb_device_t *parent);

int usb_enumerate_device_parent(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                                usb_device_t *parent)
{
    if (usb_enum_depth >= USB_MAX_ENUM_DEPTH) {
        kprintf("usb: enumeration depth limit (%d) reached at port %u; refusing\n",
                USB_MAX_ENUM_DEPTH, port);
        return -1;
    }
    usb_enum_depth++;
    int ret = usb_enumerate_device_inner(hcd, port, speed, parent);
    usb_enum_depth--;
    return ret;
}

int usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed)
{
    /* Root-hub port: no parent hub. */
    return usb_enumerate_device_parent(hcd, port, speed, NULL);
}

/* Busy-wait; enumeration runs on the boot thread and on the hot-plug kthread,
 * and the waits here are short and infrequent. */
static void usb_delay_ms(uint32_t ms)
{
    uint64_t deadline = (uint64_t)get_uptime_ms() + ms;
    while ((uint64_t)get_uptime_ms() < deadline)
        __asm__ volatile("pause");
}

/*
 * Reset the port this device sits on, whichever kind of port that is.  A root
 * port is a register write the HCD owns; a downstream port is a hub class
 * request only the hub driver can issue.
 */
static int usb_enum_reset_port(usb_hcd_t *hcd, uint8_t port, usb_device_t *parent)
{
    if (parent)
        return usb_hub_reset_port(parent, port);
    if (hcd && hcd->port_reset)
        return hcd->port_reset(hcd, port);
    return -1;
}

static int usb_enumerate_device_inner(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                                      usb_device_t *parent)
{
    usb_device_t *dev;
    struct usb_device_descriptor dd;
    struct usb_config_descriptor cd;
    uint8_t addr;
    int ret;

    dev = usb_alloc_device(hcd);
    if (!dev)
        return -1;

    dev->port = port;
    dev->speed = speed;
    dev->parent = parent;   /* NULL for a root-hub port; the hub otherwise [DRV-04] */
    dev->address = 0;   /* Default address for initial communication */

    /* Set EP0 max packet size based on speed */
    dev->ep0.max_packet = (speed == USB_SPEED_LOW) ? 8 : 64;

    /*
     * Get the first 8 bytes of the device descriptor to learn the real EP0 max
     * packet size.  Retry: a device outside spec timing does not answer the
     * first request, and a single failure used to make the port permanently
     * unenumerable.  Re-reset the port every fourth attempt, which is what
     * shakes loose a device whose state machine is wedged rather than merely
     * slow.  Mirrors NetBSD usbd_new_device(). [USB-03]
     */
    ret = USB_XFER_ERROR;
    for (int attempt = 0; attempt < USB_ENUM_DESC_TRIES; attempt++) {
        ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dd, 8);
        if (ret == USB_XFER_OK)
            break;
        if (attempt + 1 == USB_ENUM_DESC_TRIES)
            break;                  /* out of tries: don't pay the last delay */
        usb_delay_ms(USB_ENUM_DESC_DELAY_MS);
        if ((attempt & 3) == 3)
            (void)usb_enum_reset_port(hcd, port, parent);
    }
    if (ret != USB_XFER_OK) {
        kprintf("usb: port %u: failed to get device descriptor after %d tries "
                "(initial, err=%d)\n", port, USB_ENUM_DESC_TRIES, ret);
        usb_free_device(dev);
        return -1;
    }

    /*
     * Validate what came back before trusting it.  A glitched read that returns
     * plausible garbage would otherwise set an EP0 packet size like 0x2A and
     * mis-frame every later control transfer -- which presents as a device that
     * "sometimes fails to enumerate" rather than as a bad read. [USB-13]
     */
    if (dd.bDescriptorType != USB_DT_DEVICE) {
        kprintf("usb: port %u: initial descriptor has type %u, not DEVICE\n",
                port, dd.bDescriptorType);
        usb_free_device(dev);
        return -1;
    }

    /*
     * For SuperSpeed, bMaxPacketSize0 is log2(size) and the only legal value
     * is 9, meaning 512 (USB 3.2 s9.6.1).  Taken literally it yields an EP0
     * packet size of 9. [USB-12]
     */
    if (speed == USB_SPEED_SUPER) {
        if (dd.bMaxPacketSize0 != 9) {
            kprintf("usb: port %u: SuperSpeed device reports EP0 exponent %u, "
                    "forcing 9\n", port, dd.bMaxPacketSize0);
            dd.bMaxPacketSize0 = 9;
        }
        dev->ep0.max_packet = 512;
    } else {
        dev->ep0.max_packet = dd.bMaxPacketSize0;
    }
    /*
     * Legal control-endpoint packet sizes are 8/16/32/64 (USB 2.0 s5.5.3), and
     * a high-speed device must use 64.  Anything else is a misread, so fall
     * back to the conservative 8 rather than framing transfers to a size the
     * device never agreed to.
     */
    if (speed == USB_SPEED_SUPER) {
        /* already resolved above */
    } else if (speed == USB_SPEED_HIGH && dev->ep0.max_packet != 64) {
        kprintf("usb: port %u: high-speed device reports EP0 max packet %u, "
                "forcing 64\n", port, dev->ep0.max_packet);
        dev->ep0.max_packet = 64;
    } else if (dev->ep0.max_packet != 8 && dev->ep0.max_packet != 16 &&
               dev->ep0.max_packet != 32 && dev->ep0.max_packet != 64) {
        kprintf("usb: port %u: implausible EP0 max packet %u, using 8\n",
                port, dev->ep0.max_packet);
        dev->ep0.max_packet = 8;
    }

    /*
     * Tell the controller the real EP0 packet size.  xHCI baked the core's
     * guess into the endpoint context when the slot was addressed, so a
     * full-speed device with an 8-byte EP0 would otherwise be driven with a
     * context claiming 64. [USB-11]
     */
    if (hcd->set_ep0_mps)
        (void)hcd->set_ep0_mps(hcd, dev, dev->ep0.max_packet);

    /*
     * Reset the port once more before addressing the device.  Windows does
     * this and devices are tuned against the Windows sequence, so ones that
     * depend on it exist in quantity; NetBSD copies the behaviour for the same
     * reason.  The device stays in Default state at address 0 either way, so
     * nothing learned above is invalidated.  TRSTRCY is 10 ms. [USB-04]
     */
    (void)usb_enum_reset_port(hcd, port, parent);
    usb_delay_ms(10);

    /* Assign unique address from the bitmap; address is freed on detach
     * via usb_free_device, so hot-plug cycles don't leak addresses. */
    {
        int a = usb_addr_alloc();
        if (a < 0) {
            kprintf("usb: address space exhausted\n");
            usb_free_device(dev);
            return -1;
        }
        addr = (uint8_t)a;
    }

    ret = usb_set_address(dev, addr);
    if (ret != USB_XFER_OK) {
        kprintf("usb: port %u: SET_ADDRESS failed (err=%d)\n", port, ret);
        /*
         * Return the address by hand.  usb_free_device() only frees it when
         * dev->address is set, and usb_set_address() only sets that on
         * success -- so a failure here used to burn one of the 127 addresses
         * for the rest of the boot.  A machine with a few ports that report a
         * device they cannot enumerate walks the space down and eventually
         * hits "address space exhausted". [USB-07]
         */
        usb_addr_free(addr);
        usb_free_device(dev);
        return -1;
    }

    /* Wait for address to take effect (USB spec requires >= 2ms / one SOF) */
    {
        uint64_t addr_deadline = (uint64_t)get_uptime_ms() + 10;
        while ((uint64_t)get_uptime_ms() < addr_deadline)
            __asm__ volatile("pause");
    }

    /* Reset EP0 toggle after SET_ADDRESS (USB spec: device resets toggles) */
    dev->ep0.toggle = 0;

    /* Read full device descriptor */
    ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0,
                             &dev->dev_desc, sizeof(dev->dev_desc));
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: failed to get full device descriptor (err=%d)\n",
                addr, ret);
        usb_free_device(dev);
        return -1;
    }

    dev->vendor_id = dev->dev_desc.idVendor;
    dev->product_id = dev->dev_desc.idProduct;

    /* Read config descriptor header to get total length */
    ret = usb_get_descriptor(dev, USB_DT_CONFIG, 0, &cd, sizeof(cd));
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: failed to get config descriptor (err=%d)\n",
                addr, ret);
        usb_free_device(dev);
        return -1;
    }

    /* Read the full configuration descriptor into a cache sized to this
     * device.  USB_MAX_CONFIG_SIZE is only a sanity bound against a
     * hostile/wedged device claiming an absurd wTotalLength -- a device
     * within it gets exactly the space it asked for, so we never silently
     * lose trailing interfaces or endpoints. */
    if (cd.wTotalLength < sizeof(cd) || cd.wTotalLength > USB_MAX_CONFIG_SIZE) {
        kprintf("usb: addr %u: implausible config descriptor length %u B "
                "(max %u); skipping\n",
                addr, cd.wTotalLength, USB_MAX_CONFIG_SIZE);
        usb_free_device(dev);
        return -1;
    }
    dev->config_data = kzalloc(cd.wTotalLength);
    if (!dev->config_data) {
        kprintf("usb: addr %u: out of memory for %u B config descriptor\n",
                addr, cd.wTotalLength);
        usb_free_device(dev);
        return -1;
    }
    dev->config_len = cd.wTotalLength;

    ret = usb_get_descriptor(dev, USB_DT_CONFIG, 0,
                             dev->config_data, dev->config_len);
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: failed to get full config descriptor (err=%d)\n",
                addr, ret);
        usb_free_device(dev);
        return -1;
    }

    /* Parse config: extract endpoints and interface class */
    usb_parse_config(dev);

    /* Set configuration.  A device whose only configuration is numbered 0 has
     * no usable configuration at all -- selecting it unconfigures the device
     * -- so refuse rather than proceeding with a device that is not
     * configured. [USB-22] */
    if (cd.bConfigurationValue == 0) {
        kprintf("usb: addr %u: configuration value 0 is not selectable\n", addr);
        usb_free_device(dev);
        return -1;
    }
    ret = usb_set_configuration(dev, cd.bConfigurationValue);
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: SET_CONFIGURATION failed (err=%d)\n", addr, ret);
        usb_free_device(dev);
        return -1;
    }

    /* Fetch the human-readable manufacturer/product strings.  Best-effort:
     * failures leave the buffers empty and we fall back to the numeric IDs. */
    usb_get_string(dev, dev->dev_desc.iManufacturer,
                   dev->manufacturer, sizeof(dev->manufacturer));
    usb_get_string(dev, dev->dev_desc.iProduct,
                   dev->product, sizeof(dev->product));

    if (dev->manufacturer[0] || dev->product[0]) {
        kprintf("usb: addr %u: %04x:%04x class %02x/%02x/%02x (%u endpoints) "
                "\"%s%s%s\"\n",
                dev->address, dev->vendor_id, dev->product_id,
                dev->if_class, dev->if_subclass, dev->if_protocol,
                dev->num_endpoints,
                dev->manufacturer,
                (dev->manufacturer[0] && dev->product[0]) ? " " : "",
                dev->product);
    } else {
        kprintf("usb: addr %u: %04x:%04x class %02x/%02x/%02x (%u endpoints)\n",
                dev->address, dev->vendor_id, dev->product_id,
                dev->if_class, dev->if_subclass, dev->if_protocol,
                dev->num_endpoints);
    }

    /* Match and bind a class driver */
    usb_match_driver(dev);

    /* Publish into the kernel device tree (/proc/devtree). */
    usb_publish_device(dev);

    return 0;
}

void usb_enumerate_bus(usb_hcd_t *hcd)
{
    uint32_t status;
    uint8_t speed;

    if (!hcd || !hcd->port_status || !hcd->port_reset)
        return;

    for (uint8_t port = 1; port <= hcd->nports; port++) {
        status = hcd->port_status(hcd, port);

        if (!(status & USB_PORT_STAT_CONNECTION))
            continue;

        /* Reset port to enable it */
        if (hcd->port_reset(hcd, port) != 0)
            continue;

        /* Re-read status after reset */
        status = hcd->port_status(hcd, port);

        if (!(status & USB_PORT_STAT_ENABLE))
            continue;

        /* Determine speed */
        if (status & USB_PORT_STAT_SUPER_SPEED)
            speed = USB_SPEED_SUPER;
        else if (status & USB_PORT_STAT_LOW_SPEED)
            speed = USB_SPEED_LOW;
        else if (status & USB_PORT_STAT_HIGH_SPEED)
            speed = USB_SPEED_HIGH;
        else
            speed = USB_SPEED_FULL;

        usb_enumerate_device(hcd, port, speed);
    }
}

/*
 * ============================================================
 * Hot-plug monitor
 * ============================================================
 *
 * A kthread scans every registered HCD's root ports a few times a second and
 * reconciles the physical connection state against the device table:
 *   - a port that lost its device (CCS=0 while a root device is tracked there)
 *     is disconnected: the bound class driver's .detach runs (e.g. usb_msc ->
 *     scsi_unregister_link -> blkdev_unregister -> force-unmount of any mounted
 *     filesystem) and the usb_device_t is freed;
 *   - a port that gained a device (CCS=1 with nothing tracked) is reset and
 *     enumerated, so devices attached after boot are picked up too.
 * Downstream hub ports are handled by the hub driver; this covers the root hub,
 * where qemu's device_del / a physical unplug lands.
 */

/* The ROOT device (parent == NULL) currently enumerated on hcd:port, or NULL. */
static usb_device_t *usb_root_device_on_port(usb_hcd_t *hcd, uint8_t port)
{
    usb_device_t *found = NULL;

    mutex_lock(&usb_devtab_lock);
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_device_t *d = usb_devices[i];
        if (d && d->hcd == hcd && d->parent == NULL && d->port == port) {
            found = d;
            break;
        }
    }
    mutex_unlock(&usb_devtab_lock);
    return found;
}

/* Detach a vanished device.  Ordering matters: the bound driver must detach
 * (quiesce in-flight transfers, force-unmount, free its DMA buffers) and every
 * published node that stores a back-pointer to this usb_device_t must be
 * removed BEFORE the struct is freed, or those consumers dereference freed
 * memory. [DRV-01][DRV-02][DRV-20] */
void usb_disconnect_device(usb_device_t *dev)
{
    if (!dev)
        return;

    /* 0. Recursively disconnect any devices enumerated behind this one (it may
     * be a hub).  They are recorded with parent == dev (usb_enumerate_device_
     * parent) and share the hub's root-port CCS, so the root-port hot-plug scan
     * never sees them go away.  Unplugging a hub — or a whole sub-tree of nested
     * hubs — must therefore quiesce and free its children here, or each
     * downstream device's class-driver .detach (force-unmount, DMA-buffer free,
     * thread quiesce) is skipped and its usb_device_t + USB address leak.
     * Children go first so their in-flight I/O drains before the parent hub
     * (which they depend on for transfers) is torn down. [A33] */
    /*
     * Take one child at a time: look it up under the lock, then recurse with
     * the lock dropped, because the child's teardown runs its class driver's
     * .detach, which sleeps.  Snapshotting the whole list instead would put a
     * 512-byte array on a stack that recurses to the hub-tier limit. [USB-19]
     * Each pass removes a child, so the loop terminates.
     */
    for (;;) {
        usb_device_t *child = NULL;

        mutex_lock(&usb_devtab_lock);
        for (int i = 0; i < USB_MAX_DEVICES; i++) {
            usb_device_t *d = usb_devices[i];
            if (d && d != dev && d->parent == dev) {
                child = d;
                break;
            }
        }
        mutex_unlock(&usb_devtab_lock);

        if (!child)
            break;
        usb_disconnect_device(child);
    }

    /* 1. Driver detach first: it drains outstanding I/O before we free. */
    if (dev->driver && dev->driver->detach)
        dev->driver->detach(dev);
    dev->driver = NULL;

    /* 2. Remove the /proc/devtree bus node (leaked one per hot-plug otherwise). */
    if (dev->devtree_dev) {
        device_unregister(dev->devtree_dev);
        device_put(dev->devtree_dev);
        dev->devtree_dev = NULL;
    }

    /* 3. Remove the /dev/usb nodes that cache a raw usb_device_t pointer. */
    usbdevfs_unpublish(dev);

    /* 4. Now nothing references the struct: free it. */
    usb_free_device(dev);
}

/*
 * The device enumerated behind `parent` on downstream port `port`, or NULL.
 * The hub driver uses this to reconcile physical port state against what is
 * actually enumerated, the same way usb_root_device_on_port does for the
 * root hub.
 */
usb_device_t *usb_child_device_on_port(usb_device_t *parent, uint8_t port)
{
    usb_device_t *found = NULL;

    if (!parent)
        return NULL;
    mutex_lock(&usb_devtab_lock);
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        usb_device_t *d = usb_devices[i];
        if (d && d->parent == parent && d->port == port) {
            found = d;
            break;
        }
    }
    mutex_unlock(&usb_devtab_lock);
    return found;
}

static void usb_hotplug_scan(void)
{
    for (usb_hcd_t *hcd = usb_hcd_list; hcd; hcd = hcd->next) {
        if (!hcd->port_status)
            continue;
        for (uint8_t port = 1; port <= hcd->nports; port++) {
            uint32_t st = hcd->port_status(hcd, port);
            int connected = (st & USB_PORT_STAT_CONNECTION) != 0;
            usb_device_t *dev = usb_root_device_on_port(hcd, port);

            /* Ports are 1-based; index from 0 so the last port is covered
             * too. [USB-20] */
            uint8_t *fails = (port >= 1 && port <= USB_MAX_ROOT_PORTS)
                             ? &hcd->enum_fail[port - 1] : NULL;

            if (!connected) {
                if (fails)
                    *fails = 0;  /* gone: a re-plug deserves a fresh try */
                /* Let the HCD reap whatever it still has bound to this port
                 * (xHCI: the device slot and its rings). [X-12] */
                if (hcd->port_gone)
                    hcd->port_gone(hcd, port);
            }

            if (dev && !connected) {
                kprintf("usb: device removed from %s port %u\n",
                        hcd->name, port);
                usb_disconnect_device(dev);
            } else if (!dev && connected && hcd->port_reset) {
                /* Parked after repeated failures: a port that reports a
                 * device it cannot enumerate must not be reset and re-probed
                 * forever.  Only a disconnect clears this. */
                if (fails && *fails >= USB_ENUM_MAX_TRIES)
                    continue;

                if (hcd->port_reset(hcd, port) != 0) {
                    if (fails) (*fails)++;
                    continue;
                }
                st = hcd->port_status(hcd, port);
                if (!(st & USB_PORT_STAT_ENABLE)) {
                    if (fails) (*fails)++;
                    continue;
                }
                uint8_t speed = (st & USB_PORT_STAT_SUPER_SPEED) ? USB_SPEED_SUPER :
                                (st & USB_PORT_STAT_LOW_SPEED)   ? USB_SPEED_LOW   :
                                (st & USB_PORT_STAT_HIGH_SPEED)  ? USB_SPEED_HIGH  :
                                                                   USB_SPEED_FULL;
                if (usb_enumerate_device(hcd, port, speed) != 0 && fails) {
                    if (++(*fails) >= USB_ENUM_MAX_TRIES)
                        kprintf("usb: %s port %u: enumeration failed %u times,"
                                " giving up until re-plug\n",
                                hcd->name, port, USB_ENUM_MAX_TRIES);
                }
            }
        }
    }

    /*
     * Root ports only cover what is plugged directly into the controller.
     * Everything behind a hub shares its hub's root-port connection bit, so
     * the loop above can never see those come and go -- the hub driver has
     * to walk its own downstream ports.
     */
    usb_hub_scan_ports();
}

static int usb_hotplug_chan;

/*
 * Runs as a kernel process, the same shape as the page daemon: named in
 * `comm` so it is identifiable in ps, flagged is_kernel_task, and given an
 * explicit scheduling class rather than inheriting whatever the spawner had.
 * It is a long-lived system service that outlives every consumer of it, not
 * a helper thread belonging to whoever happened to call usb_init().
 */
static void usb_hotplug_monitor(void *arg)
{
    (void)arg;

    if (current_process) {
        strncpy(current_process->comm, "usbhotplug", AC_COMM_LEN);
        current_process->comm[AC_COMM_LEN - 1] = '\0';
        current_process->is_kernel_task = 1;
    }
    if (current_thread) {
        current_thread->sched_class = SCHED_TIMESHARE;
        current_thread->priority = 1;
        current_thread->base_priority = 1;
    }

    uint64_t interval = get_hz() / 4;  /* ~250 ms between scans */
    if (interval == 0)
        interval = 1;
    for (;;) {
        sched_sleep_until(&usb_hotplug_chan, get_ticks() + interval);
        usb_hotplug_scan();
    }
}

/*
 * ============================================================
 * USB Subsystem Initialization
 * ============================================================
 */

void usb_init(void)
{
    usb_hcd_t *hcd;

    mutex_init(&usb_devtab_lock, "usb_devtab");
    memset(usb_devices, 0, sizeof(usb_devices));
    memset(usb_addr_bitmap, 0, sizeof(usb_addr_bitmap));
    usb_enum_depth = 0;

    bus_register_type(&usb_bus_type);

    kprintf("usb: subsystem initialized\n");

    /* Enumerate all registered HCDs */
    for (hcd = usb_hcd_list; hcd; hcd = hcd->next) {
        usb_enumerate_bus(hcd);
    }

    /* Start the hot-plug monitor so devices attached/removed after boot
     * (qemu device_add/device_del, a physical (un)plug) are handled --
     * but not from here: see usb_late_init(). */
}

/*
 * Start the hot-plug monitor.  Separate from usb_init() purely because of
 * when it runs: usb_init() happens during device bring-up, long before kmain
 * spawns init, so spawning a process there hands the monitor PID 1 -- the
 * pid init itself is supposed to get.  Called instead from kmain after
 * init_task and the page daemon, which puts it at PID 3 where it belongs.
 */
static int usb_hotplug_started;

void usb_late_init(void)
{
    if (usb_hotplug_started)
        return;
    if (sched_spawn_kernel_process(usb_hotplug_monitor, NULL) < 0) {
        kprintf("usb: failed to start hot-plug monitor\n");
        return;
    }
    usb_hotplug_started = 1;
}
