#include "test_common.h"

struct hotplug_state {
	int arrivals;
	int removals;
	int enumerated;
	uint8_t last_bus;
	uint8_t last_address;
	libusb_hotplug_event last_event;
};

static int
count_hotplug_callback(libusb_context *ctx, libusb_device *device,
	libusb_hotplug_event event, void *user_data)
{
	struct hotplug_state *state = user_data;

	(void)ctx;
	if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
		state->arrivals++;
		state->enumerated++;
	} else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
		state->removals++;
	}
	state->last_bus = libusb_get_bus_number(device);
	state->last_address = libusb_get_device_address(device);
	state->last_event = event;
	return 0;
}

static void
test_enumerate_and_userdata(void)
{
	struct test_env env = {0};
	struct hotplug_state state = {0};
	libusb_hotplug_callback_handle handle;

	test_env_setup(&env);
	assert(libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG) == 1);
	assert(libusb_hotplug_register_callback(env.ctx,
	    LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
	    LIBUSB_HOTPLUG_ENUMERATE,
	    LIBUSB_HOTPLUG_MATCH_ANY,
	    LIBUSB_HOTPLUG_MATCH_ANY,
	    LIBUSB_HOTPLUG_MATCH_ANY,
	    count_hotplug_callback, &state, &handle) == LIBUSB_SUCCESS);
	assert(state.enumerated == 2);
	assert(libusb_hotplug_get_user_data(env.ctx, handle) == &state);
	libusb_hotplug_deregister_callback(env.ctx, handle);
	test_env_teardown(&env);
}

static void
test_arrival_and_removal(void)
{
	struct test_env env = {0};
	struct hotplug_state state = {0};
	libusb_hotplug_callback_handle handle;

	test_env_setup(&env);
	assert(libusb_hotplug_register_callback(env.ctx,
	    LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
	    LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
	    LIBUSB_HOTPLUG_NO_FLAGS,
	    0x1234, 0x5678, LIBUSB_HOTPLUG_MATCH_ANY,
	    count_hotplug_callback, &state, &handle) == LIBUSB_SUCCESS);

	create_fake_device(env.root, "bus2/dev8",
	    "port=3\nparent=1:2\nspeed=3\nactive_configuration=1\n");
	append_device_event(env.events_path, "add", "usb", "bus2/dev8");
	assert(libusb_handle_events_timeout_completed(env.ctx, &(struct timeval){0, 0},
	    NULL) == LIBUSB_SUCCESS);
	assert(state.arrivals == 1);
	assert(state.last_event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
	assert(state.last_bus == 2);
	assert(state.last_address == 8);

	remove_fake_device(env.root, "bus2/dev8");
	append_device_event(env.events_path, "remove", "usb", "bus2/dev8");
	assert(libusb_handle_events_timeout_completed(env.ctx, &(struct timeval){0, 0},
	    NULL) == LIBUSB_SUCCESS);
	assert(state.removals == 1);
	assert(state.last_event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);
	assert(state.last_bus == 2);
	assert(state.last_address == 8);

	libusb_hotplug_deregister_callback(env.ctx, handle);
	test_env_teardown(&env);
}

int main(void)
{
	printf("test_hotplug: enumerate/userdata...\n");
	test_enumerate_and_userdata();
	printf("test_hotplug: arrival/removal...\n");
	test_arrival_and_removal();
	printf("test_hotplug: PASSED\n");
	return 0;
}
