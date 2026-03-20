#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static libusb_context *g_default_context;

static struct {
	int debug_level;
	int no_device_discovery;
	libusb_log_cb log_cb;
	char locale[16];
} g_default_options = {
	.debug_level = LIBUSB_LOG_LEVEL_NONE,
	.no_device_discovery = 0,
	.log_cb = NULL,
	.locale = "C",
};

static const struct libusb_version g_libusb_version = {
	.major = 1,
	.minor = 0,
	.micro = 29,
	.nano = 0,
	.rc = "",
	.describe = "Substrate libusb compatibility layer",
};

static const char *
libusb__device_events_path(void)
{
	const char *override = getenv("LIBUSB_DEVICE_EVENTS_PATH");

	if (override != NULL && override[0] != '\0') {
		return override;
	}
	return "/proc/device-events";
}

static void
libusb__free_hotplug_snapshot(libusb_context *ctx)
{
	size_t index;

	for (index = 0; index < ctx->hotplug_device_count; index++) {
		libusb_unref_device(ctx->hotplug_devices[index]);
	}
	free(ctx->hotplug_devices);
	ctx->hotplug_devices = NULL;
	ctx->hotplug_device_count = 0;
}

static int
libusb__same_device(const libusb_device *left, const libusb_device *right)
{
	return left->bus_number == right->bus_number &&
	    left->device_address == right->device_address;
}

static int
libusb__match_hotplug_filter(struct libusb__hotplug_callback *callback,
	libusb_device *device)
{
	struct libusb_device_descriptor desc;
	int ret;

	if (callback->vendor_id == LIBUSB_HOTPLUG_MATCH_ANY &&
	    callback->product_id == LIBUSB_HOTPLUG_MATCH_ANY &&
	    callback->dev_class == LIBUSB_HOTPLUG_MATCH_ANY) {
		return 1;
	}
	ret = libusb_get_device_descriptor(device, &desc);
	if (ret != LIBUSB_SUCCESS) {
		return 0;
	}
	if (callback->vendor_id != LIBUSB_HOTPLUG_MATCH_ANY &&
	    desc.idVendor != (uint16_t)callback->vendor_id) {
		return 0;
	}
	if (callback->product_id != LIBUSB_HOTPLUG_MATCH_ANY &&
	    desc.idProduct != (uint16_t)callback->product_id) {
		return 0;
	}
	if (callback->dev_class != LIBUSB_HOTPLUG_MATCH_ANY &&
	    desc.bDeviceClass != (uint8_t)callback->dev_class) {
		return 0;
	}
	return 1;
}

static void
libusb__deregister_hotplug_handle(libusb_context *ctx,
	libusb_hotplug_callback_handle handle)
{
	int index;

	for (index = 0; index < LIBUSB__MAX_HOTPLUG_CALLBACKS; index++) {
		if (ctx->hotplug_callbacks[index].in_use &&
		    ctx->hotplug_callbacks[index].handle == handle) {
			memset(&ctx->hotplug_callbacks[index], 0,
			    sizeof(ctx->hotplug_callbacks[index]));
			return;
		}
	}
}

static void
libusb__notify_hotplug(libusb_context *ctx, libusb_device *device,
	libusb_hotplug_event event)
{
	int index;

	for (index = 0; index < LIBUSB__MAX_HOTPLUG_CALLBACKS; index++) {
		struct libusb__hotplug_callback *callback = &ctx->hotplug_callbacks[index];

		if (!callback->in_use || (callback->events & event) == 0) {
			continue;
		}
		if (!libusb__match_hotplug_filter(callback, device)) {
			continue;
		}
		if (callback->cb_fn(ctx, device, event, callback->user_data)) {
			libusb__deregister_hotplug_handle(ctx, callback->handle);
		}
	}
}

