#ifndef LIBUSB_INTERNAL_H
#define LIBUSB_INTERNAL_H

#include <libusb.h>

#include <stddef.h>
#include <string.h>

#define LIBUSB__MAX_CLAIMED_INTERFACES 32
#define LIBUSB__MAX_OPEN_HANDLES       64
#define LIBUSB__MAX_FLYING_TRANSFERS   256
#define LIBUSB__MAX_HOTPLUG_CALLBACKS  32

struct libusb__hotplug_callback {
	int in_use;
	libusb_hotplug_callback_handle handle;
	int events;
	int flags;
	int vendor_id;
	int product_id;
	int dev_class;
	libusb_hotplug_callback_fn cb_fn;
	void *user_data;
};

struct libusb__transfer_priv {
	struct usbdevfs_urb *urb;
	int submitted;
	uint32_t stream_id;
	struct timeval deadline;
	int has_deadline;
};

struct libusb_context {
	int refcount;
	int debug_level;
	int no_device_discovery;
	int event_pipe[2];
	libusb_log_cb log_cb;
	char locale[16];
	libusb_device **cached_devices;
	size_t cached_device_count;

	/* Open handle tracking for pollfd enumeration */
	libusb_device_handle *open_handles[LIBUSB__MAX_OPEN_HANDLES];
	int open_handle_count;

	/* Flying transfer list */
	struct libusb_transfer *flying_transfers[LIBUSB__MAX_FLYING_TRANSFERS];
	int flying_transfer_count;

	/* Event handling state */
	int event_handler_active;
	int event_handling_ok_flag;

	/* Pollfd notifier callbacks */
	libusb_pollfd_added_cb pollfd_added_cb;
	libusb_pollfd_removed_cb pollfd_removed_cb;
	void *pollfd_cb_user_data;

	/* Hotplug tracking */
	struct libusb__hotplug_callback hotplug_callbacks[LIBUSB__MAX_HOTPLUG_CALLBACKS];
	int next_hotplug_handle;
	libusb_device **hotplug_devices;
	size_t hotplug_device_count;
	char *hotplug_event_snapshot;
	size_t hotplug_event_snapshot_len;
};

struct libusb_device {
	libusb_context *ctx;
	int refcount;
	uint8_t bus_number;
	uint8_t device_address;
	uint8_t port_number;
	int speed;
	char path[128];
	int descriptor_valid;
	struct libusb_device_descriptor descriptor;
	libusb_device *parent;
	uint8_t parent_bus_number;
	uint8_t parent_device_address;
	int parent_valid;
	int active_configuration;
	uint16_t endpoint_max_packet[256];
	uint16_t endpoint_iso_max_packet[256];
};

struct libusb_device_handle {
	libusb_device *device;
	int fd;
	int owns_fd;
	int auto_detach;
	int active_configuration;
	int claimed_interfaces[LIBUSB__MAX_CLAIMED_INTERFACES];
	int claimed_interface_count;
};

const char *libusb__devfs_root(void);
libusb_context *libusb__resolve_context(libusb_context *ctx);
int libusb__map_errno(int err);
int libusb__context_rescan(libusb_context *ctx);
void libusb__free_cached_devices(libusb_context *ctx);
int libusb__poll_hotplug(libusb_context *ctx);

void libusb__register_handle(libusb_context *ctx, libusb_device_handle *handle);
void libusb__unregister_handle(libusb_context *ctx, libusb_device_handle *handle);

struct libusb__transfer_priv *libusb__get_transfer_priv(struct libusb_transfer *transfer);
void libusb__add_flying_transfer(libusb_context *ctx, struct libusb_transfer *transfer);
void libusb__remove_flying_transfer(libusb_context *ctx, struct libusb_transfer *transfer);
int libusb__reap_urbs(libusb_context *ctx, int timeout_ms);

static inline size_t
libusb__strlcpy(char *dst, const char *src, size_t size)
{
	size_t src_len;
	size_t copy_len;

	src_len = strlen(src);
	if (size == 0) {
		return src_len;
	}
	copy_len = src_len >= size ? size - 1 : src_len;
	memcpy(dst, src, copy_len);
	dst[copy_len] = '\0';
	return src_len;
}

#endif