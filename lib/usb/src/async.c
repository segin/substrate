#include "internal.h"
#include "../os/substrate.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>

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
	return substrate_submit_transfer(transfer);
}

int LIBUSB_CALL
libusb_cancel_transfer(struct libusb_transfer *transfer)
{
	return substrate_cancel_transfer(transfer);
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

int
libusb__reap_urbs(libusb_context *ctx, int timeout_ms)
{
	return substrate_handle_transfer_completion(ctx, timeout_ms);
}
