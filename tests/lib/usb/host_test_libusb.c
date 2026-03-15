#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <libusb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/usbdevfs.h>
#include <unistd.h>

static unsigned long g_last_request;
static struct usbdevfs_urb *g_submitted_urb;
static int g_submitted_urb_count;
static unsigned char g_device_descriptor[18] = {
	18, LIBUSB_DT_DEVICE, 0x00, 0x02, 0, 0, 0, 64,
	0x34, 0x12, 0x78, 0x56, 0x00, 0x01, 1, 2, 3, 1,
};
static unsigned char g_config_descriptor[] = {
	9, LIBUSB_DT_CONFIG, 25, 0, 1, 1, 0, 0x80, 50,
	9, LIBUSB_DT_INTERFACE, 0, 0, 1, LIBUSB_CLASS_VENDOR_SPEC, 0, 0, 0,
	7, LIBUSB_DT_ENDPOINT, 0x81, LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK, 64, 0, 0,
};
static unsigned char g_string_descriptor[] = {
	10, LIBUSB_DT_STRING, 'T', 0, 'e', 0, 's', 0, 't', 0,
};

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	unsigned int index;
	(void)timeout;
	for (index = 0; index < nfds; index++) {
		fds[index].revents = fds[index].events;
	}
	return 1;
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	(void)fd;
	g_last_request = request;
	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	if (request == USBDEVFS_CONTROL) {
		struct usbdevfs_ctrltransfer *ctrl = arg;
		unsigned char *buf = ctrl->data;
		uint8_t type = (uint8_t)(ctrl->wValue >> 8);
		if (type == LIBUSB_DT_DEVICE) {
			memcpy(buf, g_device_descriptor, ctrl->wLength < sizeof(g_device_descriptor) ? ctrl->wLength : sizeof(g_device_descriptor));
			return sizeof(g_device_descriptor);
		}
		if (type == LIBUSB_DT_CONFIG) {
			memcpy(buf, g_config_descriptor, ctrl->wLength < sizeof(g_config_descriptor) ? ctrl->wLength : sizeof(g_config_descriptor));
			return ctrl->wLength < (int)sizeof(g_config_descriptor) ? ctrl->wLength : (int)sizeof(g_config_descriptor);
		}
		if (type == LIBUSB_DT_STRING) {
			memcpy(buf, g_string_descriptor, ctrl->wLength < sizeof(g_string_descriptor) ? ctrl->wLength : sizeof(g_string_descriptor));
			return sizeof(g_string_descriptor);
		}
		return -1;
	}
	if (request == USBDEVFS_BULK) {
		struct usbdevfs_bulktransfer *bulk = arg;
		if ((bulk->ep & LIBUSB_ENDPOINT_IN) != 0) {
			memset(bulk->data, 0x5a, bulk->len);
		}
		return (int)bulk->len;
	}
	if (request == USBDEVFS_SETCONFIGURATION || request == USBDEVFS_CLAIMINTERFACE ||
	    request == USBDEVFS_RELEASEINTERFACE || request == USBDEVFS_SETINTERFACE ||
	    request == USBDEVFS_CLEAR_HALT || request == USBDEVFS_RESET ||
	    request == USBDEVFS_DISCONNECT || request == USBDEVFS_CONNECT) {
		return 0;
	}
	if (request == USBDEVFS_GET_DRIVER) {
		struct usbdevfs_getdriver *driver = arg;
		strcpy(driver->driver, "stub");
		return 0;
	}
	if (request == USBDEVFS_SUBMITURB) {
		struct usbdevfs_urb *urb = arg;
		g_submitted_urb = urb;
		g_submitted_urb_count++;
		return 0;
	}
	if (request == USBDEVFS_REAPURBNDELAY) {
		void **urb_out = arg;
		if (g_submitted_urb != NULL) {
			g_submitted_urb->status = 0;
			g_submitted_urb->actual_length = g_submitted_urb->buffer_length;
			*urb_out = g_submitted_urb;
			g_submitted_urb = NULL;
			return 0;
		}
		errno = EAGAIN;
		return -1;
	}
	if (request == USBDEVFS_DISCARDURB) {
		if (g_submitted_urb != NULL) {
			g_submitted_urb->status = -ECANCELED;
			return 0;
		}
		errno = EINVAL;
		return -1;
	}
	return -1;
}

static void make_path(char *buf, size_t bufsz, const char *root, const char *tail)
{
	snprintf(buf, bufsz, "%s/%s", root, tail);
}

