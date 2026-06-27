/*
 * usb.c - USB Core Framework
 *
 * Device enumeration, descriptor parsing, control/bulk transfer wrappers,
 * and class driver matching for the Substrate kernel USB subsystem.
 */

#include "usb.h"
#include <kern/console.h>
#include <kern/time.h>
#include <kern/device.h>
#include <kern/bus.h>
#include <vm/vm_kmem.h>
#include <stdio.h>
#include <string.h>

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
static inline int    usb_addr_alloc(void) {
    for (int a = 1; a < 128; a++) {
        if (!(usb_addr_bitmap[a >> 5] & (1U << (a & 31)))) {
            usb_addr_bitmap[a >> 5] |= (1U << (a & 31));
            return a;
        }
    }
    return -1;
}
static inline void   usb_addr_free(uint8_t a) {
    if (a == 0 || a >= 128) return;
    usb_addr_bitmap[a >> 5] &= ~(1U << (a & 31));
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

    /* Find free slot */
    for (slot = 0; slot < USB_MAX_DEVICES; slot++) {
        if (!usb_devices[slot])
            break;
    }
    if (slot >= USB_MAX_DEVICES)
        return NULL;

    dev = kzalloc(sizeof(usb_device_t));
    if (!dev)
        return NULL;

    dev->hcd = hcd;
    dev->slot = (uint8_t)slot;
    dev->ep0.address = 0;
    dev->ep0.type = USB_EP_TYPE_CONTROL;
    dev->ep0.max_packet = 8;    /* Default until descriptor read */
    dev->ep0.toggle = 0;

    usb_devices[slot] = dev;
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

    if (dev->slot < USB_MAX_DEVICES)
        usb_devices[dev->slot] = NULL;

    kfree(dev, sizeof(usb_device_t));
}

/*
 * ============================================================
 * Transfer API (synchronous wrappers)
 * ============================================================
 */

int usb_control_transfer(usb_device_t *dev,
                         uint8_t bmRequestType, uint8_t bRequest,
                         uint16_t wValue, uint16_t wIndex,
                         void *data, uint16_t wLength)
{
    usb_transfer_t xfer;

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

    return dev->hcd->submit(dev->hcd, &xfer);
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
        dev->configured = 1;
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

usb_endpoint_t *usb_find_endpoint(usb_device_t *dev, uint8_t type, uint8_t dir)
{
    for (int i = 0; i < dev->num_endpoints; i++) {
        usb_endpoint_t *ep = &dev->endpoints[i];
        if ((ep->address & USB_EP_DIR_MASK) == dir &&
            ep->type == type)
            return ep;
    }
    return NULL;
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

    dev->num_endpoints = 0;

    while (ptr + 2 <= end) {
        uint8_t bLength = ptr[0];
        uint8_t bType = ptr[1];

        if (bLength < 2 || ptr + bLength > end)
            break;

        if (bType == USB_DT_INTERFACE && bLength >= 9 && !first_iface_seen) {
            struct usb_interface_descriptor *iface =
                (struct usb_interface_descriptor *)ptr;
            dev->if_class = iface->bInterfaceClass;
            dev->if_subclass = iface->bInterfaceSubClass;
            dev->if_protocol = iface->bInterfaceProtocol;
            first_iface_seen = 1;
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
                    ep->address = addr;
                    ep->type = ep_desc->bmAttributes & USB_EP_TYPE_MASK;
                    ep->max_packet = ep_desc->wMaxPacketSize;
                    ep->interval = ep_desc->bInterval;
                    ep->toggle = 0;
                    dev->num_endpoints++;
                }
            }
        }

        ptr += bLength;
    }
}

/*
 * ============================================================
 * Class Driver Matching
 * ============================================================
 */

static void usb_match_driver(usb_device_t *dev)
{
    usb_class_driver_t *drv;

    for (drv = usb_class_drivers; drv; drv = drv->next) {
        /* Check class/subclass/protocol match */
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
#define USB_MAX_ENUM_DEPTH 7
static int usb_enum_depth = 0;

static int usb_enumerate_device_inner(usb_hcd_t *hcd, uint8_t port, uint8_t speed);

int usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed)
{
    if (usb_enum_depth >= USB_MAX_ENUM_DEPTH) {
        kprintf("usb: enumeration depth limit (%d) reached at port %u; refusing\n",
                USB_MAX_ENUM_DEPTH, port);
        return -1;
    }
    usb_enum_depth++;
    int ret = usb_enumerate_device_inner(hcd, port, speed);
    usb_enum_depth--;
    return ret;
}

static int usb_enumerate_device_inner(usb_hcd_t *hcd, uint8_t port, uint8_t speed)
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
    dev->address = 0;   /* Default address for initial communication */

    /* Set EP0 max packet size based on speed */
    dev->ep0.max_packet = (speed == USB_SPEED_LOW) ? 8 : 64;

    /* Get first 8 bytes of device descriptor to learn max packet size */
    ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0, &dd, 8);
    if (ret != USB_XFER_OK) {
        kprintf("usb: port %u: failed to get device descriptor (initial, err=%d)\n",
                port, ret);
        usb_free_device(dev);
        return -1;
    }

    dev->ep0.max_packet = dd.bMaxPacketSize0;
    if (dev->ep0.max_packet == 0)
        dev->ep0.max_packet = 8;

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

    /* Read full configuration descriptor.  USB_MAX_CONFIG_SIZE bounds
     * the per-device cache so a hostile/buggy device can't make us
     * allocate unbounded memory; reject the device if its config
     * legitimately exceeds the cache so we don't silently lose
     * trailing endpoints / interfaces. */
    if (cd.wTotalLength > USB_MAX_CONFIG_SIZE) {
        kprintf("usb: addr %u: config descriptor %u B exceeds cache (%u); skipping\n",
                addr, cd.wTotalLength, USB_MAX_CONFIG_SIZE);
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

    /* Set configuration */
    ret = usb_set_configuration(dev, cd.bConfigurationValue);
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: SET_CONFIGURATION failed (err=%d)\n", addr, ret);
        usb_free_device(dev);
        return -1;
    }

    kprintf("usb: addr %u: %04x:%04x class %02x/%02x/%02x (%u endpoints)\n",
            dev->address, dev->vendor_id, dev->product_id,
            dev->if_class, dev->if_subclass, dev->if_protocol,
            dev->num_endpoints);

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
        if (status & USB_PORT_STAT_LOW_SPEED)
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
 * USB Subsystem Initialization
 * ============================================================
 */

void usb_init(void)
{
    usb_hcd_t *hcd;

    memset(usb_devices, 0, sizeof(usb_devices));
    memset(usb_addr_bitmap, 0, sizeof(usb_addr_bitmap));
    usb_enum_depth = 0;

    bus_register_type(&usb_bus_type);

    kprintf("usb: subsystem initialized\n");

    /* Enumerate all registered HCDs */
    for (hcd = usb_hcd_list; hcd; hcd = hcd->next) {
        usb_enumerate_bus(hcd);
    }
}
