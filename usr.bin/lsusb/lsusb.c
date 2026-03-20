/*
 * lsusb - list USB devices
 *
 * Enumerates USB devices using libusb and displays information
 * about each device attached to the system.
 */

#include <getopt.h>
#include <libusb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int opt_verbose;
static int opt_tree;
static int filter_bus = -1;
static int filter_dev = -1;
static int filter_vendor = -1;
static int filter_product = -1;

static const char *
class_name(uint8_t bclass)
{
	switch (bclass) {
	case LIBUSB_CLASS_PER_INTERFACE:      return "(Defined at Interface level)";
	case LIBUSB_CLASS_AUDIO:              return "Audio";
	case LIBUSB_CLASS_COMM:               return "Communications";
	case LIBUSB_CLASS_HID:                return "Human Interface Device";
	case LIBUSB_CLASS_PHYSICAL:           return "Physical";
	case LIBUSB_CLASS_IMAGE:              return "Still Image Capture";
	case LIBUSB_CLASS_PRINTER:            return "Printer";
	case LIBUSB_CLASS_MASS_STORAGE:       return "Mass Storage";
	case LIBUSB_CLASS_HUB:               return "Hub";
	case LIBUSB_CLASS_DATA:              return "CDC-Data";
	case LIBUSB_CLASS_SMART_CARD:        return "Smart Card";
	case LIBUSB_CLASS_CONTENT_SECURITY:  return "Content Security";
	case LIBUSB_CLASS_VIDEO:             return "Video";
	case LIBUSB_CLASS_PERSONAL_HEALTHCARE: return "Personal Healthcare";
	case LIBUSB_CLASS_DIAGNOSTIC_DEVICE: return "Diagnostic Device";
	case LIBUSB_CLASS_WIRELESS:          return "Wireless";
	case LIBUSB_CLASS_MISCELLANEOUS:     return "Miscellaneous";
	case LIBUSB_CLASS_APPLICATION:       return "Application Specific";
	case LIBUSB_CLASS_VENDOR_SPEC:       return "Vendor Specific";
	default:                             return "Unknown";
	}
}

static const char *
speed_name(int speed)
{
	switch (speed) {
	case LIBUSB_SPEED_LOW:            return "1.5M";
	case LIBUSB_SPEED_FULL:           return "12M";
	case LIBUSB_SPEED_HIGH:           return "480M";
	case LIBUSB_SPEED_SUPER:          return "5000M";
	case LIBUSB_SPEED_SUPER_PLUS:     return "10000M";
	case LIBUSB_SPEED_SUPER_PLUS_X2:  return "20000M";
	default:                          return "Unknown";
	}
}

static const char *
transfer_type_name(uint8_t bmAttributes)
{
	switch (bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_CONTROL:     return "Control";
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_ISOCHRONOUS: return "Isochronous";
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK:        return "Bulk";
	case LIBUSB_ENDPOINT_TRANSFER_TYPE_INTERRUPT:   return "Interrupt";
	default:                                        return "Unknown";
	}
}

static void
get_string(libusb_device_handle *handle, uint8_t index, char *buf, size_t bufsz)
{
	int ret;

	buf[0] = '\0';
	if (index == 0 || handle == NULL) {
		return;
	}
	ret = libusb_get_string_descriptor_ascii(handle, index,
		(unsigned char *)buf, (int)bufsz);
	if (ret < 0) {
		buf[0] = '\0';
	}
}

