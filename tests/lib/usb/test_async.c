/*
 * test_async.c - Unit tests for libusb asynchronous I/O:
 *                transfer alloc/free, ISO packets, stream IDs,
 *                submit/reap, cancel, pollfds, and event locking.
 */
#include "test_common.h"

static void test_alloc_free(void)
{
	struct libusb_transfer *transfer;

	/* Basic alloc/free */
	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);
	assert(transfer->num_iso_packets == 0);
	libusb_free_transfer(transfer);

	/* Alloc with ISO packets */
	transfer = libusb_alloc_transfer(4);
	assert(transfer != NULL);
	assert(transfer->num_iso_packets == 4);
	libusb_free_transfer(transfer);

	/* Negative ISO packets returns NULL */
	assert(libusb_alloc_transfer(-1) == NULL);
}

static void test_stream_id(void)
{
	struct libusb_transfer *transfer;

	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);
	libusb_transfer_set_stream_id(transfer, 42);
	assert(libusb_transfer_get_stream_id(transfer) == 42);
	libusb_free_transfer(transfer);
}

static void test_submit_reap(void)
{
	struct test_env env = {0};
	struct libusb_transfer *transfer;

	test_env_setup(&env);

	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);

	{
		unsigned char buffer[64];
		libusb_fill_bulk_transfer(transfer, env.handle, LIBUSB_ENDPOINT_IN | 1,
			buffer, sizeof(buffer), NULL, NULL, 5000);

		g_submitted_urb = NULL;
		g_submitted_urb_count = 0;

		assert(libusb_submit_transfer(transfer) == LIBUSB_SUCCESS);
		assert(g_submitted_urb_count == 1);
		assert(g_last_request == USBDEVFS_SUBMITURB);

		/* Reap via handle_events */
		{
			struct timeval tv = {0, 0};
			assert(libusb_handle_events_timeout(env.ctx, &tv) == LIBUSB_SUCCESS);
		}
	}

	libusb_free_transfer(transfer);
	test_env_teardown(&env);
}

static void test_cancel(void)
{
	struct test_env env = {0};
	struct libusb_transfer *transfer;

	test_env_setup(&env);

	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);

	{
		unsigned char buffer[32];
		libusb_fill_bulk_transfer(transfer, env.handle, LIBUSB_ENDPOINT_OUT | 2,
			buffer, sizeof(buffer), NULL, NULL, 0);
		assert(libusb_submit_transfer(transfer) == LIBUSB_SUCCESS);
		assert(libusb_cancel_transfer(transfer) == LIBUSB_SUCCESS);
	}

	/* Reap the cancelled transfer */
	{
		struct timeval tv = {0, 0};
		(void)libusb_handle_events_timeout(env.ctx, &tv);
	}

	libusb_free_transfer(transfer);
	test_env_teardown(&env);
}

static void test_pollfds(void)
{
	struct test_env env = {0};
	const struct libusb_pollfd **pollfds;
	int pollfd_count;

	test_env_setup(&env);

	pollfds = libusb_get_pollfds(env.ctx);
	assert(pollfds != NULL);
	pollfd_count = 0;
	while (pollfds[pollfd_count] != NULL) {
		pollfd_count++;
	}
	/* event pipe + one open handle */
	assert(pollfd_count == 2);
	libusb_free_pollfds(pollfds);

	assert(libusb_pollfds_handle_timeouts(env.ctx) == 1);

	test_env_teardown(&env);
}

static void test_event_locking(void)
{
	libusb_context *ctx = NULL;

	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);

	/* Initially not active */
	assert(libusb_event_handler_active(ctx) == 0);

	/* Try lock should succeed */
	assert(libusb_try_lock_events(ctx) == 0);
	assert(libusb_event_handler_active(ctx) == 1);
	assert(libusb_event_handling_ok(ctx) == 1);

	/* Second try lock should fail */
	assert(libusb_try_lock_events(ctx) == 1);

	/* Unlock */
	libusb_unlock_events(ctx);
	assert(libusb_event_handler_active(ctx) == 0);
	assert(libusb_event_handling_ok(ctx) == 0);

	/* Test interrupt */
	libusb_interrupt_event_handler(ctx);

	/* Test get_next_timeout with no flying transfers */
	{
		struct timeval tv;
		assert(libusb_get_next_timeout(ctx, &tv) == 0);
	}

	libusb_exit(ctx);
}

int main(void)
{
	printf("test_async: alloc/free...\n");
	test_alloc_free();
	printf("test_async: stream ID...\n");
	test_stream_id();
	printf("test_async: submit/reap...\n");
	test_submit_reap();
	printf("test_async: cancel...\n");
	test_cancel();
	printf("test_async: pollfds...\n");
	test_pollfds();
	printf("test_async: event locking...\n");
	test_event_locking();
	printf("test_async: PASSED\n");
	return 0;
}
