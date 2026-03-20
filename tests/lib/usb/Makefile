HOST_TESTS = host_test_libusb test_core test_descriptor test_device test_io test_async test_hotplug

USB_SRCS = ../../../lib/usb/src/core.c ../../../lib/usb/src/device.c ../../../lib/usb/src/sync.c ../../../lib/usb/src/descriptor.c ../../../lib/usb/src/async.c ../../../lib/usb/src/poll.c
USB_INCLUDES = -DNATIVE_BUILD=1 -I../../../lib/usb/include -idirafter ../../../sys/include -I../../../sys
CFLAGS_TEST = -g -Wall -Wextra -Werror -I.

.PHONY: all clean test

all: $(HOST_TESTS)

host_test_libusb: host_test_libusb.c $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) host_test_libusb.c -o $@

test_core: test_core.c test_common.h $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) test_core.c -o $@

test_descriptor: test_descriptor.c test_common.h $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) test_descriptor.c -o $@

test_device: test_device.c test_common.h $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) test_device.c -o $@

test_io: test_io.c test_common.h $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) test_io.c -o $@

test_async: test_async.c test_common.h $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) test_async.c -o $@

test_hotplug: test_hotplug.c test_common.h $(USB_SRCS)
	$(CC) $(CFLAGS_TEST) $(USB_INCLUDES) $(USB_SRCS) test_hotplug.c -o $@

test: $(HOST_TESTS)
	@for t in $(HOST_TESTS); do echo "=== $$t ==="; ./$$t || exit 1; done
	@echo "All tests passed."

clean:
	rm -f $(HOST_TESTS)