static void
print_endpoint(const struct libusb_endpoint_descriptor *ep, const char *indent)
{
	printf("%s    Endpoint Descriptor:\n", indent);
	printf("%s      bLength             %5u\n", indent, ep->bLength);
	printf("%s      bDescriptorType     %5u\n", indent, ep->bDescriptorType);
	printf("%s      bEndpointAddress     0x%02x  EP %u %s\n", indent,
		ep->bEndpointAddress,
		ep->bEndpointAddress & LIBUSB_ENDPOINT_ADDRESS_MASK,
		(ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ? "IN" : "OUT");
	printf("%s      bmAttributes        %5u\n", indent, ep->bmAttributes);
	printf("%s        Transfer Type            %s\n", indent,
		transfer_type_name(ep->bmAttributes));
	printf("%s      wMaxPacketSize     0x%04x  %u bytes\n", indent,
		ep->wMaxPacketSize, ep->wMaxPacketSize & 0x07ff);
	printf("%s      bInterval           %5u\n", indent, ep->bInterval);
}

static void
print_interface(const struct libusb_interface *iface, libusb_device_handle *handle,
	const char *indent)
{
	int alt;

	for (alt = 0; alt < iface->num_altsetting; alt++) {
		const struct libusb_interface_descriptor *desc = &iface->altsetting[alt];
		uint8_t ep;
		char iface_str[256];

		printf("%s  Interface Descriptor:\n", indent);
		printf("%s    bLength             %5u\n", indent, desc->bLength);
		printf("%s    bDescriptorType     %5u\n", indent, desc->bDescriptorType);
		printf("%s    bInterfaceNumber    %5u\n", indent, desc->bInterfaceNumber);
		printf("%s    bAlternateSetting   %5u\n", indent, desc->bAlternateSetting);
		printf("%s    bNumEndpoints       %5u\n", indent, desc->bNumEndpoints);
		printf("%s    bInterfaceClass     %5u %s\n", indent,
			desc->bInterfaceClass, class_name(desc->bInterfaceClass));
		printf("%s    bInterfaceSubClass  %5u\n", indent, desc->bInterfaceSubClass);
		printf("%s    bInterfaceProtocol  %5u\n", indent, desc->bInterfaceProtocol);

		get_string(handle, desc->iInterface, iface_str, sizeof(iface_str));
		printf("%s    iInterface          %5u %s\n", indent,
			desc->iInterface, iface_str);

		for (ep = 0; ep < desc->bNumEndpoints; ep++) {
			print_endpoint(&desc->endpoint[ep], indent);
		}
	}
}

static void
print_config(struct libusb_config_descriptor *config, libusb_device_handle *handle,
	const char *indent)
{
	uint8_t iface_idx;
	char config_str[256];

	printf("%sConfiguration Descriptor:\n", indent);
	printf("%s  bLength             %5u\n", indent, config->bLength);
	printf("%s  bDescriptorType     %5u\n", indent, config->bDescriptorType);
	printf("%s  wTotalLength       0x%04x\n", indent, config->wTotalLength);
	printf("%s  bNumInterfaces     %5u\n", indent, config->bNumInterfaces);
	printf("%s  bConfigurationValue%5u\n", indent, config->bConfigurationValue);

	get_string(handle, config->iConfiguration, config_str, sizeof(config_str));
	printf("%s  iConfiguration     %5u %s\n", indent,
		config->iConfiguration, config_str);

	printf("%s  bmAttributes         0x%02x\n", indent, config->bmAttributes);
	if (config->bmAttributes & 0x40)
		printf("%s    Self Powered\n", indent);
	if (config->bmAttributes & 0x20)
		printf("%s    Remote Wakeup\n", indent);
	printf("%s  MaxPower            %5umA\n", indent, config->MaxPower * 2);

	for (iface_idx = 0; iface_idx < config->bNumInterfaces; iface_idx++) {
		print_interface(&config->interface[iface_idx], handle, indent);
	}
}

static void
print_device_verbose(libusb_device *dev, struct libusb_device_descriptor *desc)
{
	libusb_device_handle *handle = NULL;
	struct libusb_config_descriptor *config;
	char manufacturer[256], product[256], serial[256];
	uint8_t cfg;
	int ret;

	ret = libusb_open(dev, &handle);
	if (ret != LIBUSB_SUCCESS) {
		handle = NULL;
	}

	get_string(handle, desc->iManufacturer, manufacturer, sizeof(manufacturer));
	get_string(handle, desc->iProduct, product, sizeof(product));
	get_string(handle, desc->iSerialNumber, serial, sizeof(serial));

	printf("Device Descriptor:\n");
	printf("  bLength             %5u\n", desc->bLength);
	printf("  bDescriptorType     %5u\n", desc->bDescriptorType);
	printf("  bcdUSB             %2x.%02x\n",
		desc->bcdUSB >> 8, desc->bcdUSB & 0xff);
	printf("  bDeviceClass        %5u %s\n",
		desc->bDeviceClass, class_name(desc->bDeviceClass));
	printf("  bDeviceSubClass     %5u\n", desc->bDeviceSubClass);
	printf("  bDeviceProtocol     %5u\n", desc->bDeviceProtocol);
	printf("  bMaxPacketSize0     %5u\n", desc->bMaxPacketSize0);
	printf("  idVendor           0x%04x\n", desc->idVendor);
	printf("  idProduct          0x%04x\n", desc->idProduct);
	printf("  bcdDevice          %2x.%02x\n",
		desc->bcdDevice >> 8, desc->bcdDevice & 0xff);
	printf("  iManufacturer       %5u %s\n", desc->iManufacturer, manufacturer);
	printf("  iProduct            %5u %s\n", desc->iProduct, product);
	printf("  iSerialNumber       %5u %s\n", desc->iSerialNumber, serial);
	printf("  bNumConfigurations  %5u\n", desc->bNumConfigurations);

	for (cfg = 0; cfg < desc->bNumConfigurations; cfg++) {
		ret = libusb_get_config_descriptor(dev, cfg, &config);
		if (ret != LIBUSB_SUCCESS) {
			fprintf(stderr, "  Couldn't get configuration descriptor %u: %s\n",
				cfg, libusb_error_name(ret));
			continue;
		}
		print_config(config, handle, "  ");
		libusb_free_config_descriptor(config);
	}

	if (handle != NULL) {
		libusb_close(handle);
	}
}

static int
device_matches_filter(libusb_device *dev, struct libusb_device_descriptor *desc)
{
	if (filter_bus >= 0 && libusb_get_bus_number(dev) != filter_bus)
		return 0;
	if (filter_dev >= 0 && libusb_get_device_address(dev) != filter_dev)
		return 0;
	if (filter_vendor >= 0 && desc->idVendor != (uint16_t)filter_vendor)
		return 0;
	if (filter_product >= 0 && desc->idProduct != (uint16_t)filter_product)
		return 0;
	return 1;
}

static int
list_devices(libusb_context *ctx)
{
	libusb_device **list = NULL;
	ssize_t count, i;
	int ret;

	count = libusb_get_device_list(ctx, &list);
	if (count < 0) {
		fprintf(stderr, "lsusb: failed to get device list: %s\n",
			libusb_error_name((int)count));
		return 1;
	}

	for (i = 0; i < count; i++) {
		struct libusb_device_descriptor desc;

		ret = libusb_get_device_descriptor(list[i], &desc);
		if (ret != LIBUSB_SUCCESS) {
			fprintf(stderr, "lsusb: failed to get descriptor: %s\n",
				libusb_error_name(ret));
			continue;
		}

		if (!device_matches_filter(list[i], &desc))
			continue;

		printf("Bus %03u Device %03u: ID %04x:%04x",
			libusb_get_bus_number(list[i]),
			libusb_get_device_address(list[i]),
			desc.idVendor, desc.idProduct);

		if (opt_verbose) {
			libusb_device_handle *handle = NULL;
			char product[256];
			product[0] = '\0';
			if (libusb_open(list[i], &handle) == LIBUSB_SUCCESS) {
				get_string(handle, desc.iProduct, product, sizeof(product));
				libusb_close(handle);
			}
			if (product[0] != '\0')
				printf(" %s", product);
		}
		printf("\n");

		if (opt_verbose) {
			print_device_verbose(list[i], &desc);
			printf("\n");
		}
	}

	libusb_free_device_list(list, 1);
	return 0;
}

static int
tree_devices(libusb_context *ctx)
{
	libusb_device **list = NULL;
	ssize_t count, i;
	int last_bus = -1;

	count = libusb_get_device_list(ctx, &list);
	if (count < 0) {
		fprintf(stderr, "lsusb: failed to get device list: %s\n",
			libusb_error_name((int)count));
		return 1;
	}

	for (i = 0; i < count; i++) {
		struct libusb_device_descriptor desc;
		int bus, addr, speed;
		int ret;

		ret = libusb_get_device_descriptor(list[i], &desc);
		if (ret != LIBUSB_SUCCESS)
			continue;

		if (!device_matches_filter(list[i], &desc))
			continue;

		bus = libusb_get_bus_number(list[i]);
		addr = libusb_get_device_address(list[i]);
		speed = libusb_get_device_speed(list[i]);

		if (bus != last_bus) {
			printf("/:  Bus %03d.Port 1: Dev 1, Class=root_hub\n", bus);
			last_bus = bus;
		}

		if (addr != 1) {
			uint8_t port = libusb_get_port_number(list[i]);
			printf("    |__ Port %u: Dev %u, If 0, Class=%s, Driver=, %sMbit/s\n",
				port, addr,
				class_name(desc.bDeviceClass),
				speed_name(speed));
		}
	}

	libusb_free_device_list(list, 1);
	return 0;
}

static int
parse_bus_dev(const char *arg)
{
	const char *colon;
	char *end;
	unsigned long val;

	colon = strchr(arg, ':');
	if (colon == NULL) {
		fprintf(stderr, "lsusb: invalid -s format, expected [bus]:[devnum]\n");
		return -1;
	}

	if (colon != arg) {
		val = strtoul(arg, &end, 10);
		if (end != colon || val > 255) {
			fprintf(stderr, "lsusb: invalid bus number in -s\n");
			return -1;
		}
		filter_bus = (int)val;
	}

	if (colon[1] != '\0') {
		val = strtoul(colon + 1, &end, 10);
		if (*end != '\0' || val > 255) {
			fprintf(stderr, "lsusb: invalid device number in -s\n");
			return -1;
		}
		filter_dev = (int)val;
	}

	return 0;
}

static int
parse_vid_pid(const char *arg)
{
	const char *colon;
	char *end;
	unsigned long val;

	colon = strchr(arg, ':');
	if (colon == NULL) {
		fprintf(stderr, "lsusb: invalid -d format, expected [vendor]:[product]\n");
		return -1;
	}

	if (colon != arg) {
		val = strtoul(arg, &end, 16);
		if (end != colon || val > 0xffff) {
			fprintf(stderr, "lsusb: invalid vendor ID in -d\n");
			return -1;
		}
		filter_vendor = (int)val;
	}

	if (colon[1] != '\0') {
		val = strtoul(colon + 1, &end, 16);
		if (*end != '\0' || val > 0xffff) {
			fprintf(stderr, "lsusb: invalid product ID in -d\n");
			return -1;
		}
		filter_product = (int)val;
	}

	return 0;
}

static void
usage(void)
{
	fprintf(stderr,
		"Usage: lsusb [options]\n"
		"  -v            Verbose (show descriptors)\n"
		"  -t            Show device tree\n"
		"  -s [bus]:[devnum]  Show only device with bus/devnum\n"
		"  -d [vendor]:[product]  Show only devices with vendor/product ID\n"
		"  -V            Show version\n"
		"  -h            Show this help\n");
}

int
main(int argc, char **argv)
{
	libusb_context *ctx = NULL;
	int ch, ret;

	while ((ch = getopt(argc, argv, "d:hs:tvV")) != -1) {
		switch (ch) {
		case 'd':
			if (parse_vid_pid(optarg) < 0)
				return 1;
			break;
		case 's':
			if (parse_bus_dev(optarg) < 0)
				return 1;
			break;
		case 't':
			opt_tree = 1;
			break;
		case 'v':
			opt_verbose = 1;
			break;
		case 'V':
			printf("lsusb (Substrate) using libusb v%u.%u.%u.%u\n",
				LIBUSB_API_VERSION >> 24,
				(LIBUSB_API_VERSION >> 16) & 0xff,
				(LIBUSB_API_VERSION >> 8) & 0xff,
				LIBUSB_API_VERSION & 0xff);
			return 0;
		case 'h':
		default:
			usage();
			return (ch == 'h') ? 0 : 1;
		}
	}

	ret = libusb_init(&ctx);
	if (ret != LIBUSB_SUCCESS) {
		fprintf(stderr, "lsusb: libusb_init failed: %s\n",
			libusb_error_name(ret));
		return 1;
	}

	if (opt_tree)
		ret = tree_devices(ctx);
	else
		ret = list_devices(ctx);

	libusb_exit(ctx);
	return ret;
}
