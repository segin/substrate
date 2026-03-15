#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/time.h>

/*
 * Convert a timeval to milliseconds for poll().
 */
static int
libusb__tv_to_ms(struct timeval *tv)
{
	if (tv == NULL) {
		return -1;
	}
	if (tv->tv_sec < 0 || (tv->tv_sec == 0 && tv->tv_usec <= 0)) {
		return 0;
	}
	return (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000);
}

/*
 * Inner event handling loop (no lock management).
 */
static int
libusb__handle_events_inner(libusb_context *ctx, int timeout_ms, int *completed)
{
	int ret;

	if (completed != NULL && *completed) {
		return LIBUSB_SUCCESS;
	}

	ret = libusb__reap_urbs(ctx, timeout_ms);
	if (ret < 0) {
		return ret;
	}
	return LIBUSB_SUCCESS;
}

/*
 * Event lock/unlock — simple cooperative model.
 * Real multi-threaded event handling would need proper mutexes;
 * for now this is flag-based.
 */

int LIBUSB_CALL
libusb_try_lock_events(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return 1;
	}
	if (resolved->event_handler_active) {
		return 1;
	}
	resolved->event_handler_active = 1;
	resolved->event_handling_ok_flag = 1;
	return 0;
}

void LIBUSB_CALL
libusb_lock_events(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return;
	}
	resolved->event_handler_active = 1;
	resolved->event_handling_ok_flag = 1;
}

void LIBUSB_CALL
libusb_unlock_events(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return;
	}
	resolved->event_handler_active = 0;
	resolved->event_handling_ok_flag = 0;
}

int LIBUSB_CALL
libusb_event_handling_ok(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return 0;
	}
	return resolved->event_handling_ok_flag;
}

int LIBUSB_CALL
libusb_event_handler_active(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return 0;
	}
	return resolved->event_handler_active;
}

void LIBUSB_CALL
libusb_interrupt_event_handler(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	unsigned char byte = 1;

	if (resolved == NULL) {
		return;
	}
	if (resolved->event_pipe[1] >= 0) {
		(void)write(resolved->event_pipe[1], &byte, 1);
	}
}

void LIBUSB_CALL
libusb_lock_event_waiters(libusb_context *ctx)
{
	(void)ctx;
	/* No-op in single-threaded implementation */
}

void LIBUSB_CALL
libusb_unlock_event_waiters(libusb_context *ctx)
{
	(void)ctx;
	/* No-op in single-threaded implementation */
}

int LIBUSB_CALL
libusb_wait_for_event(libusb_context *ctx, struct timeval *tv)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	int timeout_ms;

	if (resolved == NULL) {
		return 1;
	}
	timeout_ms = libusb__tv_to_ms(tv);
	/* Wait on the event pipe */
	{
		struct pollfd pfd;
		int ret;

		pfd.fd = resolved->event_pipe[0];
		pfd.events = POLLIN;
		pfd.revents = 0;
		ret = poll(&pfd, 1, timeout_ms);
		if (ret == 0) {
			return 1; /* timed out */
		}
		if (ret > 0 && (pfd.revents & POLLIN)) {
			char buf[8];
			(void)read(resolved->event_pipe[0], buf, sizeof(buf));
		}
	}
	return 0;
}

/*
 * Public event handling API.
 */

int LIBUSB_CALL
libusb_handle_events_timeout_completed(libusb_context *ctx, struct timeval *tv,
	int *completed)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	int timeout_ms;
	int ret;

	if (resolved == NULL) {
		return LIBUSB_ERROR_OTHER;
	}

	timeout_ms = libusb__tv_to_ms(tv);

	if (libusb_try_lock_events(resolved) == 0) {
		ret = libusb__handle_events_inner(resolved, timeout_ms, completed);
		libusb_unlock_events(resolved);
	} else {
		ret = LIBUSB_SUCCESS;
	}
	return ret;
}

int LIBUSB_CALL
libusb_handle_events_timeout(libusb_context *ctx, struct timeval *tv)
{
	return libusb_handle_events_timeout_completed(ctx, tv, NULL);
}

