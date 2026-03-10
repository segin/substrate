#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>

extern int mys_sprintf(char *str, const char *format, ...);
extern int mys_sscanf(const char *str, const char *format, ...);
extern void mys___stdio_init(void);

void test_prop_int_roundtrip(void) {
	char buf[64];
	for (int i = -1000; i < 1000; i++) {
		mys_sprintf(buf, "%d", i);
		int val;
		int n = mys_sscanf(buf, "%d", &val);
		assert(n == 1);
		assert(val == i);
	}
	printf("test_prop_int_roundtrip passed\n");
}

void test_prop_uint_roundtrip(void) {
	char buf[64];
	for (unsigned int i = 0; i < 2000; i++) {
		mys_sprintf(buf, "%u", i);
		unsigned int val;
		int n = mys_sscanf(buf, "%u", &val);
		assert(n == 1);
		assert(val == i);
	}
	printf("test_prop_uint_roundtrip passed\n");
}

void test_prop_float_roundtrip(void) {
	char buf[64];
	float floats[] = { 3.14159f, 0.0f, -1.0f, 123.456f, 0.001f, 1e5f };
	for (int i = 0; i < 6; i++) {
		mys_sprintf(buf, "%f", floats[i]);
		float val;
		int n = mys_sscanf(buf, "%f", &val);
		assert(n == 1);
		float diff = val - floats[i];
		if (diff < 0) diff = -diff;
		assert(diff < 0.001f);
	}
	printf("test_prop_float_roundtrip passed\n");
}

int main(void) {
	mys___stdio_init();
	printf("Running Substrate stdio property tests...\n");
	test_prop_int_roundtrip();
	test_prop_uint_roundtrip();
	test_prop_float_roundtrip();
	printf("All property tests passed!\n");
	return 0;
}
