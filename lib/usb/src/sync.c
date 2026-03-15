#include "internal.h"

#include <errno.h>
#include <sys/poll.h>
#include <sys/ioctl.h>

static int
libusb__wait_ready(int fd, unsigned int timeout)
{
	struct pollfd pfd;
	int poll_timeout;
	int ret;

	if (timeout == 0) {
		return LIBUSB_SUCCESS;
	}
	pfd.fd = fd;
	pfd.events = POLLIN | POLLOUT;
	pfd.revents = 0;
	poll_timeout = timeout > (unsigned int)INT_MAX ? INT_MAX : (int)timeout;
	ret = poll(&pfd, 1, poll_timeout);
	if (ret == 0) {
		return LIBUSB_ERROR_TIMEOUT;
	}
	if (ret < 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_control_transfer(libusb_device_handle *dev_handle, uint8_t bmRequestType,
	uint8_t bRequest, uint16_t wValue, uint16_t wIndex, unsigned char *data,
	uint16_t wLength, unsigned int timeout)
{
	struct usbdevfs_ctrltransfer transfer;
	int ret;

	if (dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = libusb__wait_ready(dev_handle->fd, timeout);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	transfer.bRequestType = bmRequestType;
	transfer.bRequest = bRequest;
	transfer.wValue = wValue;
	transfer.wIndex = wIndex;
	transfer.wLength = wLength;
	transfer.timeout = timeout;
	transfer.data = data;
	ret = ioctl(dev_handle->fd, USBDEVFS_CONTROL, &transfer);
	if (ret < 0) {
		return libusb__map_errno(errno);
	}
	return ret > 0 ? ret : (int)wLength;
}

int LIBUSB_CALL
libusb_bulk_transfer(libusb_device_handle *dev_handle, unsigned char endpoint,
	unsigned char *data, int length, int *transferred, unsigned int timeout)
{
	struct usbdevfs_bulktransfer transfer;
	int ret;

	if (dev_handle == NULL || data == NULL || length < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = libusb__wait_ready(dev_handle->fd, timeout);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	transfer.ep = endpoint;
	transfer.len = (unsigned int)length;
	transfer.timeout = timeout;
	transfer.data = data;
	ret = ioctl(dev_handle->fd, USBDEVFS_BULK, &transfer);
	if (ret < 0) {
		return libusb__map_errno(errno);
	}
	if (transferred != NULL) {
		*transferred = ret > 0 ? ret : length;
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_interrupt_transfer(libusb_device_handle *dev_handle,
	unsigned char endpoint, unsigned char *data, int length,
	int *transferred, unsigned int timeout)
{
	return libusb_bulk_transfer(dev_handle, endpoint, data, length,
		transferred, timeout);
}