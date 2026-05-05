#ifndef LIBUSB_OS_SUBSTRATE_H
#define LIBUSB_OS_SUBSTRATE_H

#include <libusb.h>
#include <sys/usbdevfs.h>

#include <stddef.h>
#include <stdint.h>

/*
 * Substrate OS backend for libusb.
 *
 * This module is the single point through which the rest of libusb reaches
 * the Substrate kernel.  It owns:
 *   - device-tree enumeration under /dev/usb/
 *   - device-node open/close
 *   - synchronous descriptor fetches
 *   - URB submit/cancel/reap
 *   - configuration / interface / halt / reset / kernel-driver controls
 *
 * Every function returns an LIBUSB_* error code unless documented otherwise.
 */

struct libusb_context;
struct libusb_device;
struct libusb_transfer;

/* Device enumeration ------------------------------------------------------- */

/*
 * Re-scan /dev/usb and refresh ctx->cached_devices.  Equivalent to the
 * existing libusb__context_rescan() entry point; this is the canonical
 * substrate entry.
 */
int substrate_get_device_list(struct libusb_context *ctx);

/* Device-node open / close ------------------------------------------------- */

int substrate_open(struct libusb_device *dev, int *out_fd);
void substrate_close(int fd);

/* Configuration / interface controls --------------------------------------- */

int substrate_set_configuration(int fd, int configuration);
int substrate_claim_interface(int fd, int interface_number);
int substrate_release_interface(int fd, int interface_number);
int substrate_set_interface_alt_setting(int fd, int interface_number,
	int alternate_setting);
int substrate_clear_halt(int fd, unsigned char endpoint);
int substrate_reset_device(int fd);

/* Kernel-driver control ---------------------------------------------------- */

/*
 * out_active receives 1 if a kernel driver is bound to the interface, 0
 * otherwise.  Returns LIBUSB_SUCCESS on success.
 */
int substrate_kernel_driver_active(int fd, int interface_number,
	int *out_active);
int substrate_detach_kernel_driver(int fd, int interface_number);
int substrate_attach_kernel_driver(int fd, int interface_number);

/* Descriptor fetch (via control transfer ioctl) ---------------------------- */

int substrate_get_device_descriptor(int fd,
	struct libusb_device_descriptor *desc);
/*
 * Fetch raw config descriptor bytes.  If buffer is NULL or buffer_length is 0,
 * only the first 9 bytes are read so the caller can size the full descriptor.
 * Returns the number of bytes actually transferred (>=0) or an LIBUSB_ERROR_*.
 */
int substrate_get_config_descriptor(int fd, uint8_t config_index,
	unsigned char *buffer, size_t buffer_length);

/* Asynchronous transfers --------------------------------------------------- */

/*
 * Build a usbdevfs_urb from transfer, submit it via USBDEVFS_SUBMITURB, and
 * record it on the context's flying-transfer list.  The transfer must have a
 * valid dev_handle.
 */
int substrate_submit_transfer(struct libusb_transfer *transfer);

/*
 * Discard a previously-submitted URB belonging to transfer.  Safe to call on
 * a transfer that has already completed (returns LIBUSB_SUCCESS) or one that
 * was never submitted (returns LIBUSB_ERROR_NOT_FOUND).
 */
int substrate_cancel_transfer(struct libusb_transfer *transfer);

/*
 * Reap completed URBs from every open handle, dispatch their callbacks, and
 * cancel any flying transfer whose deadline has passed.  Returns the number
 * of URBs reaped, or a negative LIBUSB_ERROR_*.
 */
int substrate_handle_transfer_completion(struct libusb_context *ctx,
	int timeout_ms);

#endif
