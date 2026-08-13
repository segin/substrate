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
#define USB_SPEED_SUPER         3   /* 5 Gbps -- USB 3.x */

/* Descriptor Types */
#define USB_DT_DEVICE           1
#define USB_DT_CONFIG           2
#define USB_DT_STRING           3
#define USB_DT_INTERFACE        4
#define USB_DT_ENDPOINT         5
#define USB_DT_SS_EP_COMP     0x30  /* SuperSpeed endpoint companion */
#define USB_DT_HUB             0x29
#define USB_DT_SS_HUB          0x2A  /* SuperSpeed hubs answer to this, not 0x29 */

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

/*
 * Transfer status taxonomy. [RF-11]
 *
 * The distinction callers act on:
 *   STALL   -- the DEVICE said no (protocol stall / functional halt).
 *              Recovery is usb_clear_halt; class drivers may treat it as
 *              a durable answer (usb_hid latches ctl_poll_refused).
 *   ERROR   -- the TRANSPORT broke (CRC/babble/bitstuff/buffer error,
 *              error-counter exhaustion, dead controller).  Retrying or
 *              reset recovery may help; clear-halt will not.
 *   TIMEOUT -- nothing answered within the caller's deadline.
 *   NAK     -- polled endpoint had no data.  Synthesized by class-driver
 *              polling loops (usb_hid); the HCDs themselves never return
 *              it from submit.
 *   SHORT   -- reserved: no HCD currently produces it; short reads
 *              return OK with actual_length < requested.
 *
 * Contract: the submit return value is authoritative, and the core's
 * transfer wrappers stamp xfer.status with it after every submit, so the
 * two never disagree.  Every HCD classifies with the same policy
 * (per-driver encodings: ehci_halt_status, uhci_td_status,
 * xhci_xfer_status).
 */
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

/* Root-port status extras.  The core learns a port's speed from these bits;
 * SuperSpeed has no encoding in the USB 2.0 hub status word, so it gets a
 * private one the HCDs set and usb_enumerate_* reads. */
#define USB_PORT_STAT_SUPER_SPEED   0x0800

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
/*
 * Total devices tracked across all controllers.  Raised with the hub limits:
 * 16 hubs would otherwise consume half of a 32-entry table before a single
 * peripheral was plugged in, so lifting the hub caps without this one would
 * not actually let you attach more.  128 matches the USB address space that
 * usb_addr_bitmap already covers (1..127); usb_devices[] is a pointer array,
 * so the whole increase costs 512 bytes.
 */
#define USB_MAX_DEVICES         128

/*
 * A port that reports a device but fails to enumerate is retried this many
 * times and then PARKED until its connection state actually changes.
 *
 * Without this the hot-plug scan is an infinite retry loop: the port still
 * reads connected, no device is tracked on it, so every pass resets it and
 * re-runs enumeration -- printing a fresh failure each time.  On the Lenovo
 * C460 that produced descriptor-read errors at exactly the scan rate (4 Hz)
 * and took the kernel down within seconds of reaching userspace.
 */
#define USB_ENUM_MAX_TRIES      3
/*
 * Attempts at the initial 8-byte device-descriptor read before a port is
 * declared unenumerable, and the pause between them.  A device slower than
 * spec -- which describes most cheap hubs and keyboards in the window right
 * after a port reset -- does not answer the first request, and giving up there
 * made such a device permanently invisible.  NetBSD (usb_subr.c,
 * usbd_new_device) uses exactly these numbers and re-resets the port every
 * fourth attempt; FreeBSD re-enumerates twice.  Only a port that is failing
 * pays the cost, and USB_ENUM_MAX_TRIES then bounds how often that repeats.
 */
