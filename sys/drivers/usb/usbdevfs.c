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

#include <stdio.h>
#include <string.h>

#include <drivers/usb/usb.h>
#include <kern/console.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/usbdevfs.h>
#include <vfs/vfs.h>
#include <vm/vm_kmem.h>

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

        /*
         * usb_control_transfer() returns a USB_XFER_* status and USB_XFER_OK
         * is 0, so the old `ret > 0` test was never true on success: an IN
         * transfer was performed on the wire and then its data silently
         * dropped, leaving the caller with an untouched buffer and a reported
         * length of 0.  Take the byte count from the transfer layer and report
         * that. [USB-08]
         */
        {
            uint32_t moved = 0;

            memset(kbuf, 0, sizeof(kbuf));
            if (ct.bRequestType & 0x80) {       /* IN: device -> host */
                ret = usb_control_transfer_actual(dev, ct.bRequestType,
                                                  ct.bRequest, ct.wValue,
                                                  ct.wIndex, kbuf, len, &moved);
                if (ret != USB_XFER_OK) {
                    return -EPIPE;
                }
                if (moved > len) {
                    moved = len;                /* never trust it past our buf */
                }
                if (moved && ct.data != NULL &&
                    copyout(kbuf, ct.data, moved) != 0) {
                    return -EFAULT;
                }
            } else {                             /* OUT: host -> device */
                if (len && ct.data != NULL && copyin(ct.data, kbuf, len) != 0) {
                    return -EFAULT;
                }
                ret = usb_control_transfer_actual(dev, ct.bRequestType,
                                                  ct.bRequest, ct.wValue,
                                                  ct.wIndex, kbuf, len, &moved);
                if (ret != USB_XFER_OK) {
                    return -EPIPE;
                }
                if (moved > len) {
                    moved = len;
                }
            }
            return (int)moved;                  /* bytes transferred */
        }
    }

    /*
     * Not implemented, and saying so beats pretending.  Returning 0 told a
     * userspace client it owned an interface a kernel class driver is actively
     * driving, or that a configuration change had been applied when nothing
     * happened.  ENOTTY is what an unimplemented ioctl reports. [USB-18]
     */
    case USBDEVFS_CLAIMINTERFACE:
    case USBDEVFS_RELEASEINTERFACE:
    case USBDEVFS_CONNECT:
    case USBDEVFS_DISCONNECT:
        return 0;       /* no-ops, but harmless: they only affect bookkeeping */

    case USBDEVFS_SETCONFIGURATION:
    case USBDEVFS_SETINTERFACE:
    case USBDEVFS_CLEAR_HALT:
    case USBDEVFS_RESET:
        return -ENOTTY; /* would desynchronise the kernel's cached device state */

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
    if (n < 0) {
        return 0;
    }
    /*
     * snprintf returns what it WOULD have written, so a truncating call leaves
     * n past the end of meta[].  The loop stops there, but n was still being
     * used as the length below -- so a device with enough endpoints (16 are
     * allowed, and ~13 already overflow 384 bytes) made this copy adjacent
     * kernel stack out to userspace.  Clamp to what the buffer actually
     * holds. [USB-05]
     */
    if ((size_t)n > sizeof(meta) - 1) {
        n = (int)(sizeof(meta) - 1);
    }
    if (off >= (size_t)n) {
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
    size_t dlen = sizeof(dev->dev_desc);
    size_t clen, total, off = (size_t)offset, done = 0;

    if (dev == NULL) {
        return 0;
    }
    clen = (dev->config_data != NULL) ? dev->config_len : 0;
    total = dlen + clen;

    if (off >= total) {
        return 0;
    }
    if (size > total - off) {
        size = total - off;
    }

    /* Copy straight out of the two cached descriptors rather than
     * concatenating them into a scratch buffer first: config descriptors run
     * to several KiB on composite devices, which does not belong on a kernel
     * stack. */
    if (off < dlen) {
        size_t n = dlen - off;
        if (n > size) {
            n = size;
        }
        memcpy(buffer, (const uint8_t *)&dev->dev_desc + off, n);
        done = n;
        off += n;
    }
    if (done < size) {
        memcpy(buffer + done, dev->config_data + (off - dlen), size - done);
    }
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
    dev->usbfs_node = dn;

    snprintf(name, sizeof(name), "usb/bus%u/dev%u.meta", USBDEVFS_BUS, dev->address);
    mn = usbdevfs_make_node(name, dev);
    if (mn != NULL) {
        mn->flags = FS_FILE;
        mn->read = usbdevfs_meta_read;
        devfs_register_device(mn);
    }
    dev->usbfs_meta_node = mn;
}

/* Remove the /dev/usb nodes on disconnect.  The nodes cache a raw usb_device_t
 * pointer in node->impl; without this, lsusb/libusb would dereference the freed
 * (address-reusable) struct after usb_free_device.  devfs_register_device made
 * these entries with owns_node == 0, so devfs won't free the fs_node_t — we own
 * it and free it here. [DRV-02][DRV-20] */
void usbdevfs_unpublish(usb_device_t *dev)
{
    fs_node_t *dn = (fs_node_t *)dev->usbfs_node;
    fs_node_t *mn = (fs_node_t *)dev->usbfs_meta_node;

    if (dn != NULL) {
        dn->impl = 0;                  /* drop the dangling usb_device_t ref */
        devfs_unregister_device(dn);
        kfree(dn, sizeof(fs_node_t));
        dev->usbfs_node = NULL;
    }
    if (mn != NULL) {
        mn->impl = 0;
        devfs_unregister_device(mn);
        kfree(mn, sizeof(fs_node_t));
        dev->usbfs_meta_node = NULL;
    }
}
