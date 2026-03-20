/*
 * test_core.c - Unit tests for libusb core init/exit, version, capabilities,
 *               error names, log levels, and default context.
 */
#include "test_common.h"

static void test_init_exit(void)
{
	libusb_context *ctx = NULL;

	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
	assert(ctx != NULL);
	libusb_exit(ctx);
}

static void test_default_context(void)
{
	assert(libusb_init(NULL) == LIBUSB_SUCCESS);
	assert(libusb_set_option(NULL, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_DEBUG) == LIBUSB_SUCCESS);
	libusb_exit(NULL);
}

static void test_version(void)
{
	const struct libusb_version *ver = libusb_get_version();
	assert(ver != NULL);
	assert(ver->major == 1);
}

static void test_capability(void)
{
	libusb_context *ctx = NULL;
	assert(libusb_init(&ctx) == LIBUSB_SUCCESS);
	assert(libusb_has_capability(LIBUSB_CAP_HAS_CAPABILITY) == 1);
	libusb_exit(ctx);
}

static void test_error_names(void)
{
	assert(strcmp(libusb_error_name(LIBUSB_ERROR_BUSY), "LIBUSB_ERROR_BUSY") == 0);
	assert(strcmp(libusb_error_name(LIBUSB_ERROR_IO), "LIBUSB_ERROR_IO") == 0);
	assert(strcmp(libusb_error_name(LIBUSB_ERROR_NOT_FOUND), "LIBUSB_ERROR_NOT_FOUND") == 0);
	assert(strcmp(libusb_error_name(LIBUSB_ERROR_NO_MEM), "LIBUSB_ERROR_NO_MEM") == 0);
	assert(strcmp(libusb_error_name(LIBUSB_SUCCESS), "LIBUSB_SUCCESS") == 0);

	assert(strcmp(libusb_strerror(LIBUSB_ERROR_TIMEOUT), "Operation timed out") == 0);
	assert(strcmp(libusb_strerror(LIBUSB_SUCCESS), "Success") == 0);
}

int main(void)
{
	printf("test_core: init/exit...\n");
	test_init_exit();
	printf("test_core: default context...\n");
	test_default_context();
	printf("test_core: version...\n");
	test_version();
	printf("test_core: capability...\n");
	test_capability();
	printf("test_core: error names...\n");
	test_error_names();
	printf("test_core: PASSED\n");
	return 0;
}
