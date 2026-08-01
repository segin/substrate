/*
 * usb.h - USB Core Framework
 *
 * Kernel-side USB subsystem providing host controller abstraction,
 * device enumeration, and transfer primitives for class drivers.
 *
 * Architecture:
 *   usb_hcd      - Host Controller Driver interface
 *   usb_device   - Enumerated USB device with cached descriptors
 *   usb_endpoint - Endpoint abstraction (control/bulk/interrupt/iso)
 *   usb_transfer - Transfer request (URB equivalent)
 *
 * References:
 *   USB 2.0 Specification (usb.org)
 *   USB Mass Storage Class - Bulk Only Transport (usb.org)
 */

#ifndef _USB_H
#define _USB_H

#include <stdint.h>
#include <stddef.h>

/*
 * ============================================================
 * USB Constants
 * ============================================================
 */

/* Speeds */
#define USB_SPEED_LOW           0   /* 1.5 Mbps */
#define USB_SPEED_FULL          1   /* 12 Mbps */
#define USB_SPEED_HIGH          2   /* 480 Mbps */

/* Descriptor Types */
#define USB_DT_DEVICE           1
#define USB_DT_CONFIG           2
#define USB_DT_STRING           3
#define USB_DT_INTERFACE        4
#define USB_DT_ENDPOINT         5
#define USB_DT_SS_EP_COMP     0x30  /* SuperSpeed endpoint companion */
#define USB_DT_HUB             0x29

/* Standard Request Codes */
#define USB_REQ_GET_STATUS      0x00
#define USB_REQ_CLEAR_FEATURE   0x01
#define USB_REQ_SET_FEATURE     0x03
#define USB_REQ_SET_ADDRESS     0x05
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_REQ_SET_DESCRIPTOR  0x07
#define USB_REQ_GET_CONFIG      0x08
#define USB_REQ_SET_CONFIG      0x09
#define USB_REQ_GET_INTERFACE   0x0A
#define USB_REQ_SET_INTERFACE   0x0B

/* Request Types (bmRequestType) */
#define USB_DIR_OUT             0x00
#define USB_DIR_IN              0x80
#define USB_TYPE_STANDARD       (0 << 5)
#define USB_TYPE_CLASS          (1 << 5)
#define USB_TYPE_VENDOR         (2 << 5)
#define USB_RECIP_DEVICE        0x00
#define USB_RECIP_INTERFACE     0x01
#define USB_RECIP_ENDPOINT      0x02
#define USB_RECIP_OTHER         0x03

/* Endpoint Directions */
#define USB_EP_DIR_OUT          0x00
#define USB_EP_DIR_IN           0x80
#define USB_EP_DIR_MASK         0x80
#define USB_EP_NUM_MASK         0x0F

/* Endpoint Transfer Types */
#define USB_EP_TYPE_CONTROL     0
#define USB_EP_TYPE_ISO         1
#define USB_EP_TYPE_BULK        2
#define USB_EP_TYPE_INTERRUPT   3
#define USB_EP_TYPE_MASK        0x03

/* Device Classes */
#define USB_CLASS_PER_INTERFACE 0x00
#define USB_CLASS_AUDIO         0x01
#define USB_CLASS_HID           0x03
#define USB_CLASS_HUB           0x09
#define USB_CLASS_MASS_STORAGE  0x08

/* Audio (UAC) interface subclasses */
#define USB_SUBCLASS_AUDIOCONTROL    0x01
#define USB_SUBCLASS_AUDIOSTREAMING  0x02

/* Mass Storage Subclasses */
#define USB_MSC_SUBCLASS_SCSI   0x06

/* Mass Storage Protocols */
#define USB_MSC_PROTO_BOT       0x50  /* Bulk-Only Transport */
#define USB_MSC_PROTO_UAS       0x62  /* USB Attached SCSI */

/* Transfer status */
#define USB_XFER_OK             0
#define USB_XFER_STALL          (-1)
#define USB_XFER_NAK            (-2)
#define USB_XFER_TIMEOUT        (-3)
#define USB_XFER_ERROR          (-4)
#define USB_XFER_SHORT          (-5)

