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
#define USB_CLASS_HUB           0x09
#define USB_CLASS_MASS_STORAGE  0x08

/* Mass Storage Subclasses */
#define USB_MSC_SUBCLASS_SCSI   0x06

/* Mass Storage Protocols */
#define USB_MSC_PROTO_BOT       0x50  /* Bulk-Only Transport */

/* Transfer status */
#define USB_XFER_OK             0
#define USB_XFER_STALL          (-1)
#define USB_XFER_NAK            (-2)
#define USB_XFER_TIMEOUT        (-3)
#define USB_XFER_ERROR          (-4)
#define USB_XFER_SHORT          (-5)

/* Endpoint feature selectors */
#define USB_FEATURE_HALT        0

/* Hub features */
#define USB_HUB_FEAT_PORT_RESET     4
#define USB_HUB_FEAT_PORT_POWER     8
#define USB_HUB_FEAT_C_PORT_CONNECT 16
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
#define USB_MAX_CONFIG_SIZE     512
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
} usb_endpoint_t;

/*
 * ============================================================
 * USB Device
 * ============================================================
 */
typedef struct usb_device {
    uint8_t  address;           /* Assigned USB address (1-127) */
    uint8_t  speed;             /* USB_SPEED_* */
    uint8_t  port;              /* Hub port number (1-based) */
    uint8_t  config_value;      /* Active bConfigurationValue */
    uint16_t vendor_id;
    uint16_t product_id;

    /* Cached full device descriptor */
    struct usb_device_descriptor dev_desc;

    /* Raw config descriptor data */
    uint8_t  config_data[USB_MAX_CONFIG_SIZE];
    uint16_t config_len;

    /* Parsed endpoints (excluding EP0) */
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint8_t  num_endpoints;

    /* Control endpoint (EP0) */
    usb_endpoint_t ep0;

    /* Interface class info from first interface */
    uint8_t  if_class;
    uint8_t  if_subclass;
    uint8_t  if_protocol;

    /* Parent HCD */
    struct usb_hcd *hcd;

    /* Hub parent (NULL for root) */
    struct usb_device *parent;

    /* Class driver private data */
    void *driver_data;

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

int usb_bulk_transfer(usb_device_t *dev, usb_endpoint_t *ep,
                      void *data, uint32_t length,
                      uint32_t *actual_length);

/* Standard Requests */
int usb_get_descriptor(usb_device_t *dev, uint8_t type, uint8_t index,
                       void *buf, uint16_t size);
int usb_set_address(usb_device_t *dev, uint8_t address);
int usb_set_configuration(usb_device_t *dev, uint8_t config);
int usb_clear_halt(usb_device_t *dev, usb_endpoint_t *ep);

/* Enumeration */
int usb_enumerate_device(usb_hcd_t *hcd, uint8_t port, uint8_t speed);
void usb_enumerate_bus(usb_hcd_t *hcd);

/* Endpoint Lookup Helpers */
usb_endpoint_t *usb_find_endpoint(usb_device_t *dev, uint8_t type, uint8_t dir);

#endif /* _USB_H */