static int
libusb__seed_hotplug_snapshot(libusb_context *ctx)
{
	size_t index;

	if (ctx->hotplug_devices != NULL || ctx->cached_device_count == 0) {
		return LIBUSB_SUCCESS;
	}
	ctx->hotplug_devices = calloc(ctx->cached_device_count,
	    sizeof(*ctx->hotplug_devices));
	if (ctx->hotplug_devices == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}
	for (index = 0; index < ctx->cached_device_count; index++) {
		ctx->hotplug_devices[index] = libusb_ref_device(ctx->cached_devices[index]);
	}
	ctx->hotplug_device_count = ctx->cached_device_count;
	return LIBUSB_SUCCESS;
}

static int
libusb__sync_hotplug_devices(libusb_context *ctx)
{
	libusb_device **current = NULL;
	ssize_t count;
	size_t old_index;
	size_t new_index;
	libusb_device **snapshot = NULL;

	count = libusb_get_device_list(ctx, &current);
	if (count < 0) {
		return (int)count;
	}
	for (old_index = 0; old_index < ctx->hotplug_device_count; old_index++) {
		int still_present = 0;

		for (new_index = 0; new_index < (size_t)count; new_index++) {
			if (libusb__same_device(ctx->hotplug_devices[old_index], current[new_index])) {
				still_present = 1;
				break;
			}
		}
		if (!still_present) {
			libusb__notify_hotplug(ctx, ctx->hotplug_devices[old_index],
			    LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT);
		}
	}
	for (new_index = 0; new_index < (size_t)count; new_index++) {
		int already_known = 0;

		for (old_index = 0; old_index < ctx->hotplug_device_count; old_index++) {
			if (libusb__same_device(current[new_index], ctx->hotplug_devices[old_index])) {
				already_known = 1;
				break;
			}
		}
		if (!already_known) {
			libusb__notify_hotplug(ctx, current[new_index],
			    LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED);
		}
	}
	if (count > 0) {
		snapshot = calloc((size_t)count, sizeof(*snapshot));
		if (snapshot == NULL) {
			libusb_free_device_list(current, 1);
			return LIBUSB_ERROR_NO_MEM;
		}
		for (new_index = 0; new_index < (size_t)count; new_index++) {
			snapshot[new_index] = libusb_ref_device(current[new_index]);
		}
	}
	libusb__free_hotplug_snapshot(ctx);
	ctx->hotplug_devices = snapshot;
	ctx->hotplug_device_count = (size_t)count;
	libusb_free_device_list(current, 1);
	return LIBUSB_SUCCESS;
}

static int
libusb__read_device_events(libusb_context *ctx, int *changed)
{
	const char *path = libusb__device_events_path();
	FILE *stream;
	char *buffer;
	long size;

	*changed = 0;
	stream = fopen(path, "r");
	if (stream == NULL) {
		return LIBUSB_SUCCESS;
	}
	if (fseek(stream, 0, SEEK_END) != 0) {
		fclose(stream);
		return LIBUSB_SUCCESS;
	}
	size = ftell(stream);
	if (size < 0 || fseek(stream, 0, SEEK_SET) != 0) {
		fclose(stream);
		return LIBUSB_SUCCESS;
	}
	buffer = calloc((size_t)size + 1, 1);
	if (buffer == NULL) {
		fclose(stream);
		return LIBUSB_ERROR_NO_MEM;
	}
	if (size > 0 && fread(buffer, 1, (size_t)size, stream) != (size_t)size) {
		free(buffer);
		fclose(stream);
		return LIBUSB_SUCCESS;
	}
	fclose(stream);
	if (ctx->hotplug_event_snapshot_len == 0) {
		*changed = strstr(buffer, "add usb ") != NULL ||
		    strstr(buffer, "remove usb ") != NULL;
	} else if ((size_t)size >= ctx->hotplug_event_snapshot_len &&
	    memcmp(buffer, ctx->hotplug_event_snapshot,
	    ctx->hotplug_event_snapshot_len) == 0) {
		*changed = strstr(buffer + ctx->hotplug_event_snapshot_len,
		    "add usb ") != NULL ||
		    strstr(buffer + ctx->hotplug_event_snapshot_len,
		    "remove usb ") != NULL;
	} else {
		*changed = strstr(buffer, "add usb ") != NULL ||
		    strstr(buffer, "remove usb ") != NULL;
	}
	free(ctx->hotplug_event_snapshot);
	ctx->hotplug_event_snapshot = buffer;
	ctx->hotplug_event_snapshot_len = (size_t)size;
	return LIBUSB_SUCCESS;
}

