/*
 * sys/drivers/usb/usbdevfs.c — /dev/usb device nodes for libusb / lsusb.
 *
 * For each enumerated USB device this publishes:
 *   /dev/usb/bus0/dev<addr>        — a char node whose USBDEVFS_CONTROL ioctl
 *                                    issues control transfers through the kernel
 *                                    USB core (libusb fetches descriptors here);
 *   /dev/usb/bus0/dev<addr>.meta   — a regular file describing the device
 *                                    (port/speed/parent/config/endpoint sizes),
 *                                    the key=value format lib/usb's substrate
 *                                    backend parses.
 */

#include <drivers/usb/usb.h>
#include <sys/usbdevfs.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

#define USBDEVFS_BUS 0u    /* one logical bus for now */

/* USBDEVFS_CONTROL + friends on a /dev/usb/.../dev<addr> node. */
static int usbdevfs_dev_ioctl(fs_node_t *node, uint32_t request, void *arg)
{
    usb_device_t *dev = (usb_device_t *)(uintptr_t)node->impl;

    if (dev == NULL) {
        return -ENODEV;
    }

    switch (request) {
    case USBDEVFS_CONTROL: {
        struct usbdevfs_ctrltransfer ct;
        uint8_t kbuf[260];
        uint16_t len;
        int ret;

        if (copyin(arg, &ct, sizeof(ct)) != 0) {
            return -EFAULT;
        }
        len = ct.wLength;
        if (len > sizeof(kbuf)) {
            len = sizeof(kbuf);
        }

        /* Serve standard GET_DESCRIPTOR from the descriptors cached at
         * enumeration time.  This is what lsusb needs, and it avoids replaying a
         * live control transfer from a process ioctl (the boot-time HCD path
         * doesn't currently support that cleanly — it times out). */
        if (ct.bRequestType == 0x80 && ct.bRequest == 0x06) {
            const uint8_t *src = NULL;
            uint16_t srclen = 0;
            uint8_t dtype = (uint8_t)(ct.wValue >> 8);

            if (dtype == 0x01) {              /* DEVICE */
                src = (const uint8_t *)&dev->dev_desc;
                srclen = (uint16_t)sizeof(dev->dev_desc);
            } else if (dtype == 0x02) {       /* CONFIGURATION (+ ifaces/eps) */
                src = dev->config_data;
                srclen = dev->config_len;
            }
            if (src != NULL) {
                if (len > srclen) {
                    len = srclen;
                }
                if (len && ct.data != NULL && copyout(src, ct.data, len) != 0) {
                    return -EFAULT;
                }
                return len;
            }
            return -EPIPE;                    /* strings etc. not cached */
        }

        if (ct.bRequestType & 0x80) {           /* IN: device -> host */
            ret = usb_control_transfer(dev, ct.bRequestType, ct.bRequest,
                                       ct.wValue, ct.wIndex, kbuf, len);
            if (ret > 0 && ct.data != NULL) {
                size_t n = (size_t)(ret < (int)len ? ret : len);
                if (copyout(kbuf, ct.data, n) != 0) {
                    return -EFAULT;
                }
            }
        } else {                                 /* OUT: host -> device */
            if (len && ct.data != NULL && copyin(ct.data, kbuf, len) != 0) {
                return -EFAULT;
            }
            ret = usb_control_transfer(dev, ct.bRequestType, ct.bRequest,
                                       ct.wValue, ct.wIndex, kbuf, len);
        }
        return ret;     /* bytes transferred, or negative errno */
    }

    case USBDEVFS_SETCONFIGURATION:
    case USBDEVFS_CLAIMINTERFACE:
    case USBDEVFS_RELEASEINTERFACE:
    case USBDEVFS_SETINTERFACE:
    case USBDEVFS_CLEAR_HALT:
    case USBDEVFS_RESET:
    case USBDEVFS_CONNECT:
    case USBDEVFS_DISCONNECT:
        return 0;       /* accepted; bookkeeping not needed for enumeration */

    default:
        return -EINVAL;
    }
}

