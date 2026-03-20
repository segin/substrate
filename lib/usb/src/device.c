#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int
libusb__parse_number(const char *name, const char *prefix, unsigned int *value)
{
	char *end;
	unsigned long parsed;
	size_t prefix_len;

	prefix_len = strlen(prefix);
	if (strncmp(name, prefix, prefix_len) != 0) {
		return 0;
	}
	parsed = strtoul(name + prefix_len, &end, 10);
	if (end == NULL || *end != '\0' || parsed > 255UL) {
		return 0;
	}
	*value = (unsigned int)parsed;
	return 1;
}

static int
libusb__device_compare(const void *lhs, const void *rhs)
{
	const libusb_device *const *left = lhs;
	const libusb_device *const *right = rhs;

	if ((*left)->bus_number != (*right)->bus_number) {
		return (int)(*left)->bus_number - (int)(*right)->bus_number;
	}
	return (int)(*left)->device_address - (int)(*right)->device_address;
}

static int
libusb__append_device(libusb_device ***devices, size_t *count, libusb_device *device)
{
	libusb_device **grown;

	grown = realloc(*devices, (*count + 1) * sizeof(**devices));
	if (grown == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}
	grown[*count] = device;
	*devices = grown;
	(*count)++;
	return LIBUSB_SUCCESS;
}

static int
libusb__parse_bus_address(const char *value, uint8_t *bus_number,
	uint8_t *device_address)
{
	char *end;
	unsigned long parsed;

	parsed = strtoul(value, &end, 10);
	if (end == value || end == NULL || *end != ':' || parsed > 255UL) {
		return 0;
	}
	*bus_number = (uint8_t)parsed;
	parsed = strtoul(end + 1, &end, 10);
	if (end == NULL || *end != '\0' || parsed > 255UL) {
		return 0;
	}
	*device_address = (uint8_t)parsed;
	return 1;
}

static void
libusb__trim_ascii(char *text)
{
	size_t len;

	len = strlen(text);
	while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r' ||
	    text[len - 1] == ' ' || text[len - 1] == '\t')) {
		text[--len] = '\0';
	}
}

static void
libusb__load_metadata(libusb_device *device)
{
	char meta_path[PATH_MAX];
	FILE *stream;
	char line[256];

	snprintf(meta_path, sizeof(meta_path), "%s.meta", device->path);
	stream = fopen(meta_path, "r");
	if (stream == NULL) {
		return;
	}

	while (fgets(line, sizeof(line), stream) != NULL) {
		char *equals;
		char *key;
		char *value;
		char *end;
		unsigned long parsed;
		unsigned int endpoint_number;

		libusb__trim_ascii(line);
		if (line[0] == '\0' || line[0] == '#') {
			continue;
		}
		equals = strchr(line, '=');
		if (equals == NULL) {
			continue;
		}
		*equals = '\0';
		key = line;
		value = equals + 1;
		if (strcmp(key, "port") == 0) {
			parsed = strtoul(value, &end, 10);
			if (end != NULL && *end == '\0' && parsed <= 255UL) {
				device->port_number = (uint8_t)parsed;
			}
		} else if (strcmp(key, "speed") == 0) {
			parsed = strtoul(value, &end, 10);
			if (end != NULL && *end == '\0' && parsed <= INT_MAX) {
				device->speed = (int)parsed;
			}
		} else if (strcmp(key, "parent") == 0) {
			device->parent_valid = libusb__parse_bus_address(value,
				&device->parent_bus_number, &device->parent_device_address);
		} else if (strcmp(key, "active_configuration") == 0) {
			parsed = strtoul(value, &end, 10);
			if (end != NULL && *end == '\0' && parsed <= INT_MAX) {
				device->active_configuration = (int)parsed;
			}
		} else if (sscanf(key, "ep%x_iso_max_packet_size", &endpoint_number) == 1 &&
		    endpoint_number <= 255U) {
			parsed = strtoul(value, &end, 0);
			if (end != NULL && *end == '\0' && parsed <= 0xffffUL) {
				device->endpoint_iso_max_packet[endpoint_number] = (uint16_t)parsed;
			}
		} else if (sscanf(key, "ep%x_max_packet_size", &endpoint_number) == 1 &&
		    endpoint_number <= 255U) {
			parsed = strtoul(value, &end, 0);
			if (end != NULL && *end == '\0' && parsed <= 0xffffUL) {
				device->endpoint_max_packet[endpoint_number] = (uint16_t)parsed;
			}
		}
	}

	fclose(stream);
}

