HOST_TESTS = host_test_libusb

USB_SRCS = ../../../lib/usb/src/core.c ../../../lib/usb/src/device.c ../../../lib/usb/src/sync.c ../../../lib/usb/src/descriptor.c ../../../lib/usb/src/async.c ../../../lib/usb/src/poll.c
USB_INCLUDES = -DNATIVE_BUILD=1 -I../../../lib/usb/include -idirafter ../../../sys/include -I../../../sys

.PHONY: all clean

all: $(HOST_TESTS)

host_test_libusb: host_test_libusb.c $(USB_SRCS)
	$(CC) -g -Wall -Wextra -Werror $(USB_INCLUDES) $(USB_SRCS) host_test_libusb.c -o $@

clean:
	rm -f $(HOST_TESTS)