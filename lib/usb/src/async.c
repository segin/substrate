#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>

#ifndef ECONNRESET
#define ECONNRESET 104
#endif
#ifndef ECANCELED
#define ECANCELED 125
#endif

/*
 * Handle tracking for pollfd enumeration.
 */

void
libusb__register_handle(libusb_context *ctx, libusb_device_handle *handle)
{
	if (ctx == NULL || handle == NULL) {
		return;
	}
	if (ctx->open_handle_count >= LIBUSB__MAX_OPEN_HANDLES) {
		return;
	}
	ctx->open_handles[ctx->open_handle_count++] = handle;

	if (ctx->pollfd_added_cb != NULL && handle->fd >= 0) {
		ctx->pollfd_added_cb(handle->fd, POLLIN, ctx->pollfd_cb_user_data);
	}
}

void
libusb__unregister_handle(libusb_context *ctx, libusb_device_handle *handle)
{
	int index;

	if (ctx == NULL || handle == NULL) {
		return;
	}
	for (index = 0; index < ctx->open_handle_count; index++) {
		if (ctx->open_handles[index] == handle) {
			ctx->open_handles[index] =
				ctx->open_handles[ctx->open_handle_count - 1];
			ctx->open_handle_count--;

			if (ctx->pollfd_removed_cb != NULL && handle->fd >= 0) {
				ctx->pollfd_removed_cb(handle->fd,
					ctx->pollfd_cb_user_data);
			}
			return;
		}
	}
}

/*
 * Private data is stored immediately after the transfer + iso packets.
 */

struct libusb__transfer_priv *
libusb__get_transfer_priv(struct libusb_transfer *transfer)
{
	unsigned char *base;

	base = (unsigned char *)transfer;
	base += sizeof(*transfer);
	base += (size_t)transfer->num_iso_packets *
		sizeof(struct libusb_iso_packet_descriptor);
	return (struct libusb__transfer_priv *)base;
}

/*
 * Flying transfer tracking.
 */

void
libusb__add_flying_transfer(libusb_context *ctx, struct libusb_transfer *transfer)
{
	if (ctx == NULL || transfer == NULL) {
		return;
	}
	if (ctx->flying_transfer_count >= LIBUSB__MAX_FLYING_TRANSFERS) {
		return;
	}
	ctx->flying_transfers[ctx->flying_transfer_count++] = transfer;
}

void
libusb__remove_flying_transfer(libusb_context *ctx, struct libusb_transfer *transfer)
{
	int index;

	if (ctx == NULL || transfer == NULL) {
		return;
	}
	for (index = 0; index < ctx->flying_transfer_count; index++) {
		if (ctx->flying_transfers[index] == transfer) {
			ctx->flying_transfers[index] =
				ctx->flying_transfers[ctx->flying_transfer_count - 1];
			ctx->flying_transfer_count--;
			return;
		}
	}
}

/*
 * Map libusb transfer type to usbdevfs URB type.
 */
static uint8_t
libusb__transfer_type_to_urb_type(unsigned char type)
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

/*
 * Map URB status to libusb transfer status.
 */
static enum libusb_transfer_status
libusb__urb_status_to_transfer_status(int urb_status)
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

/*
 * Public API.
 */

struct libusb_transfer *LIBUSB_CALL
libusb_alloc_transfer(int iso_packets)
{
	struct libusb_transfer *transfer;
	struct libusb__transfer_priv *priv;
	size_t alloc_size;

	if (iso_packets < 0) {
		return NULL;
	}
	alloc_size = sizeof(*transfer) +
		(size_t)iso_packets * sizeof(struct libusb_iso_packet_descriptor) +
		sizeof(struct libusb__transfer_priv);
	transfer = calloc(1, alloc_size);
	if (transfer == NULL) {
		return NULL;
	}
	transfer->num_iso_packets = iso_packets;
	priv = libusb__get_transfer_priv(transfer);
	priv->urb = NULL;
	priv->submitted = 0;
	priv->stream_id = 0;
	priv->has_deadline = 0;
	return transfer;
}

void LIBUSB_CALL
libusb_free_transfer(struct libusb_transfer *transfer)
{
	struct libusb__transfer_priv *priv;

	if (transfer == NULL) {
		return;
	}
	priv = libusb__get_transfer_priv(transfer);
	free(priv->urb);
	if (transfer->flags & LIBUSB_TRANSFER_FREE_BUFFER) {
		free(transfer->buffer);
	}
	free(transfer);
}

int LIBUSB_CALL
libusb_submit_transfer(struct libusb_transfer *transfer)
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

	urb->type = libusb__transfer_type_to_urb_type(transfer->type);
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

	/* Set up deadline if timeout is specified */
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

int LIBUSB_CALL
libusb_cancel_transfer(struct libusb_transfer *transfer)
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

void LIBUSB_CALL
libusb_transfer_set_stream_id(struct libusb_transfer *transfer, uint32_t stream_id)
{
	struct libusb__transfer_priv *priv;

	if (transfer == NULL) {
		return;
	}
	priv = libusb__get_transfer_priv(transfer);
	priv->stream_id = stream_id;
}

uint32_t LIBUSB_CALL
libusb_transfer_get_stream_id(struct libusb_transfer *transfer)
{
	struct libusb__transfer_priv *priv;

	if (transfer == NULL) {
		return 0;
	}
	priv = libusb__get_transfer_priv(transfer);
	return priv->stream_id;
}

/*
 * Reap completed URBs from all open handles.
 * Called from the event loop.  Returns the number of transfers completed,
 * or a negative libusb error.
 */
int
libusb__reap_urbs(libusb_context *ctx, int timeout_ms)
{
	struct pollfd fds[LIBUSB__MAX_OPEN_HANDLES + 1];
	int nfds = 0;
	int index;
	int ready;
	int reaped = 0;

	if (ctx == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	/* Add event pipe */
	fds[nfds].fd = ctx->event_pipe[0];
	fds[nfds].events = POLLIN;
	fds[nfds].revents = 0;
	nfds++;

	/* Add all open device fds */
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

	/* Drain event pipe if signaled */
	if (fds[0].revents & POLLIN) {
		char buf[8];
		(void)read(ctx->event_pipe[0], buf, sizeof(buf));
	}

	/* Reap completed URBs from device fds */
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
			transfer->status =
				libusb__urb_status_to_transfer_status(urb->status);

			/* Map ISO per-packet status */
			if (transfer->type ==
			    LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS) {
				int pkt;
				int pkt_count = transfer->num_iso_packets;
				for (pkt = 0; pkt < pkt_count; pkt++) {
					transfer->iso_packet_desc[pkt].actual_length =
						urb->iso_frame_desc[pkt].actual_length;
					transfer->iso_packet_desc[pkt].status =
						libusb__urb_status_to_transfer_status(
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

	/* Check for timed-out transfers */
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
				libusb_cancel_transfer(transfer);
			}
		}
	}

	return reaped;
}
