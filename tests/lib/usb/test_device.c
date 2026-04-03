/*
 * test_device.c - Unit tests for libusb device list management,
 *                 bus/address queries, open/close, refcounting.
 */
#include "test_common.h"

static void test_device_list(void)
{
	struct test_env env = {0};
	char root[128];

	create_fake_tree(root, sizeof(root));
	assert(setenv("LIBUSB_DEVFS_ROOT", root, 1) == 0);

	{
		libusb_context *ctx = NULL;
		libusb_device **list = NULL;
		ssize_t count;

		assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
		count = libusb_get_device_list(ctx, &list);
		assert(count == 2);
		assert(list[count] == NULL); /* NULL-terminated */

		/* Verify bus numbers and device addresses */
		assert(libusb_get_bus_number(list[0]) == 1);
		assert(libusb_get_device_address(list[0]) == 2);
		assert(libusb_get_bus_number(list[1]) == 2);
		assert(libusb_get_device_address(list[1]) == 7);

		libusb_free_device_list(list, 1);
		libusb_exit(ctx);
	}

	(void)env;
	assert(unsetenv("LIBUSB_DEVFS_ROOT") == 0);
	remove_fake_tree(root);
}

static void test_open_close(void)
{
	struct test_env env = {0};

	test_env_setup(&env);

	/* Verify device back-reference */
	assert(libusb_get_device(env.handle) == env.list[0]);

	/* get_configuration should succeed via ioctl stub */
	{
		int config = -1;
		assert(libusb_get_configuration(env.handle, &config) == LIBUSB_SUCCESS);
		assert(config == 1);
	}

	/* auto-detach kernel driver */
	assert(libusb_set_auto_detach_kernel_driver(env.handle, 1) == LIBUSB_SUCCESS);

	test_env_teardown(&env);
}

static void test_topology_and_packet_helpers(void)
{
	struct test_env env = {0};
	uint8_t ports[8];
	struct libusb_device_descriptor desc;

	test_env_setup(&env);

	assert(libusb_get_port_number(env.list[0]) == 1);
	assert(libusb_get_port_numbers(env.list[0], ports, 8) == 1);
	assert(ports[0] == 1);
	assert(libusb_get_parent(env.list[0]) == NULL);
	assert(libusb_get_device_speed(env.list[0]) == LIBUSB_SPEED_HIGH);
	assert(libusb_get_max_packet_size(env.list[0], 0x81) == 64);

	assert(libusb_get_port_numbers(env.list[1], ports, 8) == 2);
	assert(ports[0] == 1);
	assert(ports[1] == 2);
	assert(libusb_get_parent(env.list[1]) == env.list[0]);
	assert(libusb_get_device_speed(env.list[1]) == LIBUSB_SPEED_SUPER);
	assert(libusb_get_device_descriptor(env.list[1], &desc) == LIBUSB_SUCCESS);
	assert(desc.bNumConfigurations == 1);
	assert(libusb_get_max_iso_packet_size(env.list[1], 0x82) == 1024);

	test_env_teardown(&env);
}

static void test_ref_unref(void)
{
	char root[128];
	libusb_context *ctx = NULL;
	libusb_device **list = NULL;
	libusb_device *dev;
	ssize_t count;

	create_fake_tree(root, sizeof(root));
	assert(setenv("LIBUSB_DEVFS_ROOT", root, 1) == 0);
	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
	count = libusb_get_device_list(ctx, &list);
	assert(count >= 1);

	/* ref + unref should be balanced */
	dev = libusb_ref_device(list[0]);
	assert(dev == list[0]);
	libusb_unref_device(dev);

	libusb_free_device_list(list, 1);
	libusb_exit(ctx);
	assert(unsetenv("LIBUSB_DEVFS_ROOT") == 0);
	remove_fake_tree(root);
}

int main(void)
{
	printf("test_device: device list...\n");
	test_device_list();
	printf("test_device: open/close...\n");
	test_open_close();
	printf("test_device: ref/unref...\n");
	test_ref_unref();
	printf("test_device: topology/helpers...\n");
	test_topology_and_packet_helpers();
	printf("test_device: PASSED\n");
	return 0;
}