static void
libusb__apply_option_values(libusb_context *ctx, enum libusb_option option,
	int int_value, libusb_log_cb cb_value)
{
	if (option == LIBUSB_OPTION_LOG_LEVEL) {
		ctx->debug_level = int_value;
	} else if (option == LIBUSB_OPTION_NO_DEVICE_DISCOVERY) {
		ctx->no_device_discovery = 1;
	} else if (option == LIBUSB_OPTION_LOG_CB) {
		ctx->log_cb = cb_value;
	}
}

static void
libusb__apply_default_options(libusb_context *ctx)
{
	ctx->debug_level = g_default_options.debug_level;
	ctx->no_device_discovery = g_default_options.no_device_discovery;
	ctx->log_cb = g_default_options.log_cb;
	libusb__strlcpy(ctx->locale, g_default_options.locale, sizeof(ctx->locale));
}

static int
libusb__create_context(libusb_context **out_ctx)
{
	libusb_context *ctx;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}

	ctx->refcount = 1;
	ctx->event_pipe[0] = -1;
	ctx->event_pipe[1] = -1;
	libusb__apply_default_options(ctx);

	if (pipe(ctx->event_pipe) != 0) {
		free(ctx);
		return libusb__map_errno(errno);
	}
	/* Make event pipe non-blocking so reads don't hang */
	fcntl(ctx->event_pipe[0], F_SETFL,
		fcntl(ctx->event_pipe[0], F_GETFL) | O_NONBLOCK);
	fcntl(ctx->event_pipe[1], F_SETFL,
		fcntl(ctx->event_pipe[1], F_GETFL) | O_NONBLOCK);

	if (!ctx->no_device_discovery) {
		int ret = libusb__context_rescan(ctx);
		if (ret != LIBUSB_SUCCESS) {
			close(ctx->event_pipe[0]);
			close(ctx->event_pipe[1]);
			free(ctx);
			return ret;
		}
	}

	*out_ctx = ctx;
	return LIBUSB_SUCCESS;
}

const char *
libusb__devfs_root(void)
{
	const char *override = getenv("LIBUSB_DEVFS_ROOT");

	if (override != NULL && override[0] != '\0') {
		return override;
	}
	return USBDEVFS_ROOT_DIR;
}

libusb_context *
libusb__resolve_context(libusb_context *ctx)
{
	if (ctx != NULL) {
		return ctx;
	}
	if (g_default_context == NULL) {
		if (libusb_init(NULL) != LIBUSB_SUCCESS) {
			return NULL;
		}
	}
	return g_default_context;
}

int
libusb__map_errno(int err)
{
	switch (err) {
	case 0:
		return LIBUSB_SUCCESS;
	case EINVAL:
		return LIBUSB_ERROR_INVALID_PARAM;
	case EACCES:
	case EPERM:
		return LIBUSB_ERROR_ACCESS;
	case ENOENT:
		return LIBUSB_ERROR_NOT_FOUND;
	case ENODEV:
	case ENXIO:
		return LIBUSB_ERROR_NO_DEVICE;
	case EBUSY:
		return LIBUSB_ERROR_BUSY;
	case ETIMEDOUT:
		return LIBUSB_ERROR_TIMEOUT;
	case EOVERFLOW:
		return LIBUSB_ERROR_OVERFLOW;
	case EPIPE:
		return LIBUSB_ERROR_PIPE;
	case EINTR:
		return LIBUSB_ERROR_INTERRUPTED;
	case ENOMEM:
		return LIBUSB_ERROR_NO_MEM;
	case ENOSYS:
	case ENOTTY:
		return LIBUSB_ERROR_NOT_SUPPORTED;
	case EIO:
	default:
		return LIBUSB_ERROR_IO;
	}
}