static void create_fake_tree(char *root, size_t rootsz)
{
	char path[256];
	int fd;

	snprintf(root, rootsz, "/tmp/libusb-host-test-%d", getpid());
	assert(mkdir(root, 0700) == 0);
	make_path(path, sizeof(path), root, "bus1");
	assert(mkdir(path, 0700) == 0);
	make_path(path, sizeof(path), root, "bus2");
	assert(mkdir(path, 0700) == 0);

	make_path(path, sizeof(path), root, "bus1/dev2");
	fd = open(path, O_CREAT | O_RDWR, 0600);
	assert(fd >= 0);
	close(fd);

	make_path(path, sizeof(path), root, "bus2/dev7");
	fd = open(path, O_CREAT | O_RDWR, 0600);
	assert(fd >= 0);
	close(fd);
}

static void remove_fake_tree(const char *root)
{
	char path[256];

	make_path(path, sizeof(path), root, "bus1/dev2");
	(void)unlink(path);
	make_path(path, sizeof(path), root, "bus2/dev7");
	(void)unlink(path);
	make_path(path, sizeof(path), root, "bus1");
	(void)rmdir(path);
	make_path(path, sizeof(path), root, "bus2");
	(void)rmdir(path);
	(void)rmdir(root);
}

static void test_core_and_enumeration(void)
{
	char root[128];
	libusb_context *ctx = NULL;
	libusb_device **list = NULL;
	libusb_device_handle *handle = NULL;
	ssize_t count;

	create_fake_tree(root, sizeof(root));
	assert(setenv("LIBUSB_DEVFS_ROOT", root, 1) == 0);

	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
	assert(ctx != NULL);
	assert(libusb_get_version()->major == 1);
	assert(libusb_has_capability(LIBUSB_CAP_HAS_CAPABILITY) == 1);
	assert(strcmp(libusb_error_name(LIBUSB_ERROR_BUSY), "LIBUSB_ERROR_BUSY") == 0);
	assert(strcmp(libusb_strerror(LIBUSB_ERROR_TIMEOUT), "Operation timed out") == 0);

	count = libusb_get_device_list(ctx, &list);
	assert(count == 2);
	assert(libusb_get_bus_number(list[0]) == 1);
	assert(libusb_get_device_address(list[0]) == 2);
	assert(libusb_get_bus_number(list[1]) == 2);
	assert(libusb_get_device_address(list[1]) == 7);

	assert(libusb_open(list[0], &handle) == LIBUSB_SUCCESS);
	assert(libusb_get_device(handle) == list[0]);
	assert(libusb_get_configuration(handle, &(int){0}) == LIBUSB_SUCCESS);
	assert(libusb_set_auto_detach_kernel_driver(handle, 1) == LIBUSB_SUCCESS);
	{
		struct libusb_device_descriptor desc;
		struct libusb_config_descriptor *config = NULL;
		unsigned char data[8];
		int transferred = 0;

		assert(libusb_get_device_descriptor(list[0], &desc) == LIBUSB_SUCCESS);
		assert(desc.idVendor == 0x1234);
		assert(desc.idProduct == 0x5678);
		assert(libusb_get_config_descriptor(list[0], 0, &config) == LIBUSB_SUCCESS);
		assert(config->bNumInterfaces == 1);
		assert(config->interface[0].num_altsetting == 1);
		libusb_free_config_descriptor(config);
		assert(libusb_get_string_descriptor_ascii(handle, 1, data, sizeof(data)) == 4);
		assert(strcmp((char *)data, "Test") == 0);
		assert(libusb_control_transfer(handle, LIBUSB_ENDPOINT_IN,
			LIBUSB_REQUEST_GET_DESCRIPTOR,
			(uint16_t)(LIBUSB_DT_DEVICE << 8), 0, data, sizeof(data), 1000) == (int)sizeof(g_device_descriptor));
		assert(g_last_request == USBDEVFS_CONTROL);
		assert(libusb_bulk_transfer(handle, LIBUSB_ENDPOINT_IN | 1, data, sizeof(data), &transferred, 1000) == LIBUSB_SUCCESS);
		assert(transferred == (int)sizeof(data));
		assert(libusb_interrupt_transfer(handle, LIBUSB_ENDPOINT_IN | 1, data, sizeof(data), &transferred, 1000) == LIBUSB_SUCCESS);
	}
	libusb_close(handle);

	libusb_free_device_list(list, 1);
	libusb_exit(ctx);
	assert(unsetenv("LIBUSB_DEVFS_ROOT") == 0);
	remove_fake_tree(root);
}

static void test_default_context(void)
{
	assert(libusb_init(NULL) == LIBUSB_SUCCESS);
	assert(libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_DEBUG) == LIBUSB_SUCCESS);
	libusb_exit(NULL);
}

