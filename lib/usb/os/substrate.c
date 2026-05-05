#include "substrate.h"
#include "../src/internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <unistd.h>

#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#ifndef ECANCELED
#define ECANCELED 125
#endif

/* Device enumeration ------------------------------------------------------- */

int
substrate_get_device_list(libusb_context *ctx)
{
	if (ctx == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	return libusb__context_rescan(ctx);
}

/* Device-node open / close ------------------------------------------------- */

int
substrate_open(libusb_device *dev, int *out_fd)
{
	int fd;

	if (dev == NULL || out_fd == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	fd = open(dev->path, O_RDWR);
	if (fd < 0) {
		return libusb__map_errno(errno);
	}
	*out_fd = fd;
	return LIBUSB_SUCCESS;
}

void
substrate_close(int fd)
{
	if (fd >= 0) {
		(void)close(fd);
	}
}

/* Configuration / interface controls --------------------------------------- */

int
substrate_set_configuration(int fd, int configuration)
{
	unsigned int request;

	if (fd < 0 || configuration < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = (unsigned int)configuration;
	if (ioctl(fd, USBDEVFS_SETCONFIGURATION, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_claim_interface(int fd, int interface_number)
{
	unsigned int request;

	if (fd < 0 || interface_number < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = (unsigned int)interface_number;
	if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_release_interface(int fd, int interface_number)
{
	unsigned int request;

	if (fd < 0 || interface_number < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = (unsigned int)interface_number;
	if (ioctl(fd, USBDEVFS_RELEASEINTERFACE, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_set_interface_alt_setting(int fd, int interface_number,
	int alternate_setting)
{
	struct usbdevfs_setinterface request;

	if (fd < 0 || interface_number < 0 || alternate_setting < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request.interface = (unsigned int)interface_number;
	request.altsetting = (unsigned int)alternate_setting;
	if (ioctl(fd, USBDEVFS_SETINTERFACE, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_clear_halt(int fd, unsigned char endpoint)
{
	unsigned int request;

	if (fd < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = endpoint;
	if (ioctl(fd, USBDEVFS_CLEAR_HALT, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_reset_device(int fd)
{
	if (fd < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (ioctl(fd, USBDEVFS_RESET, NULL) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

/* Kernel-driver control ---------------------------------------------------- */

int
substrate_kernel_driver_active(int fd, int interface_number, int *out_active)
{
	struct usbdevfs_getdriver request;

	if (fd < 0 || interface_number < 0 || out_active == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	memset(&request, 0, sizeof(request));
	request.interface = (unsigned int)interface_number;
	if (ioctl(fd, USBDEVFS_GET_DRIVER, &request) != 0) {
		return libusb__map_errno(errno);
	}
	*out_active = request.driver[0] != '\0';
	return LIBUSB_SUCCESS;
}

int
substrate_detach_kernel_driver(int fd, int interface_number)
{
	(void)interface_number;
	if (fd < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (ioctl(fd, USBDEVFS_DISCONNECT, NULL) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_attach_kernel_driver(int fd, int interface_number)
{
	(void)interface_number;
	if (fd < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (ioctl(fd, USBDEVFS_CONNECT, NULL) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

/* Descriptor fetch (via control transfer ioctl) ---------------------------- */

static int
substrate__control_get_descriptor(int fd, uint8_t desc_type, uint8_t desc_index,
	unsigned char *buffer, uint16_t length)
{
	struct usbdevfs_ctrltransfer transfer;
	int ret;

	transfer.bRequestType = LIBUSB_ENDPOINT_IN |
	    LIBUSB_REQUEST_TYPE_STANDARD | LIBUSB_RECIPIENT_DEVICE;
	transfer.bRequest = LIBUSB_REQUEST_GET_DESCRIPTOR;
	transfer.wValue = (uint16_t)((desc_type << 8) | desc_index);
	transfer.wIndex = 0;
	transfer.wLength = length;
	transfer.timeout = 1000;
	transfer.data = buffer;

	ret = ioctl(fd, USBDEVFS_CONTROL, &transfer);
	if (ret < 0) {
		return libusb__map_errno(errno);
	}
	return ret;
}

static uint16_t
substrate__read_le16(const unsigned char *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

int
substrate_get_device_descriptor(int fd, struct libusb_device_descriptor *desc)
{
	unsigned char raw[LIBUSB_DT_DEVICE_SIZE];
	int ret;

	if (fd < 0 || desc == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = substrate__control_get_descriptor(fd, LIBUSB_DT_DEVICE, 0, raw,
	    sizeof(raw));
	if (ret < 0) {
		return ret;
	}
	if ((size_t)ret < sizeof(raw)) {
		return LIBUSB_ERROR_IO;
	}
	desc->bLength = raw[0];
	desc->bDescriptorType = raw[1];
	desc->bcdUSB = substrate__read_le16(raw + 2);
	desc->bDeviceClass = raw[4];
	desc->bDeviceSubClass = raw[5];
	desc->bDeviceProtocol = raw[6];
	desc->bMaxPacketSize0 = raw[7];
	desc->idVendor = substrate__read_le16(raw + 8);
	desc->idProduct = substrate__read_le16(raw + 10);
	desc->bcdDevice = substrate__read_le16(raw + 12);
	desc->iManufacturer = raw[14];
	desc->iProduct = raw[15];
	desc->iSerialNumber = raw[16];
	desc->bNumConfigurations = raw[17];
	return LIBUSB_SUCCESS;
}

int
substrate_get_config_descriptor(int fd, uint8_t config_index,
	unsigned char *buffer, size_t buffer_length)
{
	unsigned char header[LIBUSB_DT_CONFIG_SIZE];
	uint16_t total_length;
	uint16_t request_length;
	int ret;

	if (fd < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	ret = substrate__control_get_descriptor(fd, LIBUSB_DT_CONFIG,
	    config_index, header, sizeof(header));
	if (ret < 0) {
		return ret;
	}
	if ((size_t)ret < sizeof(header)) {
		return LIBUSB_ERROR_IO;
	}

	total_length = substrate__read_le16(header + 2);

	if (buffer == NULL || buffer_length == 0) {
		return (int)total_length;
	}

	request_length = (uint16_t)(buffer_length < total_length ?
	    buffer_length : total_length);
	ret = substrate__control_get_descriptor(fd, LIBUSB_DT_CONFIG,
	    config_index, buffer, request_length);
	if (ret < 0) {
		return ret;
	}
	return ret;
}

/* Asynchronous transfers --------------------------------------------------- */

static uint8_t
substrate__transfer_type_to_urb_type(unsigned char type)
{
	switch (type) {
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_CONTROL:
		return USBDEVFS_URB_TYPE_CONTROL;
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS:
		return USBDEVFS_URB_TYPE_ISO;
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK:
		return USBDEVFS_URB_TYPE_BULK;
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT:
		return USBDEVFS_URB_TYPE_INTERRUPT;
	default:
		return USBDEVFS_URB_TYPE_BULK;
	}
}

static enum libusb_transfer_status
substrate__urb_status_to_transfer_status(int urb_status)
{
	switch (urb_status) {
	case 0:
		return LIBUSB_TRANSFER_COMPLETED;
	case -ETIMEDOUT:
		return LIBUSB_TRANSFER_TIMED_OUT;
	case -EPIPE:
		return LIBUSB_TRANSFER_STALL;
	case -ENODEV:
		return LIBUSB_TRANSFER_NO_DEVICE;
	case -EOVERFLOW:
		return LIBUSB_TRANSFER_OVERFLOW;
	case -ECONNRESET:
	case -ECANCELED:
		return LIBUSB_TRANSFER_CANCELLED;
	default:
		return LIBUSB_TRANSFER_ERROR;
	}
}

int
substrate_submit_transfer(struct libusb_transfer *transfer)
{
	struct libusb__transfer_priv *priv;
	struct usbdevfs_urb *urb;
	libusb_context *ctx;
	size_t urb_size;
	int iso_count;
	int ret;

	if (transfer == NULL || transfer->dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	priv = libusb__get_transfer_priv(transfer);
	if (priv->submitted) {
		return LIBUSB_ERROR_BUSY;
	}

	iso_count = transfer->num_iso_packets;
	urb_size = sizeof(*urb) +
	    (size_t)iso_count * sizeof(struct usbdevfs_iso_packet_desc);
	urb = calloc(1, urb_size);
	if (urb == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}

	urb->type = substrate__transfer_type_to_urb_type(transfer->type);
	urb->endpoint = transfer->endpoint;
	urb->buffer = transfer->buffer;
	urb->buffer_length = transfer->length;
	urb->usercontext = transfer;

	if (transfer->type == LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS) {
		int pkt;
		urb->u.number_of_packets = iso_count;
		urb->flags |= USBDEVFS_URB_ISO_ASAP;
		for (pkt = 0; pkt < iso_count; pkt++) {
			urb->iso_frame_desc[pkt].length =
			    transfer->iso_packet_desc[pkt].length;
		}
	} else {
		urb->u.stream_id = priv->stream_id;
	}

	if (transfer->flags & LIBUSB_TRANSFER_ADD_ZERO_PACKET) {
		urb->flags |= USBDEVFS_URB_ZERO_PACKET;
	}

	free(priv->urb);
	priv->urb = urb;

	if (transfer->timeout > 0) {
		struct timeval now;
		gettimeofday(&now, NULL);
		priv->deadline.tv_sec = now.tv_sec +
		    (long)(transfer->timeout / 1000);
		priv->deadline.tv_usec = now.tv_usec +
		    (long)((transfer->timeout % 1000) * 1000);
		if (priv->deadline.tv_usec >= 1000000) {
			priv->deadline.tv_sec++;
			priv->deadline.tv_usec -= 1000000;
		}
		priv->has_deadline = 1;
	} else {
		priv->has_deadline = 0;
	}

	ret = ioctl(transfer->dev_handle->fd, USBDEVFS_SUBMITURB, urb);
	if (ret < 0) {
		free(priv->urb);
		priv->urb = NULL;
		return libusb__map_errno(errno);
	}

	priv->submitted = 1;
	ctx = transfer->dev_handle->device != NULL ?
	    transfer->dev_handle->device->ctx : NULL;
	libusb__add_flying_transfer(ctx, transfer);
	return LIBUSB_SUCCESS;
}

int
substrate_cancel_transfer(struct libusb_transfer *transfer)
{
	struct libusb__transfer_priv *priv;
	int ret;

	if (transfer == NULL || transfer->dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	priv = libusb__get_transfer_priv(transfer);
	if (!priv->submitted) {
		return LIBUSB_ERROR_NOT_FOUND;
	}

	ret = ioctl(transfer->dev_handle->fd, USBDEVFS_DISCARDURB, priv->urb);
	if (ret < 0 && errno != EINVAL) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int
substrate_handle_transfer_completion(libusb_context *ctx, int timeout_ms)
{
	struct pollfd fds[LIBUSB__MAX_OPEN_HANDLES + 1];
	int nfds = 0;
	int index;
	int ready;
	int reaped = 0;

	if (ctx == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	fds[nfds].fd = ctx->event_pipe[0];
	fds[nfds].events = POLLIN;
	fds[nfds].revents = 0;
	nfds++;

	for (index = 0; index < ctx->open_handle_count; index++) {
		if (ctx->open_handles[index]->fd >= 0) {
			fds[nfds].fd = ctx->open_handles[index]->fd;
			fds[nfds].events = POLLOUT;
			fds[nfds].revents = 0;
			nfds++;
		}
	}

	ready = poll(fds, (nfds_t)nfds, timeout_ms);
	if (ready < 0) {
		if (errno == EINTR) {
			return 0;
		}
		return libusb__map_errno(errno);
	}
	if (ready == 0) {
		return 0;
	}

	if (fds[0].revents & POLLIN) {
		char buf[8];
		(void)read(ctx->event_pipe[0], buf, sizeof(buf));
	}

	for (index = 0; index < ctx->open_handle_count; index++) {
		struct usbdevfs_urb *urb = NULL;
		int fd = ctx->open_handles[index]->fd;
		int ret;

		while ((ret = ioctl(fd, USBDEVFS_REAPURBNDELAY, &urb)) == 0 &&
		    urb != NULL) {
			struct libusb_transfer *transfer;
			struct libusb__transfer_priv *priv;
			libusb_context *tctx;

			transfer = (struct libusb_transfer *)urb->usercontext;
			if (transfer == NULL) {
				continue;
			}
			priv = libusb__get_transfer_priv(transfer);
			priv->submitted = 0;

			transfer->actual_length = urb->actual_length;
			transfer->status = substrate__urb_status_to_transfer_status(
			    urb->status);

			if (transfer->type ==
			    LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS) {
				int pkt;
				int pkt_count = transfer->num_iso_packets;
				for (pkt = 0; pkt < pkt_count; pkt++) {
					transfer->iso_packet_desc[pkt].actual_length =
					    urb->iso_frame_desc[pkt].actual_length;
					transfer->iso_packet_desc[pkt].status =
					    substrate__urb_status_to_transfer_status(
						(int)urb->iso_frame_desc[pkt].status);
				}
			}

			tctx = transfer->dev_handle != NULL &&
			    transfer->dev_handle->device != NULL ?
			    transfer->dev_handle->device->ctx : NULL;
			libusb__remove_flying_transfer(tctx, transfer);

			if (transfer->callback != NULL) {
				transfer->callback(transfer);
			}

			if (transfer->flags & LIBUSB_TRANSFER_FREE_TRANSFER) {
				libusb_free_transfer(transfer);
			}

			reaped++;
			urb = NULL;
		}
	}

	{
		struct timeval now;
		gettimeofday(&now, NULL);

		for (index = 0; index < ctx->flying_transfer_count; index++) {
			struct libusb_transfer *transfer =
			    ctx->flying_transfers[index];
			struct libusb__transfer_priv *priv =
			    libusb__get_transfer_priv(transfer);

			if (priv->has_deadline &&
			    (now.tv_sec > priv->deadline.tv_sec ||
			    (now.tv_sec == priv->deadline.tv_sec &&
			    now.tv_usec >= priv->deadline.tv_usec))) {
				substrate_cancel_transfer(transfer);
			}
		}
	}

	return reaped;
}
