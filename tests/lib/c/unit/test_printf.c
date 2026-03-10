#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

// Prefixed symbols
extern int mys_sprintf(char *str, const char *format, ...);
extern int mys_snprintf(char *str, size_t size, const char *format, ...);
extern int mys_asprintf(char **ret, const char *format, ...);

void test_printf_basic(void) {
	char buf[128];
	int n = mys_sprintf(buf, "Hello %d %s", 123, "world");
	assert(n == strlen("Hello 123 world"));
	assert(strcmp(buf, "Hello 123 world") == 0);
	printf("test_printf_basic passed\n");
}

void test_printf_float(void) {
	char buf[128];
	mys_sprintf(buf, "%f", 3.14159);
	// We expect roughly 3.141590
	assert(strncmp(buf, "3.14159", 7) == 0);
	printf("test_printf_float passed\n");
}

void test_printf_snprintf(void) {
	char buf[8];
	int n = mys_snprintf(buf, sizeof(buf), "1234567890");
	assert(n == 10);
	assert(strlen(buf) == 7);
	assert(strcmp(buf, "1234567") == 0);
	printf("test_printf_snprintf passed\n");
}

int main(void) {
	printf("Running Substrate printf tests...\n");
	test_printf_basic();
	test_printf_float();
	test_printf_snprintf();
	printf("All printf tests passed!\n");
	return 0;
}