int LIBUSB_CALL
libusb_init(libusb_context **ctx)
{
	return libusb_init_context(ctx, NULL, 0);
}

int LIBUSB_CALL
libusb_init_context(libusb_context **ctx, const struct libusb_init_option options[],
	int num_options)
{
	libusb_context *created;
	created = NULL;
	int ret;
	int index;

	if (ctx == NULL && g_default_context != NULL) {
		g_default_context->refcount++;
		return LIBUSB_SUCCESS;
	}

	ret = libusb__create_context(&created);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}

	for (index = 0; index < num_options; index++) {
		switch (options[index].option) {
		case LIBUSB_OPTION_LOG_LEVEL:
			libusb__apply_option_values(created, options[index].option,
				options[index].value.ival, NULL);
			break;
		case LIBUSB_OPTION_NO_DEVICE_DISCOVERY:
			libusb__apply_option_values(created, options[index].option, 1, NULL);
			break;
		case LIBUSB_OPTION_LOG_CB:
			libusb__apply_option_values(created, options[index].option, 0,
				options[index].value.log_cbval);
			break;
		default:
			break;
		}
	}

	if (ctx != NULL) {
		*ctx = created;
	} else {
		g_default_context = created;
	}

	if (!created->no_device_discovery) {
		(void)libusb__context_rescan(created);
	}

	return LIBUSB_SUCCESS;
}

void LIBUSB_CALL
libusb_exit(libusb_context *ctx)
{
	libusb_context *resolved = ctx;

	if (resolved == NULL) {
		resolved = g_default_context;
	}
	if (resolved == NULL) {
		return;
	}
	if (--resolved->refcount > 0) {
		return;
	}
	if (resolved == g_default_context) {
		g_default_context = NULL;
	}
	libusb__free_cached_devices(resolved);
	libusb__free_hotplug_snapshot(resolved);
	free(resolved->hotplug_event_snapshot);
	if (resolved->event_pipe[0] >= 0) {
		close(resolved->event_pipe[0]);
	}
	if (resolved->event_pipe[1] >= 0) {
		close(resolved->event_pipe[1]);
	}
	free(resolved);
}

void LIBUSB_CALL
libusb_set_debug(libusb_context *ctx, int level)
{
	libusb_context *resolved = ctx != NULL ? ctx : g_default_context;

	if (resolved != NULL) {
		resolved->debug_level = level;
	} else {
		g_default_options.debug_level = level;
	}
}

void LIBUSB_CALL
libusb_set_log_cb(libusb_context *ctx, libusb_log_cb cb, int mode)
{
	(void)mode;
	if (ctx != NULL) {
		ctx->log_cb = cb;
	} else {
		g_default_options.log_cb = cb;
	}
}

const struct libusb_version *LIBUSB_CALL
libusb_get_version(void)
{
	return &g_libusb_version;
}

int LIBUSB_CALL
libusb_has_capability(uint32_t capability)
{
	switch (capability) {
	case LIBUSB_CAP_HAS_CAPABILITY:
	case LIBUSB_CAP_HAS_HOTPLUG:
	case LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER:
		return 1;
	case LIBUSB_CAP_HAS_HID_ACCESS:
	default:
		return 0;
	}
}

