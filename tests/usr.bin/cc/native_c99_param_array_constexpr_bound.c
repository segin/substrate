#include <stdio.h>
#include <string.h>

enum { ABFORMAT_SIZE = 128 };

static void fill(char abmon[12][ABFORMAT_SIZE]) {
	strcpy(abmon[6], "Jul");
}

int main(void) {
	char abmon[12][ABFORMAT_SIZE];

	memset(abmon, 0, sizeof(abmon));
	fill(abmon);
	puts(abmon[6]);
	return strcmp(abmon[6], "Jul") != 0;
}