static void
libusb__resolve_topology(libusb_device **devices, size_t count)
{
	size_t child_index;

	for (child_index = 0; child_index < count; child_index++) {
		libusb_device *child = devices[child_index];
		size_t parent_index;

		if (!child->parent_valid || child->parent != NULL) {
			continue;
		}
		for (parent_index = 0; parent_index < count; parent_index++) {
			libusb_device *candidate = devices[parent_index];

			if (candidate->bus_number == child->parent_bus_number &&
			    candidate->device_address == child->parent_device_address) {
				child->parent = libusb_ref_device(candidate);
				break;
			}
		}
	}
}

static int
libusb__find_endpoint_packet_size(libusb_device *dev, unsigned char endpoint,
	int is_isochronous)
{
	struct libusb_config_descriptor *config = NULL;
	int ret;
	uint8_t iface_index;
	uint16_t cached_size;

	cached_size = is_isochronous ? dev->endpoint_iso_max_packet[endpoint] :
	    dev->endpoint_max_packet[endpoint];
	if (cached_size != 0) {
		return (int)cached_size;
	}

	ret = libusb_get_active_config_descriptor(dev, &config);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}

	for (iface_index = 0; iface_index < config->bNumInterfaces; iface_index++) {
		const struct libusb_interface *iface = &config->interface[iface_index];
		int alt_index;

		for (alt_index = 0; alt_index < iface->num_altsetting; alt_index++) {
			const struct libusb_interface_descriptor *alt =
				&iface->altsetting[alt_index];
			int ep_index;

			for (ep_index = 0; ep_index < alt->bNumEndpoints; ep_index++) {
				const struct libusb_endpoint_descriptor *ep =
					&alt->endpoint[ep_index];
				uint16_t raw_size;
				int packet_size;

				if (ep->bEndpointAddress != endpoint) {
					continue;
				}
				raw_size = ep->wMaxPacketSize;
				packet_size = (int)(raw_size & 0x07ffU);
				if (is_isochronous) {
					packet_size *= (int)(((raw_size >> 11) & 0x3U) + 1U);
				}
				libusb_free_config_descriptor(config);
				return packet_size;
			}
		}
	}

	libusb_free_config_descriptor(config);
	return LIBUSB_ERROR_NOT_FOUND;
}

static void
libusb__append_path_component(char *path, size_t path_size, const char *component)
{
	size_t len;

	len = strnlen(path, path_size);
	if (len >= path_size - 1) {
		return;
	}
	path[len++] = '/';
	path[len] = '\0';
	libusb__strlcpy(path + len, component, path_size - len);
}

