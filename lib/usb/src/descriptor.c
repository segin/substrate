#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint16_t
libusb__read_le16(const unsigned char *ptr)
{
	return (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
}

static int
libusb__with_temporary_handle(libusb_device *dev, libusb_device_handle **out)
{
	int fd;
	libusb_device_handle *handle;

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
	*out = handle;
	return LIBUSB_SUCCESS;
}

static int
libusb__fetch_descriptor(libusb_device *dev, uint8_t desc_type, uint8_t desc_index,
	unsigned char *buffer, uint16_t length)
{
	libusb_device_handle *handle = NULL;
	int ret;

	ret = libusb__with_temporary_handle(dev, &handle);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	ret = libusb_control_transfer(handle, LIBUSB_ENDPOINT_IN,
		LIBUSB_REQUEST_GET_DESCRIPTOR,
		(uint16_t)((desc_type << 8) | desc_index), 0, buffer, length, 1000);
	libusb_close(handle);
	return ret;
}

static int
libusb__load_device_descriptor(libusb_device *dev)
{
	unsigned char raw[LIBUSB_DT_DEVICE_SIZE];
	int ret;

	if (dev->descriptor_valid) {
		return LIBUSB_SUCCESS;
	}
	ret = libusb__fetch_descriptor(dev, LIBUSB_DT_DEVICE, 0, raw, sizeof(raw));
	if (ret < 0) {
		return ret;
	}
	dev->descriptor.bLength = raw[0];
	dev->descriptor.bDescriptorType = raw[1];
	dev->descriptor.bcdUSB = libusb__read_le16(raw + 2);
	dev->descriptor.bDeviceClass = raw[4];
	dev->descriptor.bDeviceSubClass = raw[5];
	dev->descriptor.bDeviceProtocol = raw[6];
	dev->descriptor.bMaxPacketSize0 = raw[7];
	dev->descriptor.idVendor = libusb__read_le16(raw + 8);
	dev->descriptor.idProduct = libusb__read_le16(raw + 10);
	dev->descriptor.bcdDevice = libusb__read_le16(raw + 12);
	dev->descriptor.iManufacturer = raw[14];
	dev->descriptor.iProduct = raw[15];
	dev->descriptor.iSerialNumber = raw[16];
	dev->descriptor.bNumConfigurations = raw[17];
	dev->descriptor_valid = 1;
	return LIBUSB_SUCCESS;
}

static unsigned char *
libusb__append_extra(unsigned char *existing, int *existing_len,
	const unsigned char *data, int data_len)
{
	unsigned char *grown;

	grown = realloc(existing, (size_t)(*existing_len + data_len));
	if (grown == NULL) {
		return NULL;
	}
	memcpy(grown + *existing_len, data, (size_t)data_len);
	*existing_len += data_len;
	return grown;
}

static int
libusb__parse_config_blob(const unsigned char *raw, int raw_len,
	struct libusb_config_descriptor **config_out)
{
	struct libusb_config_descriptor *config;
	int offset;
	int current_interface = -1;
	int current_altsetting = -1;
	struct libusb_endpoint_descriptor *current_endpoint = NULL;

	if (raw_len < LIBUSB_DT_CONFIG_SIZE) {
		return LIBUSB_ERROR_IO;
	}
	config = calloc(1, sizeof(*config));
	if (config == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}
	config->bLength = raw[0];
	config->bDescriptorType = raw[1];
	config->wTotalLength = libusb__read_le16(raw + 2);
	config->bNumInterfaces = raw[4];
	config->bConfigurationValue = raw[5];
	config->iConfiguration = raw[6];
	config->bmAttributes = raw[7];
	config->MaxPower = raw[8];
	config->interface = calloc(config->bNumInterfaces, sizeof(*config->interface));
	if (config->interface == NULL && config->bNumInterfaces != 0) {
		free(config);
		return LIBUSB_ERROR_NO_MEM;
	}

	for (offset = raw[0]; offset + 1 < raw_len;) {
		int length = raw[offset];
		int type;

		if (length <= 0 || offset + length > raw_len) {
			break;
		}
		type = raw[offset + 1];
		if (type == LIBUSB_DT_INTERFACE) {
			struct libusb_interface *iface;
			struct libusb_interface_descriptor *altsettings;

			current_interface = raw[offset + 2];
			if (current_interface >= config->bNumInterfaces) {
				offset += length;
				continue;
			}
			iface = (struct libusb_interface *)&config->interface[current_interface];
			altsettings = realloc((void *)iface->altsetting,
				(size_t)(iface->num_altsetting + 1) * sizeof(*altsettings));
			if (altsettings == NULL) {
				libusb_free_config_descriptor(config);
				return LIBUSB_ERROR_NO_MEM;
			}
			iface->altsetting = altsettings;
			current_altsetting = iface->num_altsetting++;
			memset(&altsettings[current_altsetting], 0, sizeof(*altsettings));
			altsettings[current_altsetting].bLength = raw[offset];
			altsettings[current_altsetting].bDescriptorType = raw[offset + 1];
			altsettings[current_altsetting].bInterfaceNumber = raw[offset + 2];
			altsettings[current_altsetting].bAlternateSetting = raw[offset + 3];
			altsettings[current_altsetting].bNumEndpoints = raw[offset + 4];
			altsettings[current_altsetting].bInterfaceClass = raw[offset + 5];
			altsettings[current_altsetting].bInterfaceSubClass = raw[offset + 6];
			altsettings[current_altsetting].bInterfaceProtocol = raw[offset + 7];
			altsettings[current_altsetting].iInterface = raw[offset + 8];
			/* Reset bNumEndpoints; it will be recomputed as
			   endpoint descriptors are parsed below. */
			altsettings[current_altsetting].bNumEndpoints = 0;
			current_endpoint = NULL;
		} else if (type == LIBUSB_DT_ENDPOINT && current_interface >= 0 &&
		    current_altsetting >= 0) {
			struct libusb_interface *iface;
			struct libusb_interface_descriptor *alt;
			struct libusb_endpoint_descriptor *endpoints;
			int endpoint_count;

			iface = (struct libusb_interface *)&config->interface[current_interface];
			alt = (struct libusb_interface_descriptor *)&iface->altsetting[current_altsetting];
			endpoint_count = alt->bNumEndpoints;
			endpoints = realloc((void *)alt->endpoint,
				(size_t)(endpoint_count + 1) * sizeof(*endpoints));
			if (endpoints == NULL) {
				libusb_free_config_descriptor(config);
				return LIBUSB_ERROR_NO_MEM;
			}
			alt->endpoint = endpoints;
			current_endpoint = &endpoints[endpoint_count];
			memset(current_endpoint, 0, sizeof(*current_endpoint));
			current_endpoint->bLength = raw[offset];
			current_endpoint->bDescriptorType = raw[offset + 1];
			current_endpoint->bEndpointAddress = raw[offset + 2];
			current_endpoint->bmAttributes = raw[offset + 3];
			current_endpoint->wMaxPacketSize = libusb__read_le16(raw + offset + 4);
			current_endpoint->bInterval = raw[offset + 6];
			if (length >= 9) {
				current_endpoint->bRefresh = raw[offset + 7];
				current_endpoint->bSynchAddress = raw[offset + 8];
			}
			alt->bNumEndpoints = (uint8_t)(endpoint_count + 1);
		} else {
			unsigned char *extra_copy;
			int *extra_len;

			if (current_endpoint != NULL) {
				extra_copy = (unsigned char *)current_endpoint->extra;
				extra_len = &current_endpoint->extra_length;
			} else if (current_interface >= 0 && current_altsetting >= 0) {
				struct libusb_interface *iface = (struct libusb_interface *)&config->interface[current_interface];
				struct libusb_interface_descriptor *alt =
					(struct libusb_interface_descriptor *)&iface->altsetting[current_altsetting];
				extra_copy = (unsigned char *)alt->extra;
				extra_len = &alt->extra_length;
			} else {
				extra_copy = (unsigned char *)config->extra;
				extra_len = &config->extra_length;
			}
			extra_copy = libusb__append_extra(extra_copy, extra_len, raw + offset, length);
			if (extra_copy == NULL) {
				libusb_free_config_descriptor(config);
				return LIBUSB_ERROR_NO_MEM;
			}
			if (current_endpoint != NULL) {
				current_endpoint->extra = extra_copy;
			} else if (current_interface >= 0 && current_altsetting >= 0) {
				struct libusb_interface *iface = (struct libusb_interface *)&config->interface[current_interface];
				struct libusb_interface_descriptor *alt =
					(struct libusb_interface_descriptor *)&iface->altsetting[current_altsetting];
				alt->extra = extra_copy;
			} else {
				config->extra = extra_copy;
			}
		}
		offset += length;
	}

	*config_out = config;
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_get_device_descriptor(libusb_device *dev, struct libusb_device_descriptor *desc)
{
	int ret;

	if (dev == NULL || desc == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = libusb__load_device_descriptor(dev);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	memcpy(desc, &dev->descriptor, sizeof(*desc));
	return LIBUSB_SUCCESS;
}

int LIBUSB_CALL
libusb_get_config_descriptor(libusb_device *dev, uint8_t config_index,
	struct libusb_config_descriptor **config)
{
	unsigned char header[LIBUSB_DT_CONFIG_SIZE];
	unsigned char *blob;
	int ret;
	int blob_len;

	if (dev == NULL || config == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = libusb__fetch_descriptor(dev, LIBUSB_DT_CONFIG, config_index, header,
		sizeof(header));
	if (ret < 0) {
		return ret;
	}
	blob_len = (int)libusb__read_le16(header + 2);
	if (blob_len < LIBUSB_DT_CONFIG_SIZE) {
		return LIBUSB_ERROR_IO;
	}
	blob = malloc((size_t)blob_len);
	if (blob == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}
	ret = libusb__fetch_descriptor(dev, LIBUSB_DT_CONFIG, config_index, blob,
		(uint16_t)blob_len);
	if (ret < 0) {
		free(blob);
		return ret;
	}
	ret = libusb__parse_config_blob(blob, ret, config);
	free(blob);
	return ret;
}

int LIBUSB_CALL
libusb_get_active_config_descriptor(libusb_device *dev,
	struct libusb_config_descriptor **config)
{
	return libusb_get_config_descriptor(dev, 0, config);
}

int LIBUSB_CALL
libusb_get_config_descriptor_by_value(libusb_device *dev, uint8_t configuration_value,
	struct libusb_config_descriptor **config)
{
	struct libusb_device_descriptor desc;
	uint8_t index;
	int ret;

	ret = libusb_get_device_descriptor(dev, &desc);
	if (ret != LIBUSB_SUCCESS) {
		return ret;
	}
	for (index = 0; index < desc.bNumConfigurations; index++) {
		struct libusb_config_descriptor *candidate = NULL;
		ret = libusb_get_config_descriptor(dev, index, &candidate);
		if (ret != LIBUSB_SUCCESS) {
			return ret;
		}
		if (candidate->bConfigurationValue == configuration_value) {
			*config = candidate;
			return LIBUSB_SUCCESS;
		}
		libusb_free_config_descriptor(candidate);
	}
	return LIBUSB_ERROR_NOT_FOUND;
}

void LIBUSB_CALL
libusb_free_config_descriptor(struct libusb_config_descriptor *config)
{
	uint8_t iface_index;

	if (config == NULL) {
		return;
	}
	for (iface_index = 0; iface_index < config->bNumInterfaces; iface_index++) {
		struct libusb_interface *iface = (struct libusb_interface *)&config->interface[iface_index];
		int alt_index;
		for (alt_index = 0; alt_index < iface->num_altsetting; alt_index++) {
			struct libusb_interface_descriptor *alt =
				(struct libusb_interface_descriptor *)&iface->altsetting[alt_index];
			int ep_index;
			for (ep_index = 0; ep_index < alt->bNumEndpoints; ep_index++) {
				struct libusb_endpoint_descriptor *ep =
					(struct libusb_endpoint_descriptor *)&alt->endpoint[ep_index];
				free((void *)ep->extra);
			}
			free((void *)alt->endpoint);
			free((void *)alt->extra);
		}
		free((void *)iface->altsetting);
	}
	free((void *)config->interface);
	free((void *)config->extra);
	free(config);
}

int LIBUSB_CALL
libusb_get_ss_endpoint_companion_descriptor(libusb_context *ctx,
	const struct libusb_endpoint_descriptor *endpoint,
	struct libusb_ss_endpoint_companion_descriptor **ep_comp)
{
	const unsigned char *extra;
	int remaining;

	(void)ctx;
	if (endpoint == NULL || ep_comp == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	extra = endpoint->extra;
	remaining = endpoint->extra_length;
	while (extra != NULL && remaining >= 2) {
		int length = extra[0];
		if (length <= 0 || length > remaining) {
			break;
		}
		if (extra[1] == LIBUSB_DT_SS_ENDPOINT_COMPANION && length >= 6) {
			struct libusb_ss_endpoint_companion_descriptor *desc = calloc(1, sizeof(*desc));
			if (desc == NULL) {
				return LIBUSB_ERROR_NO_MEM;
			}
			desc->bLength = extra[0];
			desc->bDescriptorType = extra[1];
			desc->bMaxBurst = extra[2];
			desc->bmAttributes = extra[3];
			desc->wBytesPerInterval = libusb__read_le16(extra + 4);
			*ep_comp = desc;
			return LIBUSB_SUCCESS;
		}
		extra += length;
		remaining -= length;
	}
	return LIBUSB_ERROR_NOT_FOUND;
}

void LIBUSB_CALL
libusb_free_ss_endpoint_companion_descriptor(
	struct libusb_ss_endpoint_companion_descriptor *ep_comp)
{
	free(ep_comp);
}

int LIBUSB_CALL
libusb_get_bos_descriptor(libusb_device_handle *dev_handle,
	struct libusb_bos_descriptor **bos)
{
	unsigned char header[LIBUSB_DT_BOS_SIZE];
	unsigned char *blob;
	struct libusb_bos_descriptor *parsed;
	int ret;
	int blob_len;
	int offset;
	int cap_index = 0;

	if (dev_handle == NULL || bos == NULL) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = libusb_control_transfer(dev_handle, LIBUSB_ENDPOINT_IN,
		LIBUSB_REQUEST_GET_DESCRIPTOR, (uint16_t)(LIBUSB_DT_BOS << 8), 0,
		header, sizeof(header), 1000);
	if (ret < 0) {
		return ret;
	}
	blob_len = (int)libusb__read_le16(header + 2);
	if (blob_len < LIBUSB_DT_BOS_SIZE) {
		return LIBUSB_ERROR_IO;
	}
	blob = malloc((size_t)blob_len);
	if (blob == NULL) {
		return LIBUSB_ERROR_NO_MEM;
	}
	ret = libusb_control_transfer(dev_handle, LIBUSB_ENDPOINT_IN,
		LIBUSB_REQUEST_GET_DESCRIPTOR, (uint16_t)(LIBUSB_DT_BOS << 8), 0,
		blob, (uint16_t)blob_len, 1000);
	if (ret < 0) {
		free(blob);
		return ret;
	}
	parsed = calloc(1, sizeof(*parsed) + ((size_t)blob[4] * sizeof(parsed->dev_capability[0])));
	if (parsed == NULL) {
		free(blob);
		return LIBUSB_ERROR_NO_MEM;
	}
	parsed->bLength = blob[0];
	parsed->bDescriptorType = blob[1];
	parsed->wTotalLength = libusb__read_le16(blob + 2);
	parsed->bNumDeviceCaps = blob[4];
	for (offset = blob[0]; offset + 2 < ret && cap_index < parsed->bNumDeviceCaps;) {
		int length = blob[offset];
		struct libusb_bos_dev_capability_descriptor *cap;
		if (length <= 0 || offset + length > ret) {
			break;
		}
		cap = malloc((size_t)length);
		if (cap == NULL) {
			libusb_free_bos_descriptor(parsed);
			free(blob);
			return LIBUSB_ERROR_NO_MEM;
		}
		memcpy(cap, blob + offset, (size_t)length);
		parsed->dev_capability[cap_index++] = cap;
		offset += length;
	}
	free(blob);
	*bos = parsed;
	return LIBUSB_SUCCESS;
}

void LIBUSB_CALL
libusb_free_bos_descriptor(struct libusb_bos_descriptor *bos)
{
	uint8_t index;

	if (bos == NULL) {
		return;
	}
	for (index = 0; index < bos->bNumDeviceCaps; index++) {
		free(bos->dev_capability[index]);
	}
	free(bos);
}

int LIBUSB_CALL
libusb_get_string_descriptor_ascii(libusb_device_handle *dev_handle,
	uint8_t desc_index, unsigned char *data, int length)
{
	unsigned char raw[256];
	int ret;
	int out_index = 0;
	int raw_index;

	if (dev_handle == NULL || data == NULL || length <= 0) {
		return LIBUSB_ERROR_INVALID_PARAM;
	}
	ret = libusb_get_string_descriptor(dev_handle, desc_index, 0x0409, raw,
		(int)sizeof(raw));
	if (ret < 0) {
		return ret;
	}
	for (raw_index = 2; raw_index + 1 < ret && out_index < length - 1; raw_index += 2) {
		data[out_index++] = raw[raw_index];
	}
	data[out_index] = '\0';
	return out_index;
}