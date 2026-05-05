/*
 * usb_hub.c - USB Hub Class Driver
 *
 * Enumerates devices attached to external USB hubs by reading port
 * status, resetting connected ports, and calling into the core USB
 * enumeration path for each downstream device.
 *
 * QEMU auto-creates a virtual hub (0409:55aa) when more USB devices
 * are present than the host controller has root ports.  Without this
 * driver those downstream devices (e.g. mass storage) are invisible.
 *
 * References:
 *   USB 2.0 Specification, Chapter 11 (Hub Specification)
 */

#include "usb.h"
#include <kern/console.h>
#include <kern/time.h>
#include <string.h>

/*
 * ============================================================
 * Hub Port Status (GET_STATUS response, 4 bytes)
 * ============================================================
 */

struct usb_hub_port_status {
	uint16_t wPortStatus;
	uint16_t wPortChange;
} __attribute__((packed));

/*
 * ============================================================
 * Per-Hub State
 * ============================================================
 */

#define USB_HUB_MAX_DEVICES     4
#define USB_HUB_MAX_PORTS       8

typedef struct usb_hub_dev {
	usb_device_t    *udev;
	uint8_t          nports;
	uint8_t          active;
	uint8_t          pwr_on_2_pwr_good_2ms;  /* hub descriptor field */
} usb_hub_dev_t;

static usb_hub_dev_t hub_devices[USB_HUB_MAX_DEVICES];

/*
 * ============================================================
 * Hub Class Requests (port-directed control transfers)
 * ============================================================
 */

static int usb_hub_get_port_status(usb_device_t *dev, uint8_t port,
                                   struct usb_hub_port_status *st)
{
	return usb_control_transfer(dev,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER,
		USB_HUB_REQ_GET_STATUS,
		0, port,
		st, sizeof(*st));
}

static int usb_hub_set_port_feature(usb_device_t *dev, uint8_t port,
                                    uint16_t feature)
{
	return usb_control_transfer(dev,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER,
		USB_HUB_REQ_SET_FEATURE,
		feature, port,
		NULL, 0);
}

static int usb_hub_clear_port_feature(usb_device_t *dev, uint8_t port,
                                      uint16_t feature)
{
	return usb_control_transfer(dev,
		USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER,
		USB_HUB_REQ_CLEAR_FEATURE,
		feature, port,
		NULL, 0);
}

/*
 * ============================================================
 * Hub Port Enumeration
 *
 * For each port: power on, wait, check connection, reset,
 * wait for reset completion, then enumerate the downstream device.
 * ============================================================
 */

static void usb_hub_enumerate_ports(usb_hub_dev_t *hub)
{
	struct usb_hub_port_status ps;
	uint64_t deadline;

	for(uint8_t port = 1; port <= hub->nports; port++) {
		/* Power the port on */
		usb_hub_set_port_feature(hub->udev, port,
		                         USB_HUB_FEAT_PORT_POWER);
	}

	/* Wait for power to stabilize.  USB spec: hub descriptor's
	 * bPwrOn2PwrGood is the time in 2ms units; minimum 100ms.
	 * Trusting only the hard-coded 100ms breaks slow-turn-on hubs
	 * that report e.g. 200ms — devices behind those hubs would be
	 * reset before they had power. */
	{
		uint32_t pwr_ms = hub->pwr_on_2_pwr_good_2ms * 2u;
		if (pwr_ms < 100u) pwr_ms = 100u;
		deadline = (uint64_t)get_uptime_ms() + pwr_ms;
		while ((uint64_t)get_uptime_ms() < deadline)
			__asm__ volatile("pause");
	}

	for(uint8_t port = 1; port <= hub->nports; port++) {
		/* Read port status */
		if(usb_hub_get_port_status(hub->udev, port, &ps) != USB_XFER_OK)
			continue;

		/* Clear any pending connection change */
		if(ps.wPortChange & USB_PORT_STAT_C_CONNECTION)
			usb_hub_clear_port_feature(hub->udev, port,
			                           USB_HUB_FEAT_C_PORT_CONNECT);

		/* Skip if nothing connected */
		if(!(ps.wPortStatus & USB_PORT_STAT_CONNECTION))
			continue;

		/* Reset the port */
		if(usb_hub_set_port_feature(hub->udev, port,
		                            USB_HUB_FEAT_PORT_RESET) != USB_XFER_OK) {
			kprintf("usb_hub: port %u reset request failed\n", port);
			continue;
		}

		/* Wait for reset to complete (up to 200ms) */
		deadline = (uint64_t)get_uptime_ms() + 200;
		for(;;) {
			if((uint64_t)get_uptime_ms() > deadline) {
				kprintf("usb_hub: port %u reset timeout\n", port);
				break;
			}

			/* Brief pause between polls */
			{
				uint64_t d = (uint64_t)get_uptime_ms() + 10;
				while((uint64_t)get_uptime_ms() < d)
					__asm__ volatile("pause");
			}

			if(usb_hub_get_port_status(hub->udev, port, &ps) != USB_XFER_OK)
				break;

			/* Reset complete when C_PORT_RESET is set */
			if(ps.wPortChange & USB_PORT_STAT_C_RESET) {
				usb_hub_clear_port_feature(hub->udev, port,
				                           USB_HUB_FEAT_C_PORT_RESET);
				break;
			}
		}

		/* Re-read status after reset */
		if(usb_hub_get_port_status(hub->udev, port, &ps) != USB_XFER_OK)
			continue;

		/* Port must be enabled after reset */
		if(!(ps.wPortStatus & USB_PORT_STAT_ENABLE))
			continue;

		/* Determine speed */
		uint8_t speed;
		if(ps.wPortStatus & USB_PORT_STAT_LOW_SPEED)
			speed = USB_SPEED_LOW;
		else if(ps.wPortStatus & USB_PORT_STAT_HIGH_SPEED)
			speed = USB_SPEED_HIGH;
		else
			speed = USB_SPEED_FULL;

		/* Small settle delay after reset */
		{
			uint64_t d = (uint64_t)get_uptime_ms() + 10;
			while((uint64_t)get_uptime_ms() < d)
				__asm__ volatile("pause");
		}

		/* Enumerate the downstream device through the core USB stack */
		usb_enumerate_device(hub->udev->hcd, port, speed);
	}
}