static int
libusb__scan_bus_dir(libusb_context *ctx, const char *root, const char *bus_name,
	unsigned int bus_number, libusb_device ***devices, size_t *count)
{
	DIR *dir;
	struct dirent *entry;
	char bus_path[256];

	(void)ctx;
	snprintf(bus_path, sizeof(bus_path), "%s/%s", root, bus_name);
	dir = opendir(bus_path);
	if (dir == NULL) {
		return libusb__map_errno(errno);
	}

	while ((entry = readdir(dir)) != NULL) {
		unsigned int device_address;
		libusb_device *device;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!libusb__parse_number(entry->d_name, "dev", &device_address)) {
			continue;
		}

		device = calloc(1, sizeof(*device));
		if (device == NULL) {
			closedir(dir);
			return LIBUSB_ERROR_NO_MEM;
		}
		device->ctx = ctx;
		device->refcount = 1;
		device->bus_number = (uint8_t)bus_number;
		device->device_address = (uint8_t)device_address;
		device->speed = LIBUSB_SPEED_UNKNOWN;
		libusb__strlcpy(device->path, root, sizeof(device->path));
		libusb__append_path_component(device->path, sizeof(device->path), bus_name);
		libusb__append_path_component(device->path, sizeof(device->path), entry->d_name);
		libusb__load_metadata(device);

		if (libusb__append_device(devices, count, device) != LIBUSB_SUCCESS) {
			free(device);
			closedir(dir);
			return LIBUSB_ERROR_NO_MEM;
		}
	}

	closedir(dir);
	return LIBUSB_SUCCESS;
}

void
libusb__free_cached_devices(libusb_context *ctx)
{
	size_t index;

	if (ctx == NULL || ctx->cached_devices == NULL) {
		return;
	}
	for (index = 0; index < ctx->cached_device_count; index++) {
		libusb_unref_device(ctx->cached_devices[index]);
	}
	free(ctx->cached_devices);
	ctx->cached_devices = NULL;
	ctx->cached_device_count = 0;
}

int
libusb__context_rescan(libusb_context *ctx)
{
	DIR *dir;
	struct dirent *entry;
	libusb_device **devices = NULL;
	size_t count = 0;
	int ret = LIBUSB_SUCCESS;
	const char *root;

	if (ctx == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}

	libusb__free_cached_devices(ctx);
	if (ctx->no_device_discovery) {
		return LIBUSB_SUCCESS;
	}

	root = libusb__devfs_root();
	dir = opendir(root);
	if (dir == NULL) {
		if (errno == ENOENT || errno == ENOTDIR) {
			return LIBUSB_SUCCESS;
		}
		return libusb__map_errno(errno);
	}

	while ((entry = readdir(dir)) != NULL) {
		unsigned int bus_number;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		if (!libusb__parse_number(entry->d_name, "bus", &bus_number)) {
			continue;
		}
		ret = libusb__scan_bus_dir(ctx, root, entry->d_name, bus_number, &devices,
			&count);
		if (ret != LIBUSB_SUCCESS) {
			break;
		}
	}
	closedir(dir);

	if (ret != LIBUSB_SUCCESS) {
		size_t index;
		for (index = 0; index < count; index++) {
			free(devices[index]);
		}
		free(devices);
		return ret;
	}

	if (count > 1) {
		qsort(devices, count, sizeof(*devices), libusb__device_compare);
	}
	libusb__resolve_topology(devices, count);
	ctx->cached_devices = devices;
	ctx->cached_device_count = count;
	return LIBUSB_SUCCESS;
}

ssize_t LIBUSB_CALL
libusb_get_device_list(libusb_context *ctx, libusb_device ***list)
{
	libusb_context *resolved;
	libusb_device **result;
	size_t index;
	int ret;

	if (list == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	resolved = libusb__resolve_context(ctx);
	if (resolved == NULL) {
		return LIBUSB_ERROR_OTHER;
	}

	ret = libusb__context_rescan(resolved);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}

	result = calloc(resolved->cached_device_count + 1, sizeof(*result));
	if (result == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}
	for (index = 0; index < resolved->cached_device_count; index++) {
		result[index] = libusb_ref_device(resolved->cached_devices[index]);
	}
	*list = result;
	return (ssize_t)resolved->cached_device_count;
}

void LIBUSB_CALL
libusb_free_device_list(libusb_device **list, int unref_devices)
{
	size_t index;

	if (list == NULL) {
		return;
	}
	if (unref_devices) {
		for (index = 0; list[index] != NULL; index++) {
			libusb_unref_device(list[index]);
		}
	}
	free(list);
}

