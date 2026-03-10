#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

// Prefixed symbols
extern int mys_sscanf(const char *str, const char *format, ...);
extern int mys_vsscanf(const char *str, const char *format, va_list ap);

void test_scanf_int(void) {
	int a, b;
	int n = mys_sscanf("123 456", "%d %d", &a, &b);
	assert(n == 2);
	assert(a == 123);
	assert(b == 456);
	printf("test_scanf_int passed\n");
}

void test_scanf_hex(void) {
	unsigned int x;
	int n = mys_sscanf("0xdeadbeef", "%x", &x);
	assert(n == 1);
	assert(x == 0xdeadbeef);
	printf("test_scanf_hex passed\n");
}

void test_scanf_float(void) {
	float f;
	int n = mys_sscanf("3.14159", "%f", &f);
	assert(n == 1);
	assert(f > 3.14 && f < 3.15);
	printf("test_scanf_float passed\n");
}

void test_scanf_string(void) {
	char buf[32];
	int n = mys_sscanf("  Hello World  ", "%s", buf);
	assert(n == 1);
	assert(strcmp(buf, "Hello") == 0);
	printf("test_scanf_string passed\n");
}

void test_scanf_scanset(void) {
	char buf[32];
	int n = mys_sscanf("abc123def", "%[a-z]", buf);
	assert(n == 1);
	assert(strcmp(buf, "abc") == 0);
	printf("test_scanf_scanset passed\n");
}

int main(void) {
	printf("Running Substrate scanf tests...\n");
	test_scanf_int();
	test_scanf_hex();
	test_scanf_float();
	test_scanf_string();
	test_scanf_scanset();
	printf("All scanf tests passed!\n");
	return 0;
}
