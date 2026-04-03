/*
 * test_descriptor.c - Unit tests for libusb descriptor parsing:
 *                     device, config, interface, endpoint, string descriptors,
 *                     and struct size compatibility checks.
 */
#include "test_common.h"

static void test_device_descriptor(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	{
		struct libusb_device_descriptor desc;
		assert(libusb_get_device_descriptor(env.list[0], &desc) == LIBUSB_SUCCESS);
		assert(desc.bLength == 18);
		assert(desc.bDescriptorType == LIBUSB_DT_DEVICE);
		assert(desc.idVendor == 0x1234);
		assert(desc.idProduct == 0x5678);
		assert(desc.bcdUSB == 0x0200);
		assert(desc.bMaxPacketSize0 == 64);
		assert(desc.bNumConfigurations == 1);
	}

	test_env_teardown(&env);
}

static void test_config_descriptor(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	{
		struct libusb_config_descriptor *config = NULL;
		assert(libusb_get_config_descriptor(env.list[0], 0, &config) == LIBUSB_SUCCESS);
		assert(config != NULL);
		assert(config->bNumInterfaces == 1);
		assert(config->interface[0].num_altsetting == 1);
		assert(config->interface[0].altsetting[0].bNumEndpoints == 1);
		assert(config->interface[0].altsetting[0].bInterfaceClass == LIBUSB_CLASS_VENDOR_SPEC);
		assert(config->interface[0].altsetting[0].endpoint[0].bEndpointAddress == 0x81);
		assert((config->interface[0].altsetting[0].endpoint[0].bmAttributes & 0x03) ==
			LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK);
		libusb_free_config_descriptor(config);
	}

	test_env_teardown(&env);
}

static void test_string_descriptor(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	{
		unsigned char data[64];
		int len;

		len = libusb_get_string_descriptor_ascii(env.handle, 1, data, sizeof(data));
		assert(len == 4);
		assert(strcmp((char *)data, "Test") == 0);
	}

	test_env_teardown(&env);
}

static void test_struct_sizes(void)
{
	/* ABI compatibility: struct sizes must match standard libusb 1.0 */
	assert(sizeof(struct libusb_device_descriptor) == LIBUSB_DT_DEVICE_SIZE);
	assert(sizeof(struct libusb_endpoint_descriptor) >= LIBUSB_DT_ENDPOINT_SIZE);
	assert(sizeof(struct libusb_interface_descriptor) >= LIBUSB_DT_INTERFACE_SIZE);
}

int main(void)
{
	printf("test_descriptor: device descriptor...\n");
	test_device_descriptor();
	printf("test_descriptor: config descriptor...\n");
	test_config_descriptor();
	printf("test_descriptor: string descriptor...\n");
	test_string_descriptor();
	printf("test_descriptor: struct sizes...\n");
	test_struct_sizes();
	printf("test_descriptor: PASSED\n");
	return 0;
}