const char *LIBUSB_CALL
libusb_error_name(int error_code)
{
	switch (error_code) {
	case LIBUSB_SUCCESS: return "LIBUSB_SUCCESS";
	case LIBUSB_ERROR_IO: return "LIBUSB_ERROR_IO";
	case LIBUSB_ERROR_INVALID_PARAM: return "LIBUSB_ERROR_INVALID_PARAM";
	case LIBUSB_ERROR_ACCESS: return "LIBUSB_ERROR_ACCESS";
	case LIBUSB_ERROR_NO_DEVICE: return "LIBUSB_ERROR_NO_DEVICE";
	case LIBUSB_ERROR_NOT_FOUND: return "LIBUSB_ERROR_NOT_FOUND";
	case LIBUSB_ERROR_BUSY: return "LIBUSB_ERROR_BUSY";
	case LIBUSB_ERROR_TIMEOUT: return "LIBUSB_ERROR_TIMEOUT";
	case LIBUSB_ERROR_OVERFLOW: return "LIBUSB_ERROR_OVERFLOW";
	case LIBUSB_ERROR_PIPE: return "LIBUSB_ERROR_PIPE";
	case LIBUSB_ERROR_INTERRUPTED: return "LIBUSB_ERROR_INTERRUPTED";
	case LIBUSB_ERROR_NO_MEM: return "LIBUSB_ERROR_NO_MEM";
	case LIBUSB_ERROR_NOT_SUPPORTED: return "LIBUSB_ERROR_NOT_SUPPORTED";
	case LIBUSB_ERROR_OTHER: return "LIBUSB_ERROR_OTHER";
	default: return "LIBUSB_ERROR_UNKNOWN";
	}
}