/*
 * ============================================================
 * Class Driver Callbacks
 * ============================================================
 */

static int usb_hub_probe(usb_device_t *dev)
{
	/* Accept any hub device (class 0x09) */
	if(dev->if_class == USB_CLASS_HUB ||
	   dev->dev_desc.bDeviceClass == USB_CLASS_HUB)
		return 0;

	return -1;
}

static int usb_hub_attach(usb_device_t *dev)
{
	usb_hub_dev_t *hub = NULL;
	struct usb_hub_descriptor hdesc;
	int slot;
	int ret;

	/* Find free slot */
	for(slot = 0; slot < USB_HUB_MAX_DEVICES; slot++) {
		if(!hub_devices[slot].active) {
			hub = &hub_devices[slot];
			break;
		}
	}
	if(!hub) {
		kprintf("usb_hub: no free hub slots\n");
		return -1;
	}

	memset(hub, 0, sizeof(*hub));
	hub->udev = dev;

	/* Read hub descriptor to learn port count */
	memset(&hdesc, 0, sizeof(hdesc));
	ret = usb_control_transfer(dev,
		USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE,
		USB_HUB_REQ_GET_DESCRIPTOR,
		(uint16_t)(USB_DT_HUB << 8),
		0,
		&hdesc, sizeof(hdesc));

	if(ret != USB_XFER_OK) {
		kprintf("usb_hub: failed to read hub descriptor (err=%d)\n", ret);
		return -1;
	}

	hub->nports = hdesc.bNbrPorts;
	if(hub->nports > USB_HUB_MAX_PORTS)
		hub->nports = USB_HUB_MAX_PORTS;
	hub->pwr_on_2_pwr_good_2ms = hdesc.bPwrOn2PwrGood;

	hub->active = 1;
	dev->driver_data = hub;

	kprintf("usb_hub: %04x:%04x hub with %u port(s)\n",
	        dev->vendor_id, dev->product_id, hub->nports);

	/* Enumerate connected downstream devices */
	usb_hub_enumerate_ports(hub);

	return 0;
}

static void usb_hub_detach(usb_device_t *dev)
{
	usb_hub_dev_t *hub = dev->driver_data;

	if(!hub)
		return;

	hub->active = 0;
	hub->udev = NULL;
	dev->driver_data = NULL;

	kprintf("usb_hub: detached hub\n");
}

/*
 * ============================================================
 * Driver Registration
 * ============================================================
 */

static usb_class_driver_t hub_driver = {
	.name         = "usb-hub",
	.if_class     = USB_CLASS_HUB,
	.if_subclass  = 0xFF,       /* match any subclass */
	.if_protocol  = 0xFF,       /* match any protocol */
	.probe        = usb_hub_probe,
	.attach       = usb_hub_attach,
	.detach       = usb_hub_detach,
};

void usb_hub_init(void)
{
	memset(hub_devices, 0, sizeof(hub_devices));
	usb_register_class_driver(&hub_driver);
}