/* Endpoint feature selectors */
#define USB_FEATURE_HALT        0

/* Hub class-specific requests */
#define USB_HUB_REQ_GET_STATUS      0x00
#define USB_HUB_REQ_CLEAR_FEATURE   0x01
#define USB_HUB_REQ_SET_FEATURE     0x03
#define USB_HUB_REQ_GET_DESCRIPTOR  0x06

/* Hub port feature selectors (wValue for SET/CLEAR_FEATURE) */
#define USB_HUB_FEAT_PORT_ENABLE    1
#define USB_HUB_FEAT_PORT_SUSPEND   2
#define USB_HUB_FEAT_PORT_RESET     4
#define USB_HUB_FEAT_PORT_POWER     8
#define USB_HUB_FEAT_C_PORT_CONNECT 16
#define USB_HUB_FEAT_C_PORT_ENABLE  17
#define USB_HUB_FEAT_C_PORT_RESET   20

/* Hub port status bits */
#define USB_PORT_STAT_CONNECTION    0x0001
#define USB_PORT_STAT_ENABLE        0x0002
#define USB_PORT_STAT_RESET         0x0010
#define USB_PORT_STAT_POWER         0x0100
#define USB_PORT_STAT_LOW_SPEED     0x0200
#define USB_PORT_STAT_HIGH_SPEED    0x0400

/* Hub port change bits */
#define USB_PORT_STAT_C_CONNECTION  0x0001
#define USB_PORT_STAT_C_RESET       0x0010

/* Limits */
#define USB_MAX_DEVICES         32
#define USB_MAX_ENDPOINTS       16
/*
 * A device's configuration can expose many interfaces, and many alternate
 * settings per interface.  We record every interface descriptor we see (alt
 * settings included) so each endpoint can be attributed to the interface that
 * declared it; matching only ever considers alternate setting 0, since we do
 * not issue SET_INTERFACE during enumeration.  8 covers real composite
 * devices (a keyboard with a consumer-control interface, a UVC webcam, a UAC
 * headset) without making usb_device_t unreasonably large.
 */
#define USB_MAX_INTERFACES      8
/*
 * Sanity cap on a device's total configuration-descriptor length.  This is
 * not a buffer size -- config_data is allocated at the device's actual
 * wTotalLength -- only a bound so a hostile or wedged device can't make us
 * allocate up to the 64 KiB the field can encode.  It used to be 512, the
 * size of a fixed inline array, and any device with a larger config
 * descriptor was refused outright: real composite devices (UVC webcams, USB
 * audio, docking stations, keyboards with extra HID interfaces) commonly
 * report 1-3 KiB, and one reporting 1093 bytes is what exposed this.
 */
#define USB_MAX_CONFIG_SIZE     8192
#define USB_MAX_HCDS            4

/*
 * ============================================================
 * USB Descriptors (on-wire format, packed)
 * ============================================================
 */

struct usb_device_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed));

struct usb_config_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} __attribute__((packed));

struct usb_interface_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed));

struct usb_endpoint_descriptor {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed));

struct usb_hub_descriptor {
    uint8_t  bDescLength;
    uint8_t  bDescriptorType;
    uint8_t  bNbrPorts;
    uint16_t wHubCharacteristics;
    uint8_t  bPwrOn2PwrGood;
    uint8_t  bHubContrCurrent;
    uint8_t  DeviceRemovable[4];
} __attribute__((packed));

/* Control transfer setup packet */
struct usb_setup_packet {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed));

/*
 * ============================================================
 * USB Endpoint
 * ============================================================
 */
typedef struct usb_endpoint {
    uint8_t  address;       /* bEndpointAddress (dir | num) */
    uint8_t  type;          /* USB_EP_TYPE_* */
    uint16_t max_packet;    /* wMaxPacketSize */
    uint8_t  interval;      /* bInterval */
    uint8_t  toggle;        /* Data toggle (0 or 1) */
    uint8_t  max_streams;   /* SS companion MaxStreams exponent (0 = no streams) */
} usb_endpoint_t;

