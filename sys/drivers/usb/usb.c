/*
 * usb.c - USB Core Framework
 *
 * Device enumeration, descriptor parsing, control/bulk transfer wrappers,
 * and class driver matching for the Substrate kernel USB subsystem.
 */

#include "usb.h"
#include <kern/console.h>
#include <vm/vm_kmem.h>
#include <string.h>

/*
 * ============================================================
 * Global State
 * ============================================================
 */

static usb_device_t *usb_devices[USB_MAX_DEVICES];
static uint8_t       usb_next_address = 1;

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
                usb_endpoint_t *ep = &dev->endpoints[dev->num_endpoints];
                ep->address = ep_desc->bEndpointAddress;
                ep->type = ep_desc->bmAttributes & USB_EP_TYPE_MASK;
                ep->max_packet = ep_desc->wMaxPacketSize;
                ep->interval = ep_desc->bInterval;
                ep->toggle = 0;
                dev->num_endpoints++;
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
        if (drv->if_class != dev->if_class)
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

        /* Attach */
        if (drv->attach) {
            if (drv->attach(dev) == 0) {
                kprintf("usb: device %u:%u bound to driver '%s'\n",
                        dev->hcd->hcd_index, dev->address, drv->name);
                return;
            }
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

int usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed)
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
        kprintf("usb: port %u: failed to get device descriptor (initial)\n", port);
        usb_free_device(dev);
        return -1;
    }

    dev->ep0.max_packet = dd.bMaxPacketSize0;

    /* Assign unique address */
    addr = usb_next_address;
    if (addr > 127) {
        kprintf("usb: address space exhausted\n");
        usb_free_device(dev);
        return -1;
    }
    usb_next_address++;

    ret = usb_set_address(dev, addr);
    if (ret != USB_XFER_OK) {
        kprintf("usb: port %u: SET_ADDRESS failed\n", port);
        usb_free_device(dev);
        return -1;
    }

    /* Small delay for address to take effect (USB spec: 2ms) */
    for (volatile int i = 0; i < 100000; i++)
        ;

    /* Read full device descriptor */
    ret = usb_get_descriptor(dev, USB_DT_DEVICE, 0,
                             &dev->dev_desc, sizeof(dev->dev_desc));
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: failed to get full device descriptor\n", addr);
        usb_free_device(dev);
        return -1;
    }

    dev->vendor_id = dev->dev_desc.idVendor;
    dev->product_id = dev->dev_desc.idProduct;

    /* Read config descriptor header to get total length */
    ret = usb_get_descriptor(dev, USB_DT_CONFIG, 0, &cd, sizeof(cd));
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: failed to get config descriptor\n", addr);
        usb_free_device(dev);
        return -1;
    }

    /* Read full configuration descriptor */
    dev->config_len = cd.wTotalLength;
    if (dev->config_len > USB_MAX_CONFIG_SIZE)
        dev->config_len = USB_MAX_CONFIG_SIZE;

    ret = usb_get_descriptor(dev, USB_DT_CONFIG, 0,
                             dev->config_data, dev->config_len);
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: failed to get full config descriptor\n", addr);
        usb_free_device(dev);
        return -1;
    }

    /* Parse config: extract endpoints and interface class */
    usb_parse_config(dev);

    /* Set configuration */
    ret = usb_set_configuration(dev, cd.bConfigurationValue);
    if (ret != USB_XFER_OK) {
        kprintf("usb: addr %u: SET_CONFIGURATION failed\n", addr);
        usb_free_device(dev);
        return -1;
    }

    kprintf("usb: addr %u: %04x:%04x class %02x/%02x/%02x (%u endpoints)\n",
            dev->address, dev->vendor_id, dev->product_id,
            dev->if_class, dev->if_subclass, dev->if_protocol,
            dev->num_endpoints);

    /* Match and bind a class driver */
    usb_match_driver(dev);

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
    usb_next_address = 1;

    kprintf("usb: subsystem initialized\n");

    /* Enumerate all registered HCDs */
    for (hcd = usb_hcd_list; hcd; hcd = hcd->next) {
        usb_enumerate_bus(hcd);
    }
}
