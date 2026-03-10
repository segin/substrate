#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>

extern int mys_sscanf(const char *str, const char *format, ...);
extern void mys___stdio_init(void);

void fuzz_scanf_int(void) {
	char buf[32];
	int val;
	for (int i = 0; i < 1000; i++) {
		// Generate random numeric string
		int r = rand() % 2000 - 1000;
		sprintf(buf, "%d", r);
		int n = mys_sscanf(buf, "%d", &val);
		if (n == 1) {
			// Should match if it parsed
			assert(val == r);
		}
	}
}

void fuzz_scanf_random_input(void) {
	char buf[256];
	char fmt[32] = "%s";
	char out[256];
	for (int i = 0; i < 1000; i++) {
		// Fill buf with random junk
		for (int j = 0; j < 255; j++) buf[j] = (rand() % 255) + 1;
		buf[255] = '\0';
		
		// Just make sure it doesn't crash
		mys_sscanf(buf, fmt, out);
	}
}

int main(void) {
	srand(time(NULL));
	mys___stdio_init();
	printf("Running Substrate scanf fuzz tests...\n");
	fuzz_scanf_int();
	fuzz_scanf_random_input();
	printf("All scanf fuzz tests passed (no crashes)!\n");
	return 0;
}