static void test_async_transfer(void)
{
	char root[128];
	libusb_context *ctx = NULL;
	libusb_device **list = NULL;
	libusb_device_handle *handle = NULL;
	struct libusb_transfer *transfer;
	ssize_t count;
	static int callback_fired;

	create_fake_tree(root, sizeof(root));
	assert(setenv("LIBUSB_DEVFS_ROOT", root, 1) == 0);

	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
	count = libusb_get_device_list(ctx, &list);
	assert(count >= 1);
	assert(libusb_open(list[0], &handle) == LIBUSB_SUCCESS);

	/* Test alloc/free */
	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);
	assert(transfer->num_iso_packets == 0);
	libusb_free_transfer(transfer);

	/* Test alloc with iso packets */
	transfer = libusb_alloc_transfer(4);
	assert(transfer != NULL);
	assert(transfer->num_iso_packets == 4);
	libusb_free_transfer(transfer);

	/* Test alloc with negative iso packets returns NULL */
	assert(libusb_alloc_transfer(-1) == NULL);

	/* Test stream ID get/set */
	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);
	libusb_transfer_set_stream_id(transfer, 42);
	assert(libusb_transfer_get_stream_id(transfer) == 42);

	/* Test submit + reap via handle_events */
	{
		unsigned char buffer[64];
		callback_fired = 0;
		libusb_fill_bulk_transfer(transfer, handle, LIBUSB_ENDPOINT_IN | 1,
			buffer, sizeof(buffer),
			(libusb_transfer_cb_fn)(void (*)(void)){0},
			&callback_fired, 5000);

		/* Set a real callback */
		transfer->callback = NULL;
		g_submitted_urb = NULL;
		g_submitted_urb_count = 0;

		assert(libusb_submit_transfer(transfer) == LIBUSB_SUCCESS);
		assert(g_submitted_urb_count == 1);
		assert(g_last_request == USBDEVFS_SUBMITURB);

		/* Now reap via handle_events */
		{
			struct timeval tv = {0, 0};
			assert(libusb_handle_events_timeout(ctx, &tv) == LIBUSB_SUCCESS);
		}
	}
	libusb_free_transfer(transfer);

	/* Test cancel */
	transfer = libusb_alloc_transfer(0);
	assert(transfer != NULL);
	{
		unsigned char buffer[32];
		libusb_fill_bulk_transfer(transfer, handle, LIBUSB_ENDPOINT_OUT | 2,
			buffer, sizeof(buffer), NULL, NULL, 0);
		assert(libusb_submit_transfer(transfer) == LIBUSB_SUCCESS);
		assert(libusb_cancel_transfer(transfer) == LIBUSB_SUCCESS);
	}
	/* Reap the cancelled transfer */
	{
		struct timeval tv = {0, 0};
		(void)libusb_handle_events_timeout(ctx, &tv);
	}
	libusb_free_transfer(transfer);

	libusb_close(handle);
	libusb_free_device_list(list, 1);
	libusb_exit(ctx);
	assert(unsetenv("LIBUSB_DEVFS_ROOT") == 0);
	remove_fake_tree(root);
}

static void test_pollfds(void)
{
	char root[128];
	libusb_context *ctx = NULL;
	libusb_device **list = NULL;
	libusb_device_handle *handle = NULL;
	const struct libusb_pollfd **pollfds;
	ssize_t count;
	int pollfd_count;

	create_fake_tree(root, sizeof(root));
	assert(setenv("LIBUSB_DEVFS_ROOT", root, 1) == 0);

	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
	count = libusb_get_device_list(ctx, &list);
	assert(count >= 1);
	assert(libusb_open(list[0], &handle) == LIBUSB_SUCCESS);

	pollfds = libusb_get_pollfds(ctx);
	assert(pollfds != NULL);
	pollfd_count = 0;
	while (pollfds[pollfd_count] != NULL) {
		pollfd_count++;
	}
	/* event pipe + one open handle */
	assert(pollfd_count == 2);
	libusb_free_pollfds(pollfds);

	/* Test pollfds_handle_timeouts */
	assert(libusb_pollfds_handle_timeouts(ctx) == 1);

	libusb_close(handle);
	libusb_free_device_list(list, 1);
	libusb_exit(ctx);
	assert(unsetenv("LIBUSB_DEVFS_ROOT") == 0);
	remove_fake_tree(root);
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
	test_core_and_enumeration();
	test_default_context();
	test_async_transfer();
	test_pollfds();
	test_event_locking();
	return 0;
}