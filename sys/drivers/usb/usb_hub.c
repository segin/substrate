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

#include <string.h>

#include <drivers/usb/usb.h>
#include <kern/console.h>
#include <kern/time.h>

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

/*
 * USB_HUB_MAX_DEVICES bounds hub_devices[], so it caps how many hubs can be
 * attached at once; a fifth hub was simply refused.  USB allows five tiers of
 * external hubs, and a branching tree reaches four trivially (a monitor hub
 * feeding a dock feeding a keyboard hub is three before anything is plugged
 * in).  Each entry is 8 bytes.
 *
 * USB_HUB_MAX_PORTS only CLAMPS hub->nports -- nothing is indexed by port
 * number -- so ports above it were silently ignored: a 10-port hub presented
 * as a 8-port one, with the last two dead and no diagnostic.  15 is the most
 * downstream ports a USB 3.x hub can report, so nothing real is truncated
 * now, and raising it further costs only loop iterations.
 */
#define USB_HUB_MAX_DEVICES     16
#define USB_HUB_MAX_PORTS       15

typedef struct usb_hub_dev {
	usb_device_t    *udev;
	uint8_t          nports;
	uint8_t          active;
	uint8_t          pwr_on_2_pwr_good_2ms;  /* hub descriptor field */
	/* Consecutive failed enumeration attempts per downstream port, cleared
	 * on disconnect.  See USB_ENUM_MAX_TRIES: without this a port holding a
	 * device we cannot enumerate is reset and re-probed at the scan rate
	 * forever. */
	uint8_t          enum_fail[USB_HUB_MAX_PORTS + 1];
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

/*
 * Drive SET_FEATURE(PORT_RESET) on one downstream port and wait for the hub to
 * report it complete.  Returns 0 on success.
 *
 * Exported (as usb_hub_reset_port) because the core enumeration path needs it:
 * a device that fails its first descriptor read has to be re-reset and retried,
 * and the port must be reset again between that read and SET_ADDRESS.  For a
 * root-hub port the core uses hcd->port_reset; for anything behind a hub, the
 * reset is a class request and only this driver can issue it.
 */
static int usb_hub_do_port_reset(usb_device_t *hubdev, uint8_t port)
{
	struct usb_hub_port_status ps;
	uint64_t deadline;

	if(usb_hub_set_port_feature(hubdev, port,
	                            USB_HUB_FEAT_PORT_RESET) != USB_XFER_OK) {
		kprintf("usb_hub: port %u reset request failed\n", port);
		return -1;
	}

	/* Wait for reset to complete (up to 200ms) */
	deadline = (uint64_t)get_uptime_ms() + 200;
	for(;;) {
		if((uint64_t)get_uptime_ms() > deadline) {
			kprintf("usb_hub: port %u reset timeout\n", port);
			return -1;
		}

		/* Brief pause between polls */
		{
			uint64_t d = (uint64_t)get_uptime_ms() + 10;
			while((uint64_t)get_uptime_ms() < d)
				__asm__ volatile("pause");
		}

		if(usb_hub_get_port_status(hubdev, port, &ps) != USB_XFER_OK)
			return -1;

		/* Reset complete when C_PORT_RESET is set */
		if(ps.wPortChange & USB_PORT_STAT_C_RESET) {
			usb_hub_clear_port_feature(hubdev, port,
			                           USB_HUB_FEAT_C_PORT_RESET);
			break;
		}
	}
	return 0;
}

int usb_hub_reset_port(usb_device_t *hubdev, uint8_t port)
{
	if(!hubdev)
		return -1;
	return usb_hub_do_port_reset(hubdev, port);
}

/*
 * Reset one downstream port and enumerate whatever is on it.  Split out of
 * usb_hub_enumerate_ports() so the hot-plug rescan can bring up a single
 * port without re-powering and re-walking the whole hub.
 *
 * Returns 1 if a device was enumerated, 0 otherwise.
 */
static int usb_hub_bringup_port(usb_hub_dev_t *hub, uint8_t port)
{
	struct usb_hub_port_status ps;

	/* Read port status */
	if(usb_hub_get_port_status(hub->udev, port, &ps) != USB_XFER_OK)
		return 0;

	/* Clear any pending connection change */
	if(ps.wPortChange & USB_PORT_STAT_C_CONNECTION)
		usb_hub_clear_port_feature(hub->udev, port,
		                           USB_HUB_FEAT_C_PORT_CONNECT);

	/* Skip if nothing connected */
	if(!(ps.wPortStatus & USB_PORT_STAT_CONNECTION))
		return 0;

	/* Reset the port.  A reset that times out is still worth following up on:
	 * the status re-read below decides whether the port actually came up. */
	(void)usb_hub_do_port_reset(hub->udev, port);

	/* Re-read status after reset */
	if(usb_hub_get_port_status(hub->udev, port, &ps) != USB_XFER_OK)
		return 0;

	/* Port must be enabled after reset */
	if(!(ps.wPortStatus & USB_PORT_STAT_ENABLE))
		return 0;

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

	/* Enumerate the downstream device through the core USB stack,
	 * recording this hub as its parent so the root-port hot-plug scan
	 * doesn't mistake it for a root device and disconnect it. [DRV-04] */
	/* Propagate the result.  This used to `return 1` unconditionally, which
	 * would have made the retry cap dead code: a port whose enumeration
	 * failed still looked like a success, so its failure counter never
	 * advanced and it was re-probed forever. */
	if(usb_enumerate_device_parent(hub->udev->hcd, port, speed,
	                               hub->udev) != 0)
		return 0;
	return 1;
}

static void usb_hub_enumerate_ports(usb_hub_dev_t *hub)
{
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

	for(uint8_t port = 1; port <= hub->nports; port++)
		(void)usb_hub_bringup_port(hub, port);
}

/*
 * ============================================================
 * Downstream hot-plug
 * ============================================================
 *
 * usb_hub_enumerate_ports() runs ONCE, when the hub itself is attached.  A
 * device plugged into a hub after that point was therefore invisible forever,
 * and one unplugged from a hub stayed in the device table as a stale entry
 * until the hub itself was removed.  Only the ROOT ports were reconciled, by
 * usb_hotplug_scan() in usb.c -- and everything behind a hub shares that hub's
 * root-port connection bit, so that scan structurally cannot see it.
 *
 * Called from usb_hotplug_scan(), i.e. on the same ~4 Hz kthread.  Being on a
 * thread matters: each port check is a CONTROL TRANSFER (unlike a root port,
 * which is a register read), so this must be able to sleep and must never run
 * from interrupt context.
 *
 * Teardown deliberately reuses usb_disconnect_device(), which already does the
 * ordering that was expensive to get right: recurse into anything behind a
 * nested hub, run the class driver's .detach (force-unmount, DMA quiesce),
 * unregister the devtree node, unpublish the /dev/usb nodes, and only then
 * free the struct and its USB address. [DRV-01][DRV-02][DRV-20][A33]
 */
void usb_hub_scan_ports(void)
{
	struct usb_hub_port_status ps;

	for(int i = 0; i < USB_HUB_MAX_DEVICES; i++) {
		usb_hub_dev_t *hub = &hub_devices[i];

		if(!hub->active || !hub->udev)
			continue;

		for(uint8_t port = 1; port <= hub->nports; port++) {
			usb_device_t *child =
				usb_child_device_on_port(hub->udev, port);

			if(usb_hub_get_port_status(hub->udev, port, &ps) != USB_XFER_OK)
				continue;

			int connected =
				(ps.wPortStatus & USB_PORT_STAT_CONNECTION) != 0;

			/* Acknowledge the change bit either way, or the hub keeps
			 * reporting it and (once the interrupt endpoint is used)
			 * would re-notify forever. */
			if(ps.wPortChange & USB_PORT_STAT_C_CONNECTION)
				usb_hub_clear_port_feature(hub->udev, port,
				                           USB_HUB_FEAT_C_PORT_CONNECT);

			if(!connected)
				hub->enum_fail[port] = 0;   /* re-plug gets a fresh try */

			if(child && !connected) {
				kprintf("usb_hub: device removed from port %u\n", port);
				usb_disconnect_device(child);
			} else if(!child && connected) {
				/* Parked after repeated failures.  Only a disconnect
				 * clears it; otherwise this is an infinite reset +
				 * re-enumerate loop at the scan rate. */
				if(hub->enum_fail[port] >= USB_ENUM_MAX_TRIES)
					continue;

				kprintf("usb_hub: device attached on port %u\n", port);
				if(!usb_hub_bringup_port(hub, port)) {
					if(++hub->enum_fail[port] >= USB_ENUM_MAX_TRIES)
						kprintf("usb_hub: port %u: enumeration failed "
						        "%u times, giving up until re-plug\n",
						        port, USB_ENUM_MAX_TRIES);
				}
			}
		}
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
	if(hub->nports > USB_HUB_MAX_PORTS) {
		/* Say so.  This used to truncate in silence, so a hub with more
		 * ports than the clamp had dead sockets and nothing explained
		 * why. */
		kprintf("usb_hub: %u ports reported, only %u supported\n",
		        (unsigned)hub->nports, (unsigned)USB_HUB_MAX_PORTS);
		hub->nports = USB_HUB_MAX_PORTS;
	}
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
