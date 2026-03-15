/*
 * test_common.h - Shared test infrastructure for libusb unit tests.
 *
 * Provides stubbed ioctl()/poll(), fake /dev/usb/ tree helpers,
 * and global descriptor data used across all test binaries.
 */
#ifndef TEST_COMMON_H
#define TEST_COMMON_H

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

/* --- Global stub state --- */

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

/* --- Stubbed poll() --- */

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
	unsigned int index;
	(void)timeout;
	for (index = 0; index < nfds; index++) {
		fds[index].revents = fds[index].events;
	}
	return 1;
}

/* --- Stubbed ioctl() --- */

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
			memcpy(buf, g_device_descriptor,
				ctrl->wLength < sizeof(g_device_descriptor) ? ctrl->wLength : sizeof(g_device_descriptor));
			return sizeof(g_device_descriptor);
		}
		if (type == LIBUSB_DT_CONFIG) {
			memcpy(buf, g_config_descriptor,
				ctrl->wLength < sizeof(g_config_descriptor) ? ctrl->wLength : sizeof(g_config_descriptor));
			return ctrl->wLength < (int)sizeof(g_config_descriptor) ? ctrl->wLength : (int)sizeof(g_config_descriptor);
		}
		if (type == LIBUSB_DT_STRING) {
			memcpy(buf, g_string_descriptor,
				ctrl->wLength < sizeof(g_string_descriptor) ? ctrl->wLength : sizeof(g_string_descriptor));
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

/* --- Fake devfs tree helpers --- */

__attribute__((unused))
static void make_path(char *buf, size_t bufsz, const char *root, const char *tail)
{
	snprintf(buf, bufsz, "%s/%s", root, tail);
}

__attribute__((unused))
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

__attribute__((unused))
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

/* --- Common setup/teardown for tests needing a context + device --- */

struct test_env {
	char root[128];
	libusb_context *ctx;
	libusb_device **list;
	libusb_device_handle *handle;
	ssize_t count;
};

__attribute__((unused))
static void test_env_setup(struct test_env *env)
{
	create_fake_tree(env->root, sizeof(env->root));
	assert(setenv("LIBUSB_DEVFS_ROOT", env->root, 1) == 0);
	assert(libusb_init(&env->ctx) == LIBUSB_SUCCESS);
	env->count = libusb_get_device_list(env->ctx, &env->list);
	assert(env->count >= 1);
	assert(libusb_open(env->list[0], &env->handle) == LIBUSB_SUCCESS);
}

__attribute__((unused))
static void test_env_teardown(struct test_env *env)
{
	libusb_close(env->handle);
	libusb_free_device_list(env->list, 1);
	libusb_exit(env->ctx);
	assert(unsetenv("LIBUSB_DEVFS_ROOT") == 0);
	remove_fake_tree(env->root);
}

#endif /* TEST_COMMON_H */
