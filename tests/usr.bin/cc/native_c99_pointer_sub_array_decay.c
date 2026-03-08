#include <stddef.h>
#include <stdio.h>

int main(void) {
	char buf[23];
	char *bufp = buf + 19;
	char *end = buf + sizeof(buf) / sizeof(buf[0]);
	ptrdiff_t a = (buf + sizeof(buf) / sizeof(buf[0])) - bufp;
	ptrdiff_t b = end - bufp;

	printf("%ld %ld\n", (long)a, (long)b);
	return !((a == 4) && (b == 4));
}