/*
 * One interface descriptor from the active configuration.  ep_first/ep_count
 * delimit this interface's endpoints inside the device's flat endpoints[]
 * array, so a driver can ask for "the interrupt IN endpoint of MY interface"
 * rather than "the first interrupt IN endpoint anywhere in the config" --
 * which on a composite device is frequently a different interface's.
 */
typedef struct usb_interface {
    uint8_t  number;        /* bInterfaceNumber */
    uint8_t  alt_setting;   /* bAlternateSetting */
    uint8_t  if_class;      /* bInterfaceClass */
    uint8_t  if_subclass;   /* bInterfaceSubClass */
    uint8_t  if_protocol;   /* bInterfaceProtocol */
    uint8_t  ep_first;      /* index of first endpoint in dev->endpoints[] */
    uint8_t  ep_count;      /* number of endpoints belonging to it */
} usb_interface_t;

/*
 * ============================================================
 * USB Device
 * ============================================================
 */
struct device;      /* /proc/devtree bus device (kern/device.h) */

typedef struct usb_device {
    uint8_t  address;           /* Assigned USB address (1-127) */
    uint8_t  speed;             /* USB_SPEED_* */
    uint8_t  port;              /* Hub port number (1-based) */
    uint8_t  config_value;      /* Active bConfigurationValue */
    uint16_t vendor_id;
    uint16_t product_id;

    /* Human-readable strings decoded from the device's iManufacturer /
     * iProduct string descriptors (UTF-16LE -> ASCII).  Empty if the device
     * exposes none or the fetch failed. */
    char     manufacturer[64];
    char     product[64];

    /* Cached full device descriptor */
    struct usb_device_descriptor dev_desc;

    /* Raw config descriptor data, allocated at config_len bytes during
     * enumeration and freed by usb_free_device().  NULL until then. */
    uint8_t  *config_data;
    uint16_t config_len;

    /* Parsed endpoints (excluding EP0) */
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint8_t  num_endpoints;

    /* Control endpoint (EP0) */
    usb_endpoint_t ep0;

    /*
     * Every interface descriptor in the active configuration, in descriptor
     * order, alternate settings included.
     */
    usb_interface_t interfaces[USB_MAX_INTERFACES];
    uint8_t  num_interfaces;

    /*
     * The interface this device is bound to -- set by usb_match_driver()
     * before it calls a driver's probe(), so probe/attach and every
     * USB_RECIP_INTERFACE control request see the interface actually being
     * driven rather than always interface 0.  Defaults to the first
     * interface so a device with no matching driver still reports something
     * meaningful.
     */
    uint8_t  if_number;
    uint8_t  if_class;
    uint8_t  if_subclass;
    uint8_t  if_protocol;

    /* Parent HCD */
    struct usb_hcd *hcd;

    /* Hub parent (NULL for root) */
    struct usb_device *parent;

    /* Class driver private data */
    void *driver_data;

    /* The class driver bound to this device (set by usb_match_driver on a
     * successful attach), so a later disconnect can dispatch its .detach. */
    struct usb_class_driver *driver;

    /* Teardown handles published at enumeration and torn down on disconnect,
     * so nothing outlives the freed usb_device_t (see usb_disconnect_device).
     * devtree_dev is the /proc/devtree bus node; usbfs_node / usbfs_meta_node
     * are the /dev/usb/... fs_node_t nodes (stored as void * to keep vfs.h out
     * of this header). */
    struct device *devtree_dev;
    void          *usbfs_node;
    void          *usbfs_meta_node;

    /* State */
    uint8_t  slot;              /* Index in device table */
    uint8_t  configured;        /* Device has been configured */
} usb_device_t;

/*
 * ============================================================
 * USB Transfer Request
 * ============================================================
 */
