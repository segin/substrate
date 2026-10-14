#include <stdio.h>
#include <string.h>

static void fill(char abmon[12][128]) {
	strcpy(abmon[6], "Jul");
}

int main(void) {
	char abmon[12][128];

	memset(abmon, 0, sizeof(abmon));
	fill(abmon);
	puts(abmon[6]);
	return strcmp(abmon[6], "Jul") != 0;
}