int LIBUSB_CALL
libusb_setlocale(const char *locale)
{
	if (locale == NULL || locale[0] == '\0') {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	libusb__strlcpy(g_default_options.locale, locale, sizeof(g_default_options.locale));
	if (g_default_context != NULL) {
		libusb__strlcpy(g_default_context->locale, locale, sizeof(g_default_context->locale));
	}
	return LIBUSB_SUCCESS;
}

const char *LIBUSB_CALL
libusb_strerror(int errcode)
{
	switch (errcode) {
	case LIBUSB_SUCCESS: return "Success";
	case LIBUSB_ERROR_IO: return "Input/output error";
	case LIBUSB_ERROR_INVALID_PARAM: return "Invalid parameter";
	case LIBUSB_ERROR_ACCESS: return "Access denied";
	case LIBUSB_ERROR_NO_DEVICE: return "No such device";
	case LIBUSB_ERROR_NOT_FOUND: return "Entity not found";
	case LIBUSB_ERROR_BUSY: return "Resource busy";
	case LIBUSB_ERROR_TIMEOUT: return "Operation timed out";
	case LIBUSB_ERROR_OVERFLOW: return "Overflow";
	case LIBUSB_ERROR_PIPE: return "Pipe error";
	case LIBUSB_ERROR_INTERRUPTED: return "System call interrupted";
	case LIBUSB_ERROR_NO_MEM: return "Insufficient memory";
	case LIBUSB_ERROR_NOT_SUPPORTED: return "Operation not supported";
	case LIBUSB_ERROR_OTHER: return "Other error";
	default: return "Unknown libusb error";
	}
}

int LIBUSB_CALLV
libusb_set_option(libusb_context *ctx, enum libusb_option option, ...)
{
	va_list ap;
	int int_value = 0;
	libusb_log_cb cb_value = NULL;
	libusb_context *resolved = ctx;

	va_start(ap, option);
	if (option == LIBUSB_OPTION_LOG_LEVEL) {
		int_value = va_arg(ap, int);
	} else if (option == LIBUSB_OPTION_LOG_CB) {
		cb_value = va_arg(ap, libusb_log_cb);
	} else if (option == LIBUSB_OPTION_NO_DEVICE_DISCOVERY) {
		int_value = 1;
	}
	va_end(ap);

	if (resolved == NULL) {
		if (option == LIBUSB_OPTION_LOG_LEVEL) {
			g_default_options.debug_level = int_value;
		} else if (option == LIBUSB_OPTION_LOG_CB) {
			g_default_options.log_cb = cb_value;
		} else if (option == LIBUSB_OPTION_NO_DEVICE_DISCOVERY) {
			g_default_options.no_device_discovery = 1;
		}
		return LIBUSB_SUCCESS;
	}

	libusb__apply_option_values(resolved, option, int_value, cb_value);
	return LIBUSB_SUCCESS;
}

int
libusb__poll_hotplug(libusb_context *ctx)
{
	int changed;
	int ret;
	int index;

	for (index = 0; index < LIBUSB__MAX_HOTPLUG_CALLBACKS; index++) {
		if (ctx->hotplug_callbacks[index].in_use) {
			break;
		}
	}
	if (index == LIBUSB__MAX_HOTPLUG_CALLBACKS) {
		return LIBUSB_SUCCESS;
	}
	ret = libusb__seed_hotplug_snapshot(ctx);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	ret = libusb__read_device_events(ctx, &changed);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	if (changed || ctx->hotplug_event_snapshot == NULL) {
		return libusb__sync_hotplug_devices(ctx);
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_hotplug_register_callback(libusb_context *ctx, int events, int flags,
	int vendor_id, int product_id, int dev_class,
	libusb_hotplug_callback_fn cb_fn, void *user_data,
	libusb_hotplug_callback_handle *callback_handle)
{
	libusb_context *resolved;
	int index;

	if (cb_fn == NULL || callback_handle == NULL ||
	    (events & (LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED |
	    LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT)) == 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	resolved = libusb__resolve_context(ctx);
	if (resolved == NULL) {
		return LIBUSB_ERROR_OTHER;
	}
	for (index = 0; index < LIBUSB__MAX_HOTPLUG_CALLBACKS; index++) {
		if (!resolved->hotplug_callbacks[index].in_use) {
			struct libusb__hotplug_callback *callback =
			    &resolved->hotplug_callbacks[index];
			callback->in_use = 1;
			callback->handle = ++resolved->next_hotplug_handle;
			callback->events = events;
			callback->flags = flags;
			callback->vendor_id = vendor_id;
			callback->product_id = product_id;
			callback->dev_class = dev_class;
			callback->cb_fn = cb_fn;
			callback->user_data = user_data;
			*callback_handle = callback->handle;
			if (libusb__seed_hotplug_snapshot(resolved) != LIBUSB_SUCCESS) {
				memset(callback, 0, sizeof(*callback));
				return LIBUSB_ERROR_NO_MEM;
			}
			if ((flags & LIBUSB_HOTPLUG_ENUMERATE) != 0 &&
			    (events & LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) != 0) {
				size_t device_index;
				for (device_index = 0;
				    device_index < resolved->hotplug_device_count;
				    device_index++) {
					if (libusb__match_hotplug_filter(callback,
					    resolved->hotplug_devices[device_index]) &&
					    callback->cb_fn(resolved,
					    resolved->hotplug_devices[device_index],
					    LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED,
					    callback->user_data)) {
						libusb__deregister_hotplug_handle(resolved,
						    callback->handle);
						break;
					}
				}
			}
			return LIBUSB_SUCCESS;
		}
	}
	return LIBUSB_ERROR_NO_MEM;
}

void LIBUSB_CALL
libusb_hotplug_deregister_callback(libusb_context *ctx,
	libusb_hotplug_callback_handle callback_handle)
{
	libusb_context *resolved = libusb__resolve_context(ctx);

	if (resolved == NULL) {
		return;
	}
	libusb__deregister_hotplug_handle(resolved, callback_handle);
}

void *LIBUSB_CALL
libusb_hotplug_get_user_data(libusb_context *ctx,
	libusb_hotplug_callback_handle callback_handle)
{
	libusb_context *resolved = libusb__resolve_context(ctx);
	int index;

	if (resolved == NULL) {
		return NULL;
	}
	for (index = 0; index < LIBUSB__MAX_HOTPLUG_CALLBACKS; index++) {
		if (resolved->hotplug_callbacks[index].in_use &&
		    resolved->hotplug_callbacks[index].handle == callback_handle) {
			return resolved->hotplug_callbacks[index].user_data;
		}
	}
	return NULL;
}