typedef struct usb_transfer {
    usb_device_t    *dev;
    usb_endpoint_t  *ep;
    void            *data;
    uint32_t         length;        /* Requested transfer length */
    uint32_t         actual_length; /* Actually transferred */
    int              status;        /* USB_XFER_* result */

    /* Setup packet for control transfers */
    struct usb_setup_packet setup;
    uint8_t  is_control;    /* 1 for control transfers */
    uint16_t stream_id;     /* xHCI bulk stream ID (0 = no stream) */

    /*
     * Per-transfer completion timeout in milliseconds; 0 means "use the HCD's
     * default".  An interrupt IN endpoint NAKs continuously while the device
     * has nothing to report, so a poll of one must give up after a few
     * milliseconds rather than sitting on the controller's multi-second bulk
     * timeout.
     */
    uint32_t timeout_ms;
} usb_transfer_t;

/*
 * ============================================================
 * USB Host Controller Driver Interface
 * ============================================================
 */
typedef struct usb_hcd {
    const char *name;           /* e.g., "uhci0" */
    uint8_t     hcd_index;      /* HCD instance number */

    /*
     * Submit a transfer request.
     * For control: setup packet + optional data stage
     * For bulk: data transfer on specified endpoint
     * Returns 0 on success, negative on error.
     * Synchronous — blocks until transfer is complete.
     */
    int (*submit)(struct usb_hcd *hcd, usb_transfer_t *xfer);

    /*
     * Root hub port operations.
     * nports: number of root hub ports
     * port_status: read port status register (returns wPortStatus | wPortChange << 16)
     * port_reset: issue port reset and wait for completion
     * port_enable: enable/disable port
     */
    uint8_t  nports;
    uint32_t (*port_status)(struct usb_hcd *hcd, uint8_t port);
    int      (*port_reset)(struct usb_hcd *hcd, uint8_t port);
    int      (*port_enable)(struct usb_hcd *hcd, uint8_t port, int enable);

    /*
     * Optional isochronous-OUT streaming ops (USB audio).  frame_number()
     * returns the controller's current frame index; iso_schedule() arms one
     * packet from a (coherent) DMA buffer at a future frame and returns an
     * opaque handle; iso_reclaim() releases it once the frame has passed.  The
     * caller keeps a window of packets scheduled a few frames ahead of
     * frame_number() for gapless playback.  NULL if the HCD has no iso support.
     */
    uint16_t (*frame_number)(struct usb_hcd *hcd);
    int      (*iso_schedule)(struct usb_hcd *hcd, usb_device_t *dev,
                             usb_endpoint_t *ep, uint16_t frame,
                             uint32_t buf_phys, uint16_t len, void **handle);
    void     (*iso_reclaim)(struct usb_hcd *hcd, void *handle);

    /* Private HC driver data */
    void *priv;

    /* Registry */
    struct usb_hcd *next;
} usb_hcd_t;

/*
 * ============================================================
 * USB Class Driver Interface
 * ============================================================
 */
typedef struct usb_class_driver {
    const char *name;
    uint8_t  if_class;
    uint8_t  if_subclass;
    uint8_t  if_protocol;

    /* Probe: return 0 if driver can handle this device */
    int (*probe)(usb_device_t *dev);

    /* Attach: bind driver to device */
    int (*attach)(usb_device_t *dev);

    /* Detach: unbind driver from device */
    void (*detach)(usb_device_t *dev);

    struct usb_class_driver *next;
} usb_class_driver_t;

/*
 * ============================================================
 * USB Core Public API
 * ============================================================
 */

/* Initialization */
void usb_init(void);
void usb_msc_init(void);
void uas_init(void);
void uac_init(void);
void usb_hid_init(void);
void usb_hid_mouse_init(void);
void usb_hub_init(void);

/* HCD Registration */
int  usb_register_hcd(usb_hcd_t *hcd);
void usb_unregister_hcd(usb_hcd_t *hcd);

/* Class Driver Registration */
int  usb_register_class_driver(usb_class_driver_t *drv);
void usb_unregister_class_driver(usb_class_driver_t *drv);

/* Device Management (called by enumeration engine) */
usb_device_t *usb_alloc_device(usb_hcd_t *hcd);
void usb_free_device(usb_device_t *dev);

/* Transfer API (synchronous) */
int usb_control_transfer(usb_device_t *dev,
                         uint8_t bmRequestType, uint8_t bRequest,
                         uint16_t wValue, uint16_t wIndex,
                         void *data, uint16_t wLength);