/* Render the .meta key=value file lib/usb expects. */
static size_t usbdevfs_meta_read(fs_node_t *node, off_t offset, size_t size,
                                 uint8_t *buffer)
{
    usb_device_t *dev = (usb_device_t *)(uintptr_t)node->impl;
    char meta[384];
    int n;
    size_t off = (size_t)offset;

    if (dev == NULL) {
        return 0;
    }
    n = snprintf(meta, sizeof(meta),
                 "port=%u\nspeed=%u\nparent=0:0\nactive_configuration=%u\n",
                 dev->port, dev->speed, dev->config_value);
    for (int i = 0; i < dev->num_endpoints && n > 0 && n < (int)sizeof(meta); i++) {
        n += snprintf(meta + n, sizeof(meta) - (size_t)n,
                      "ep%x_max_packet_size=%u\n",
                      dev->endpoints[i].address, dev->endpoints[i].max_packet);
    }
    if (n < 0 || off >= (size_t)n) {
        return 0;
    }
    if (size > (size_t)n - off) {
        size = (size_t)n - off;
    }
    memcpy(buffer, meta + off, size);
    return size;
}

/* Raw descriptor blob (device descriptor + config descriptor), like Linux
 * usbfs: a read() of the device node returns the concatenated descriptors. */
static size_t usbdevfs_dev_read(fs_node_t *node, off_t offset, size_t size,
                                uint8_t *buffer)
{
    usb_device_t *dev = (usb_device_t *)(uintptr_t)node->impl;
    uint8_t blob[sizeof(struct usb_device_descriptor) + USB_MAX_CONFIG_SIZE];
    size_t n, off = (size_t)offset;

    if (dev == NULL) {
        return 0;
    }
    memcpy(blob, &dev->dev_desc, sizeof(dev->dev_desc));
    n = sizeof(dev->dev_desc);
    if (dev->config_len > 0 && n + dev->config_len <= sizeof(blob)) {
        memcpy(blob + n, dev->config_data, dev->config_len);
        n += dev->config_len;
    }
    if (off >= n) {
        return 0;
    }
    if (size > n - off) {
        size = n - off;
    }
    memcpy(buffer, blob + off, size);
    return size;
}

static size_t usbdevfs_dev_write(fs_node_t *node, off_t offset, size_t size,
                                 const uint8_t *buffer)
{
    (void)node; (void)offset; (void)buffer;
    return size;        /* accept and discard — only here so O_RDWR opens */
}

static fs_node_t *usbdevfs_make_node(const char *name, usb_device_t *dev)
{
    fs_node_t *node = (fs_node_t *)kzalloc(sizeof(fs_node_t));
    if (node == NULL) {
        return NULL;
    }
    strlcpy(node->name, name, sizeof(node->name));
    node->impl = (uintptr_t)dev;
    return node;
}

/* Publish the device + .meta nodes under /dev/usb/bus0/. */
void usbdevfs_publish(usb_device_t *dev)
{
    char name[64];
    fs_node_t *dn, *mn;

    snprintf(name, sizeof(name), "usb/bus%u/dev%u", USBDEVFS_BUS, dev->address);
    dn = usbdevfs_make_node(name, dev);
    if (dn != NULL) {
        /* FS_FILE, NOT FS_CHARDEVICE: a char-device open takes the tty
         * carrier-wait path and returns ETIMEDOUT, and a type-0 node is never
         * poll-ready (poll_fs only reports FS_FILE/FS_DIRECTORY ready).  libusb
         * poll()s the fd before every control transfer, so the node must read
         * as a regular file: opens cleanly, polls ready, and the ioctl stays
         * reachable regardless of type. */
        dn->flags = FS_FILE;
        dn->read = usbdevfs_dev_read;
        dn->write = usbdevfs_dev_write;
        dn->ioctl = usbdevfs_dev_ioctl;
        devfs_register_device(dn);
    }

    snprintf(name, sizeof(name), "usb/bus%u/dev%u.meta", USBDEVFS_BUS, dev->address);
    mn = usbdevfs_make_node(name, dev);
    if (mn != NULL) {
        mn->flags = FS_FILE;
        mn->read = usbdevfs_meta_read;
        devfs_register_device(mn);
    }
}
