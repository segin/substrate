#ifndef _SYS_USBDEVFS_H
#define _SYS_USBDEVFS_H

#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/types.h>

/*
 * usbdevfs userspace/kernel ABI contract.
 *
 * USB devices are exposed through devfs as character devices at:
 *   /dev/usb/busN/devM
 * where N is the bus number and M is the device address on that bus.
 */

#define USBDEVFS_ROOT_DIR              "/dev/usb"
#define USBDEVFS_BUS_DIR_FORMAT        USBDEVFS_ROOT_DIR "/bus%u"
#define USBDEVFS_DEVICE_PATH_FORMAT    USBDEVFS_ROOT_DIR "/bus%u/dev%u"
#define USBDEVFS_BUS_NAME_FORMAT       "bus%u"
#define USBDEVFS_DEVICE_NAME_FORMAT    "dev%u"

#define USBDEVFS_NODE_OWNER_NAME       "root"
#define USBDEVFS_NODE_GROUP_NAME       "usb"
#define USBDEVFS_NODE_UID              0U
#define USBDEVFS_NODE_MODE             0664

#define USBDEVFS_MAXDRIVERNAME         255

#define USBDEVFS_URB_TYPE_ISO          0U
#define USBDEVFS_URB_TYPE_INTERRUPT    1U
#define USBDEVFS_URB_TYPE_CONTROL      2U
#define USBDEVFS_URB_TYPE_BULK         3U

#define USBDEVFS_URB_SHORT_NOT_OK      0x0001U
#define USBDEVFS_URB_ISO_ASAP          0x0002U
#define USBDEVFS_URB_BULK_CONTINUATION 0x0004U
#define USBDEVFS_URB_NO_FSBR           0x0020U
#define USBDEVFS_URB_ZERO_PACKET       0x0040U

#ifndef _IOC
#define _IOC_NRBITS     8
#define _IOC_TYPEBITS   8
#define _IOC_SIZEBITS   14
#define _IOC_DIRBITS    2

#define _IOC_NRSHIFT    0
#define _IOC_TYPESHIFT  (_IOC_NRSHIFT + _IOC_NRBITS)
#define _IOC_SIZESHIFT  (_IOC_TYPESHIFT + _IOC_TYPEBITS)
#define _IOC_DIRSHIFT   (_IOC_SIZESHIFT + _IOC_SIZEBITS)

#define _IOC_NONE       0U
#define _IOC_WRITE      1U
#define _IOC_READ       2U

#define _IOC(dir, type, nr, size) \
    (((dir) << _IOC_DIRSHIFT) | \
     ((type) << _IOC_TYPESHIFT) | \
     ((nr) << _IOC_NRSHIFT) | \
     ((size) << _IOC_SIZESHIFT))

#define _IO(type, nr)            _IOC(_IOC_NONE, (type), (nr), 0)
#define _IOR(type, nr, argtype)  _IOC(_IOC_READ, (type), (nr), sizeof(argtype))
#define _IOW(type, nr, argtype)  _IOC(_IOC_WRITE, (type), (nr), sizeof(argtype))
#define _IOWR(type, nr, argtype) _IOC(_IOC_READ | _IOC_WRITE, (type), (nr), sizeof(argtype))
#endif

struct usbdevfs_ctrltransfer {
    uint8_t bRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
    uint32_t timeout;
    void *data;
};

struct usbdevfs_bulktransfer {
    unsigned int ep;
    unsigned int len;
    unsigned int timeout;
    void *data;
};

struct usbdevfs_setinterface {
    unsigned int interface;
    unsigned int altsetting;
};

struct usbdevfs_getdriver {
    unsigned int interface;
    char driver[USBDEVFS_MAXDRIVERNAME + 1];
};

struct usbdevfs_iso_packet_desc {
    unsigned int length;
    unsigned int actual_length;
    unsigned int status;
};

struct usbdevfs_urb {
    uint8_t type;
    uint8_t endpoint;
    int status;
    unsigned int flags;
    void *buffer;
    int buffer_length;
    int actual_length;
    int start_frame;
    union {
        int number_of_packets;
        unsigned int stream_id;
    } u;
    int error_count;
    unsigned int signr;
    void *usercontext;
    struct usbdevfs_iso_packet_desc iso_frame_desc[];
};

#define USBDEVFS_CONTROL             _IOWR('U', 0, struct usbdevfs_ctrltransfer)
#define USBDEVFS_BULK                _IOWR('U', 2, struct usbdevfs_bulktransfer)
#define USBDEVFS_SETINTERFACE        _IOR('U', 4, struct usbdevfs_setinterface)
#define USBDEVFS_SETCONFIGURATION    _IOR('U', 5, unsigned int)
#define USBDEVFS_GET_DRIVER          _IOW('U', 8, struct usbdevfs_getdriver)
#define USBDEVFS_SUBMITURB           _IOR('U', 10, struct usbdevfs_urb)
#define USBDEVFS_DISCARDURB          _IO('U', 11)
#define USBDEVFS_REAPURB             _IOW('U', 12, void *)
#define USBDEVFS_REAPURBNDELAY       _IOW('U', 13, void *)
#define USBDEVFS_CLAIMINTERFACE      _IOR('U', 15, unsigned int)
#define USBDEVFS_RELEASEINTERFACE    _IOR('U', 16, unsigned int)
#define USBDEVFS_RESET               _IO('U', 20)
#define USBDEVFS_CLEAR_HALT          _IOR('U', 21, unsigned int)
#define USBDEVFS_DISCONNECT          _IO('U', 22)
#define USBDEVFS_CONNECT             _IO('U', 23)

#endif