/*
 * Poll an interrupt endpoint once.  timeout_ms bounds how long to wait for the
 * device to produce a packet; a device with nothing to report NAKs for the
 * whole window and the call returns USB_XFER_NAK (or USB_XFER_TIMEOUT), which
 * is the normal idle result and not an error.
 */
int usb_interrupt_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                           void *data, uint32_t length,
                           uint32_t *actual_length, uint32_t timeout_ms);

int usb_bulk_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                      void *data, uint32_t length,
                      uint32_t *actual_length);

/* Like usb_bulk_transfer but tags the transfer with an xHCI bulk stream ID
 * (used by UAS over USB 3.0).  stream_id 0 behaves exactly like a plain bulk
 * transfer. */
int usb_bulk_stream_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                             uint16_t stream_id, void *data, uint32_t length,
                             uint32_t *actual_length);

int usb_iso_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                     void *data, uint32_t length,
                     uint32_t *actual_length);

/* Isochronous streaming (USB audio): schedule packets a few frames ahead of
 * usb_frame_number() and reclaim them once consumed.  buf_phys must be stable
 * (coherent) DMA memory owned by the caller until reclaim. */
uint16_t usb_frame_number(usb_device_t *dev);
int      usb_iso_schedule(usb_device_t *dev, usb_endpoint_t *ep, uint16_t frame,
                          uint32_t buf_phys, uint16_t len, void **handle);
void     usb_iso_reclaim(usb_device_t *dev, void *handle);

/* Standard Requests */
/*
 * Fetch string descriptor `index`, decode UTF-16LE -> NUL-terminated ASCII in
 * `buf` (non-ASCII -> '?').  `index` 0 yields "" (index 0 is the LANGID table).
 * Reads the 2-byte header first, then exactly bLength bytes, so it never
 * over-reads EP0.  Returns ASCII length, or a negative USB_XFER_* error.
 */
int usb_get_string(usb_device_t *dev, uint8_t index, char *buf, uint16_t bufsize);

int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                       void *buf, uint16_t size);
int usb_set_address(usb_device_t *dev, uint8_t address);
int usb_set_configuration(usb_device_t *dev, uint8_t config);
int usb_set_interface(usb_device_t *dev, uint8_t iface, uint8_t alt);
int usb_clear_halt(usb_device_t *dev, usb_endpoint_t *ep);

/* Enumeration.  usb_enumerate_device_parent records the enumerating hub as the
 * new device's parent (NULL = a root-hub port); usb_enumerate_device is the
 * root-port shorthand (parent == NULL). */
int usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed);
int usb_enumerate_device_parent(usb_hcd_t *hcd, uint8_t port, uint8_t speed,
                                usb_device_t *parent);

/* Hot-plug plumbing.  usb_hub_scan_ports() is called by usb_hotplug_scan()
 * on the hot-plug kthread; the other two let the hub driver reconcile its
 * downstream ports using the core's device table and teardown path. */
void usb_hub_scan_ports(void);
void usb_disconnect_device(usb_device_t *dev);
usb_device_t *usb_child_device_on_port(usb_device_t *parent, uint8_t port);

/* Publish/unpublish a device under /dev/usb (libusb/lsusb backend). */
void usbdevfs_publish(usb_device_t *dev);
void usbdevfs_unpublish(usb_device_t *dev);
void usb_enumerate_bus(usb_hcd_t *hcd);

/* Endpoint Lookup Helpers */
/*
 * Find an endpoint of the given type and direction belonging to the interface
 * the device is bound to (dev->if_number, alternate setting 0).  Scoping to
 * the bound interface is what stops a composite device from handing a driver
 * a different interface's endpoint.
 */
usb_endpoint_t *usb_find_endpoint(usb_device_t *dev, uint8_t type, uint8_t dir);

/* As above, but for an explicit interface number and alternate setting. */
usb_endpoint_t *usb_find_endpoint_iface(usb_device_t *dev, uint8_t ifnum,
                                        uint8_t alt, uint8_t type, uint8_t dir);

#endif /* _USB_H */