libusb_device *LIBUSB_CALL
libusb_ref_device(libusb_device *dev)
{
	if (dev != NULL) {
		dev->refcount++;
	}
	return dev;
}

void LIBUSB_CALL
libusb_unref_device(libusb_device *dev)
{
	if (dev == NULL) {
		return;
	}
	if (--dev->refcount > 0) {
		return;
	}
	if (dev->parent != NULL) {
		libusb_unref_device(dev->parent);
	}
	free(dev);
}

uint8_t LIBUSB_CALL
libusb_get_bus_number(libusb_device *dev)
{
	return dev != NULL ? dev->bus_number : 0;
}

uint8_t LIBUSB_CALL
libusb_get_device_address(libusb_device *dev)
{
	return dev != NULL ? dev->device_address : 0;
}

uint8_t LIBUSB_CALL
libusb_get_port_number(libusb_device *dev)
{
	return dev != NULL ? dev->port_number : 0;
}

int LIBUSB_CALL
libusb_get_port_numbers(libusb_device *dev, uint8_t *port_numbers,
	int port_numbers_len)
{
	if (dev == NULL || port_numbers == NULL || port_numbers_len <= 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (dev->port_number == 0) {
		return 0;
	}
	{
		uint8_t reversed[8];
		int depth = 0;
		libusb_device *cursor = dev;
		int index;

		while (cursor != NULL && cursor->port_number != 0 &&
		    depth < (int)(sizeof(reversed) / sizeof(reversed[0]))) {
			reversed[depth++] = cursor->port_number;
			cursor = cursor->parent;
		}
		if (depth > port_numbers_len) {
			return LIBUSB_ERROR_OVERFLOW;
		}
		for (index = 0; index < depth; index++) {
			port_numbers[index] = reversed[depth - index - 1];
		}
		return depth;
	}
}

int LIBUSB_CALL
libusb_get_port_path(libusb_context *ctx, libusb_device *dev, uint8_t *path,
	uint8_t path_length)
{
	(void)ctx;
	return libusb_get_port_numbers(dev, path, path_length);
}

libusb_device *LIBUSB_CALL
libusb_get_parent(libusb_device *dev)
{
	return dev != NULL ? dev->parent : NULL;
}

int LIBUSB_CALL
libusb_get_device_speed(libusb_device *dev)
{
	return dev != NULL ? dev->speed : LIBUSB_SPEED_UNKNOWN;
}

int LIBUSB_CALL
libusb_get_max_packet_size(libusb_device *dev, unsigned char endpoint)
{
	if (dev == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	return libusb__find_endpoint_packet_size(dev, endpoint, 0);
}

int LIBUSB_CALL
libusb_get_max_iso_packet_size(libusb_device *dev, unsigned char endpoint)
{
	if (dev == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	return libusb__find_endpoint_packet_size(dev, endpoint, 1);
}

int LIBUSB_CALL
libusb_get_configuration(libusb_device_handle *dev_handle, int *config)
{
	if (dev_handle == NULL || config == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	*config = dev_handle->active_configuration;
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_wrap_sys_device(libusb_context *ctx, intptr_t sys_dev, libusb_device_handle **dev_handle)
{
	libusb_context *resolved;
	libusb_device_handle *handle;
	libusb_device *device;

	if (dev_handle == NULL || sys_dev < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	resolved = libusb__resolve_context(ctx);
	if (resolved == NULL) {
		return LIBUSB_ERROR_OTHER;
	}

	device = calloc(1, sizeof(*device));
	handle = calloc(1, sizeof(*handle));
	if (device == NULL || handle == NULL) {
		free(device);
		free(handle);
		return LIBUSB_ERROR_NO_MEM;
	}

	device->ctx = resolved;
	device->refcount = 1;
	device->speed = LIBUSB_SPEED_UNKNOWN;
	handle->device = device;
	handle->fd = (int)sys_dev;
	handle->owns_fd = 0;
	handle->active_configuration = device->active_configuration;
	libusb__register_handle(resolved, handle);
	*dev_handle = handle;
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_open(libusb_device *dev, libusb_device_handle **dev_handle)
{
	libusb_device_handle *handle;
	int fd;

	if (dev == NULL || dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	fd = open(dev->path, O_RDWR);
	if (fd < 0) {
		return libusb__map_errno(errno);
	}
	handle = calloc(1, sizeof(*handle));
	if (handle == NULL) {
		close(fd);
		return LIBUSB_ERROR_NO_MEM;
	}
	handle->device = libusb_ref_device(dev);
	handle->fd = fd;
	handle->owns_fd = 1;
	handle->active_configuration = dev->active_configuration;
	libusb__register_handle(dev->ctx, handle);
	*dev_handle = handle;
	return LIBUSB_SUCCESS;
}

void LIBUSB_CALL
libusb_close(libusb_device_handle *dev_handle)
{
	int index;

	if (dev_handle == NULL) {
		return;
	}
	for (index = 0; index < dev_handle->claimed_interface_count; index++) {
		unsigned int interface_number = (unsigned int)dev_handle->claimed_interfaces[index];
		(void)ioctl(dev_handle->fd, USBDEVFS_RELEASEINTERFACE, &interface_number);
	}
	if (dev_handle->device != NULL && dev_handle->device->ctx != NULL) {
		libusb__unregister_handle(dev_handle->device->ctx, dev_handle);
	}
	if (dev_handle->owns_fd && dev_handle->fd >= 0) {
		close(dev_handle->fd);
	}
	libusb_unref_device(dev_handle->device);
	free(dev_handle);
}

libusb_device *LIBUSB_CALL
libusb_get_device(libusb_device_handle *dev_handle)
{
	return dev_handle != NULL ? dev_handle->device : NULL;
}

static int
libusb__track_claimed_interface(libusb_device_handle *dev_handle, int interface_number)
{
	int index;

	for (index = 0; index < dev_handle->claimed_interface_count; index++) {
		if (dev_handle->claimed_interfaces[index] == interface_number) {
			return LIBUSB_SUCCESS;
		}
	}
	if (dev_handle->claimed_interface_count >= LIBUSB__MAX_CLAIMED_INTERFACES) {
		return LIBUSB_ERROR_BUSY;
	}
	dev_handle->claimed_interfaces[dev_handle->claimed_interface_count++] = interface_number;
	return LIBUSB_SUCCESS;
}

static void
libusb__untrack_claimed_interface(libusb_device_handle *dev_handle, int interface_number)
{
	int index;

	for (index = 0; index < dev_handle->claimed_interface_count; index++) {
		if (dev_handle->claimed_interfaces[index] == interface_number) {
			memmove(&dev_handle->claimed_interfaces[index],
				&dev_handle->claimed_interfaces[index + 1],
				(size_t)(dev_handle->claimed_interface_count - index - 1) * sizeof(int));
			dev_handle->claimed_interface_count--;
			return;
		}
	}
}

int LIBUSB_CALL
libusb_set_configuration(libusb_device_handle *dev_handle, int configuration)
{
	unsigned int request;

	if (dev_handle == NULL || configuration < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = (unsigned int)configuration;
	if (ioctl(dev_handle->fd, USBDEVFS_SETCONFIGURATION, &request) != 0) {
		return libusb__map_errno(errno);
	}
	dev_handle->active_configuration = configuration;
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_claim_interface(libusb_device_handle *dev_handle, int interface_number)
{
	unsigned int request;
	int ret;

	if (dev_handle == NULL || interface_number < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = (unsigned int)interface_number;
	if (dev_handle->auto_detach) {
		(void)ioctl(dev_handle->fd, USBDEVFS_DISCONNECT, NULL);
	}
	if (ioctl(dev_handle->fd, USBDEVFS_CLAIMINTERFACE, &request) != 0) {
		return libusb__map_errno(errno);
	}
	ret = libusb__track_claimed_interface(dev_handle, interface_number);
	if (ret != LIBUSB_SUCCESS) {
		(void)ioctl(dev_handle->fd, USBDEVFS_RELEASEINTERFACE, &request);
	}
	return ret;
}

int LIBUSB_CALL
libusb_release_interface(libusb_device_handle *dev_handle, int interface_number)
{
	unsigned int request;

	if (dev_handle == NULL || interface_number < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = (unsigned int)interface_number;
	if (ioctl(dev_handle->fd, USBDEVFS_RELEASEINTERFACE, &request) != 0) {
		return libusb__map_errno(errno);
	}
	libusb__untrack_claimed_interface(dev_handle, interface_number);
	return LIBUSB_SUCCESS;
}

libusb_device_handle *LIBUSB_CALL
libusb_open_device_with_vid_pid(libusb_context *ctx, uint16_t vendor_id, uint16_t product_id)
{
	libusb_device **list = NULL;
	ssize_t count;
	ssize_t index;
	libusb_device_handle *handle = NULL;

	count = libusb_get_device_list(ctx, &list);
	if (count < 0) {
		return NULL;
	}
	for (index = 0; index < count; index++) {
		if (libusb_get_device_descriptor(list[index], &list[index]->descriptor) ==
		    LIBUSB_SUCCESS &&
		    list[index]->descriptor.idVendor == vendor_id &&
		    list[index]->descriptor.idProduct == product_id &&
		    libusb_open(list[index], &handle) == LIBUSB_SUCCESS) {
			break;
		}
	}
	libusb_free_device_list(list, 1);
	return handle;
}

int LIBUSB_CALL
libusb_set_interface_alt_setting(libusb_device_handle *dev_handle,
	int interface_number, int alternate_setting)
{
	struct usbdevfs_setinterface request;

	if (dev_handle == NULL || interface_number < 0 || alternate_setting < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request.interface = (unsigned int)interface_number;
	request.altsetting = (unsigned int)alternate_setting;
	if (ioctl(dev_handle->fd, USBDEVFS_SETINTERFACE, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_clear_halt(libusb_device_handle *dev_handle, unsigned char endpoint)
{
	unsigned int request;

	if (dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	request = endpoint;
	if (ioctl(dev_handle->fd, USBDEVFS_CLEAR_HALT, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_reset_device(libusb_device_handle *dev_handle)
{
	if (dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (ioctl(dev_handle->fd, USBDEVFS_RESET, NULL) != 0) {
		return libusb__map_errno(errno);
	}
	dev_handle->active_configuration = dev_handle->device != NULL ?
		dev_handle->device->active_configuration : dev_handle->active_configuration;
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_kernel_driver_active(libusb_device_handle *dev_handle, int interface_number)
{
	struct usbdevfs_getdriver request;

	if (dev_handle == NULL || interface_number < 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	memset(&request, 0, sizeof(request));
	request.interface = (unsigned int)interface_number;
	if (ioctl(dev_handle->fd, USBDEVFS_GET_DRIVER, &request) != 0) {
		return libusb__map_errno(errno);
	}
	return request.driver[0] != '\0';
}

int LIBUSB_CALL
libusb_detach_kernel_driver(libusb_device_handle *dev_handle, int interface_number)
{
	(void)interface_number;
	if (dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (ioctl(dev_handle->fd, USBDEVFS_DISCONNECT, NULL) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_attach_kernel_driver(libusb_device_handle *dev_handle, int interface_number)
{
	(void)interface_number;
	if (dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	if (ioctl(dev_handle->fd, USBDEVFS_CONNECT, NULL) != 0) {
		return libusb__map_errno(errno);
	}
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_set_auto_detach_kernel_driver(libusb_device_handle *dev_handle, int enable)
{
	if (dev_handle == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	dev_handle->auto_detach = enable ? 1 : 0;
	return LIBUSB_SUCCESS;
}