int LIBUSB_CALL
libusb_handle_events(libusb_context *ctx)
{
	struct timeval tv;

	tv.tv_sec = 60;
	tv.tv_usec = 0;
	return libusb_handle_events_timeout_completed(ctx, &tv, NULL);
}

int LIBUSB_CALL
libusb_handle_events_completed(libusb_context *ctx, int *completed)
{
	struct timeval tv;

	tv.tv_sec = 60;
	tv.tv_usec = 0;
	return libusb_handle_events_timeout_completed(ctx, &tv, completed);
}

int LIBUSB_CALL
libusb_handle_events_locked(libusb_context *ctx, struct timeval *tv)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	int timeout_ms;

	if (resolved == NULL) {
		return LIBUSB_ERROR_OTHER;
	}
	timeout_ms = libusb__tv_to_ms(tv);
	return libusb__handle_events_inner(resolved, timeout_ms, NULL);
}

int LIBUSB_CALL
libusb_pollfds_handle_timeouts(libusb_context *ctx)
{
	(void)ctx;
	return 1;
}

int LIBUSB_CALL
libusb_get_next_timeout(libusb_context *ctx, struct timeval *tv)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	struct timeval now;
	struct timeval earliest;
	int found = 0;
	int index;

	if (resolved == NULL || tv == NULL) {
		return 0;
	}

	gettimeofday(&now, NULL);

	for (index = 0; index < resolved->flying_transfer_count; index++) {
		struct libusb_transfer *transfer =
			resolved->flying_transfers[index];
		struct libusb__transfer_priv *priv =
			libusb__get_transfer_priv(transfer);

		if (!priv->has_deadline) {
			continue;
		}
		if (!found ||
		    priv->deadline.tv_sec < earliest.tv_sec ||
		    (priv->deadline.tv_sec == earliest.tv_sec &&
		     priv->deadline.tv_usec < earliest.tv_usec)) {
			earliest = priv->deadline;
			found = 1;
		}
	}

	if (!found) {
		return 0;
	}

	tv->tv_sec = earliest.tv_sec - now.tv_sec;
	tv->tv_usec = earliest.tv_usec - now.tv_usec;
	if (tv->tv_usec < 0) {
		tv->tv_sec--;
		tv->tv_usec += 1000000;
	}
	if (tv->tv_sec < 0) {
		tv->tv_sec = 0;
		tv->tv_usec = 0;
	}
	return 1;
}

const struct libusb_pollfd **LIBUSB_CALL
libusb_get_pollfds(libusb_context *ctx)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	const struct libusb_pollfd **list;
	int count;
	int index;
	int pos;

	if (resolved == NULL) {
		return NULL;
	}

	/* One entry per open handle + event pipe + NULL terminator */
	count = resolved->open_handle_count + 1;
	list = calloc((size_t)count + 1, sizeof(*list));
	if (list == NULL) {
		return NULL;
	}

	pos = 0;

	/* Event pipe read end */
	{
		struct libusb_pollfd *pfd = calloc(1, sizeof(*pfd));
		if (pfd != NULL) {
			pfd->fd = resolved->event_pipe[0];
			pfd->events = POLLIN;
			list[pos++] = pfd;
		}
	}

	/* Device fds */
	for (index = 0; index < resolved->open_handle_count; index++) {
		struct libusb_pollfd *pfd = calloc(1, sizeof(*pfd));
		if (pfd != NULL) {
			pfd->fd = resolved->open_handles[index]->fd;
			pfd->events = POLLIN | POLLOUT;
			list[pos++] = pfd;
		}
	}

	list[pos] = NULL;
	return list;
}

void LIBUSB_CALL
libusb_free_pollfds(const struct libusb_pollfd **pollfds)
{
	int index;

	if (pollfds == NULL) {
		return;
	}
	for (index = 0; pollfds[index] != NULL; index++) {
		free((void *)pollfds[index]);
	}
	free(pollfds);
}

void LIBUSB_CALL
libusb_set_pollfd_notifiers(libusb_context *ctx,
	libusb_pollfd_added_cb added_cb, libusb_pollfd_removed_cb removed_cb,
	void *user_data)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return;
	}
	resolved->pollfd_added_cb = added_cb;
	resolved->pollfd_removed_cb = removed_cb;
	resolved->pollfd_cb_user_data = user_data;
}