#define USB_ENUM_DESC_TRIES     10
#define USB_ENUM_DESC_DELAY_MS  200
/*
 * Rounds of the descriptor-read loop.  Round 2 begins by cutting power to a
 * root port -- the escalation past a bus reset, for a device left wedged by
 * whatever software owned it before us.
 *
 * A device that has just lost power must then be given the time to come back
 * from cold, and that is a much bigger number than the retry spacing: a card
 * reader powers up, initialises whatever media is inserted, and only then
 * answers USB at all.  The first version of this escalated at attempt 7 of
 * 10 and so allowed ~500ms after the cycle -- it cut power and gave up
 * before the device could possibly have returned, on every single retry.
 * Hence a full fresh round rather than the tail of the old one. [HW-04]
 */
#define USB_ENUM_DESC_ROUNDS      2
/*
 * Waiting out a power cycle is a WAIT FOR AN EVENT, not a fixed sleep.  A
 * blind 1500 ms delay followed by an immediate port reset is a guess, and for
 * a USB SD-card reader it is the wrong one: the device powers up, brings up
 * its own controller and initialises the inserted media before it drives D+,
 * which can take several seconds.  Resetting at a fixed 1.5 s therefore lands
 * while the device is still coming up -- so it never answers, and the port
 * enumerates roughly one attempt in thirty.
 *
 * So: sleep SETTLE_MS (dead time -- VBUS has only just come back and nothing
 * can be reported before the device's own power-on reset completes), then
 * POLL the port until it reports a connection, up to WAIT_MS in total.  A
 * fast device costs the same as before; a slow one gets the time it actually
 * needs.  DEBOUNCE_MS after CCS asserts covers the link still settling --
 * connection status can bounce before the bus is stable.
 */
#define USB_ENUM_POWER_SETTLE_MS    1500
#define USB_ENUM_POWER_WAIT_MS      8000
#define USB_ENUM_POWER_POLL_MS      100
#define USB_ENUM_POWER_DEBOUNCE_MS  250
/*
 * Root ports the per-port enumeration-failure counter covers.  Ports are
 * 1-based and the array is indexed [port - 1], so this is the highest port
 * number that can be throttled.  It used to be indexed [port] against a
 * 32-entry array, which both wasted slot 0 and left port 32 and up with no
 * counter at all -- and a port with no counter is one the retry cap cannot
 * stop from re-probing forever, which is the exact loop USB_ENUM_MAX_TRIES
 * exists to break.  xHCI encodes up to 255 ports; 128 covers every real
 * controller at one byte per port per HCD.
 */
#define USB_MAX_ROOT_PORTS      128
/* USB allows at most 7 tiers of hubs; every parent-chain walk is bounded by
 * this so a corrupted pointer cannot loop forever. */
#define USB_MAX_ENUM_DEPTH      7
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
/* How long an HCD waits for firmware to release its controller during the
 * BIOS/OS ownership handoff before claiming it anyway.  Shared by the EHCI
 * (PCI-config EECP) and xHCI (MMIO xECP) handoffs -- the walks differ per
 * spec, the patience does not. [RF-9] */
#define USB_BIOS_HANDOFF_WAIT_MS 5000

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
/*
 * wMaxPacketSize is not just a size: for a high-speed (or SuperSpeed)
 * interrupt or isochronous endpoint, bits 12:11 hold "additional transactions
 * per microframe" and only bits 10:0 are the packet size.  Storing the raw
 * field meant a high-bandwidth endpoint declaring 2 x 1024 was read as a
 * 5120-byte packet -- which xHCI wrote straight into the EP context's Max
 * Packet Size field, a value no controller will accept.  Keep the two apart.
 */
#define USB_EP_MPS_MASK         0x07FF
#define USB_EP_MPS_MULT(x)      ((((x) >> 11) & 0x3) + 1)

