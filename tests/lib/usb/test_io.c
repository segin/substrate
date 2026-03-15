/*
 * test_io.c - Unit tests for libusb synchronous I/O:
 *             control, bulk, and interrupt transfers.
 */
#include "test_common.h"

static void test_control_transfer(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	{
		unsigned char data[18];
		int ret;

		ret = libusb_control_transfer(env.handle, LIBUSB_ENDPOINT_IN,
			LIBUSB_REQUEST_GET_DESCRIPTOR,
			(uint16_t)(LIBUSB_DT_DEVICE << 8), 0,
			data, sizeof(data), 1000);
		assert(ret == (int)sizeof(g_device_descriptor));
		assert(g_last_request == USBDEVFS_CONTROL);

		/* Verify descriptor data came through */
		assert(data[0] == 18); /* bLength */
		assert(data[1] == LIBUSB_DT_DEVICE);
	}

	test_env_teardown(&env);
}

static void test_bulk_transfer(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	{
		unsigned char data[8];
		int transferred = 0;

		assert(libusb_bulk_transfer(env.handle, LIBUSB_ENDPOINT_IN | 1,
			data, sizeof(data), &transferred, 1000) == LIBUSB_SUCCESS);
		assert(transferred == (int)sizeof(data));
		/* ioctl stub fills IN data with 0x5a */
		assert(data[0] == 0x5a);
		assert(data[7] == 0x5a);
	}

	test_env_teardown(&env);
}

static void test_interrupt_transfer(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	{
		unsigned char data[8];
		int transferred = 0;

		assert(libusb_interrupt_transfer(env.handle, LIBUSB_ENDPOINT_IN | 1,
			data, sizeof(data), &transferred, 1000) == LIBUSB_SUCCESS);
		assert(transferred == (int)sizeof(data));
	}

	test_env_teardown(&env);
}

int main(void)
{
	printf("test_io: control transfer...\n");
	test_control_transfer();
	printf("test_io: bulk transfer...\n");
	test_bulk_transfer();
	printf("test_io: interrupt transfer...\n");
	test_interrupt_transfer();
	printf("test_io: PASSED\n");
	return 0;
}