typedef struct usb_endpoint {
    uint8_t  address;       /* bEndpointAddress (dir | num) */
    uint8_t  type;          /* USB_EP_TYPE_* */
    uint16_t max_packet;    /* wMaxPacketSize bits 10:0 -- the size alone */
    uint8_t  mult;          /* transactions per microframe (1..3; 1 = normal) */
    uint8_t  interval;      /* bInterval */
    uint8_t  toggle;        /* Data toggle (0 or 1) */
    uint8_t  max_streams;   /* SS companion MaxStreams exponent (0 = no streams) */
    uint8_t  max_burst;     /* SS companion bMaxBurst: packets per burst - 1 */
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
    /*
     * Per-interface binding.  A composite device is several functions behind
     * one address -- the SHARKOON 1ea7:0066 dongle is a keyboard on interface
     * 0 and a mouse on interface 1 -- and each needs its own driver and its
     * own private state.  dev->driver/dev->driver_data hold whichever
     * interface is currently being dispatched; these are the durable copies.
     * [HW-07]
     */
    struct usb_class_driver *driver;
    void                    *driver_data;
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

    /* Hub topology.  Set by usb_set_hub() when the hub driver attaches; the
     * xHCI slot context needs both before it will route to anything behind
     * this device. */
    uint8_t  is_hub;
    uint8_t  hub_nports;
    /* TT Think Time, as the 0..3 code the xHCI slot context wants (0 = 8 FS
     * bit times, 3 = 32).  Straight out of wHubCharacteristics bits 6:5. */
    uint8_t  hub_ttt;

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
    /* Consecutive failed enumeration attempts per root port; cleared when the
     * port goes disconnected so a re-plug always gets a fresh try. */
    uint8_t  enum_fail[USB_MAX_ROOT_PORTS];

    /*
     * Dead-controller latch. [RF-2]
     *
     * hc_failed is written by the HCD when the controller is beyond use --
     * the schedule refused a verified stop, or the hardware reported it
     * halted itself (EHCI USBSTS HSE/HCHalted, xHCI HSE/HCE).  The core
     * then fails every transfer fast instead of letting each one burn its
     * full timeout against hardware that will never answer, and the
     * hot-plug scanner stops resetting the dead controller's ports.
     * hc_failed_reported is core-owned: the one-shot flag that keeps a
     * dead controller from re-logging on every poll forever.
     */
    int      hc_failed;
    int      hc_failed_reported;

    /* The bus device this controller attached as.  struct device carries no
     * driver-private pointer, so .shutdown dispatch resolves the HCD through
     * usb_hcd_by_kdev() instead of per-driver registries. [RF-5] */
    struct device *kdev;
    uint32_t (*port_status)(struct usb_hcd *hcd, uint8_t port);
    int      (*port_reset)(struct usb_hcd *hcd, uint8_t port);
    int      (*port_enable)(struct usb_hcd *hcd, uint8_t port, int enable);

    /*
     * Optional: the core has learned this device is a hub with `nports`
     * downstream ports.  xHCI must set the Hub bit and port count in the slot
     * context or the controller will not route transfers past it; UHCI and
     * EHCI need nothing and leave this NULL.
     */
    int      (*set_hub)(struct usb_hcd *hcd, usb_device_t *dev, uint8_t nports,
                        uint8_t ttt);

    /*
     * Optional: cut power to a root port and restore it.  The escalation
     * beyond a bus reset -- a reset asks a device to restart its state
     * machine, a power cycle removes the choice.  It is the recovery for a
     * device left mid-conversation by other software, which on the HP
     * Pavilion is the SD-card reader the firmware was driving as its own
     * boot disk until the xHCI BIOS handoff took the controller away.
     * NULL on HCDs with no per-port power control. [HW-03]
     */
    int      (*port_power_cycle)(struct usb_hcd *hcd, uint8_t port);

    /*
     * Optional: the real EP0 max packet size has been read from the device
     * descriptor.  xHCI programs an endpoint context at Address Device time
     * from the core's guess and must be corrected once the true value is known
     * (xHCI 1.1 s4.3.4); UHCI and EHCI read ep0.max_packet per transfer and
     * need nothing.
     */
    int      (*set_ep0_mps)(struct usb_hcd *hcd, usb_device_t *dev, uint16_t mps);

    /*
     * Optional: a root port has gone disconnected, so the HCD may release any
     * controller-side state still bound to it.  Called from the hot-plug scan
     * for every unoccupied port, so it must be cheap when there is nothing to
     * do.  xHCI uses it to disable the device slot and free its contexts and
     * transfer rings; UHCI and EHCI keep no such state and leave this NULL.
     *
     * This exists so that port_status() can be a pure read.  It used to do the
     * teardown itself, which made a routine status poll take the submit lock
     * and run commands on the command ring -- a trap for any future caller.
     */
    void     (*port_gone)(struct usb_hcd *hcd, uint8_t port);

    /*
     * Optional isochronous-OUT streaming ops (USB audio).  frame_number()
     * returns the controller's current frame index; iso_schedule() arms one
     * packet from a (coherent) DMA buffer at a future frame and returns an
     * opaque handle; iso_reclaim() releases it once the frame has passed.  The
     * caller keeps a window of packets scheduled a few frames ahead of
     * frame_number() for gapless playback.  NULL if the HCD has no iso support.
     */
    /*
     * The modulus of the frame space frame_number() counts in and
     * iso_schedule() targets: 1024 on UHCI (the frame-list size), 2048 on
     * xHCI (the Frame Index portion of MFINDEX, and xHCI 1.2 s4.11.2.5:
     * "The Frame ID value is calculated as the modulus of 2048").  The two
     * differ, so a scheduler that hard-codes one breaks on the other --
     * uac's wrap arithmetic must add THIS, not a constant.  0 is read as
     * 1024 so an HCD that predates the field keeps its old behaviour.
     */
    uint16_t iso_frame_modulus;
    uint16_t (*frame_number)(struct usb_hcd *hcd);
    int      (*iso_schedule)(struct usb_hcd *hcd, usb_device_t *dev,
                             usb_endpoint_t *ep, uint16_t frame,
                             uint32_t buf_phys, uint16_t len, void **handle);
    void     (*iso_reclaim)(struct usb_hcd *hcd, void *handle);
    /*
     * Optional: the iso stream on `ep` has gone idle and no further packets
     * are coming for a while.  On xHCI this parks the endpoint cleanly: an
     * empty ring raises one Ring Underrun Transfer Event when first detected
     * and the xHC then removes the endpoint from its Pipe Schedule until the
     * next doorbell (xHCI 1.2 s4.11.2.3, s4.10.3.1) -- P5-03's original
     * claim of an event per interval flooding the event ring was WRONG, see
     * the pass-6 audit -- so this is tidiness (quiesce + dequeue resync at
     * a known point), not flood protection.  The next iso_schedule()'s
     * doorbell restarts a Stopped endpoint (s4.8.3).  UHCI reclaims its
     * frame-list slots individually and leaves this NULL. [P5-03, P6-ISO-01]
     */
    void     (*iso_stop)(struct usb_hcd *hcd, usb_device_t *dev,
                         usb_endpoint_t *ep);
    /*
     * Optional, required for iso IN: poll one armed IN packet's fate.
     * Returns 1 and the received byte count once the packet's frame has
     * been serviced (consuming the handle -- it is dead afterwards), 0
     * while still pending, negative if the packet failed or the endpoint
     * stopped.  Iso IN differs from OUT in that the caller cannot infer
     * completion from the frame counter alone: a capture packet's LENGTH
     * is only knowable from the completion, so IN packets are armed with
     * an event requested and this is how the caller collects it.  A
     * handle abandoned without polling must still be released with
     * iso_reclaim(). [T3]
     */
    int      (*iso_in_status)(struct usb_hcd *hcd, void *handle,
                              uint32_t *out_len);

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
/* Starts the hot-plug monitor.  Must run after kmain has spawned init, or
 * the monitor takes PID 1.  See the note in usb.c. */
void usb_late_init(void);
/* One synchronous scan; returns how many devices it newly enumerated, so a
 * caller can act only when the bus actually changed.  See rootwait. [HW-02] */
int  usb_hotplug_poll(void);
void usb_msc_init(void);
void uas_init(void);
void uac_init(void);
void usb_hid_init(void);
void usb_hid_mouse_init(void);
void usb_hub_init(void);

/* Millisecond busy-wait (pause spin) shared by the HCDs and hub code. [RF-12] */
void usb_delay_ms(uint32_t ms);

/* HCD Registration */
int  usb_register_hcd(usb_hcd_t *hcd);
/* Resolve a registered HCD from its bus device (shutdown dispatch). [RF-5] */
usb_hcd_t *usb_hcd_by_kdev(struct device *dev);
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
 * As above, but also reports how many bytes actually moved.  The plain form
 * returns only a USB_XFER_* status (USB_XFER_OK is 0), so a caller that needs
 * a length -- usbfs relaying a control transfer to userspace -- cannot get one
 * from it.
 */
int usb_control_transfer_actual(usb_device_t *dev,
                                uint8_t bmRequestType, uint8_t bRequest,
                                uint16_t wValue, uint16_t wIndex,
                                void *data, uint16_t wLength,
                                uint32_t *actual_length);

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
void     usb_iso_stop(usb_device_t *dev, usb_endpoint_t *ep);
int      usb_iso_in_status(usb_device_t *dev, void *handle, uint32_t *out_len);

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
/* Reset one downstream port of an attached hub (a class request, so only the
 * hub driver can issue it).  Used by the core enumeration path to retry a slow
 * device and to re-reset the port before SET_ADDRESS. */
int  usb_hub_reset_port(usb_device_t *hubdev, uint8_t port);
void usb_disconnect_device(usb_device_t *dev);
usb_device_t *usb_child_device_on_port(usb_device_t *parent, uint8_t port);

/*
 * ---- Bus topology ----
 *
 * A device behind a hub cannot be addressed from its port number alone: xHCI
 * needs a Route String naming the port at every tier, and EHCI needs the
 * address and port of the nearest upstream high-speed hub so it can wrap the
 * transfer in split transactions.  Both are derived from the dev->parent chain,
 * which enumeration already records.
 */

/* Root-hub port this device ultimately hangs off, walking up through any hubs. */
uint8_t usb_root_port(const usb_device_t *dev);

/*
 * xHCI Route String (xHCI 1.1 s4.5.2): 4 bits per tier, the port on the
 * root-hub's immediate downstream hub in bits 3:0 and each deeper tier above
 * it.  0 for a device attached straight to a root port.  Port numbers above 15
 * cannot be encoded and clamp to 15, as the spec requires.
 */
uint32_t usb_route_string(const usb_device_t *dev);

/*
 * The nearest upstream high-speed hub for a low/full-speed device -- the one
 * whose transaction translator has to bridge it onto the high-speed bus.  NULL
 * when the device is high-speed itself, or is on a root port, or no high-speed
 * hub is in its path.  *ttport, when non-NULL, gets the port number this
 * device's branch occupies on that hub.
 */
usb_device_t *usb_tt_hub(const usb_device_t *dev, uint8_t *ttport);

/*
 * Record that a device is a hub with `nports` downstream ports.  Called by the
 * hub driver at attach.  xHCI has to be told before it will route transfers to
 * anything behind the hub; other controllers do not care.
 */
void usb_set_hub(usb_device_t *dev, uint8_t nports, uint8_t ttt);

/* TT Think Time sub-field of wHubCharacteristics (USB 2.0 Table 11-13),
 * already scaled to the 0..3 code xHCI's slot context uses. */
#define USB_HUB_TT_THINK(wHubChar)   (((wHubChar) >> 5) & 0x3)

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
