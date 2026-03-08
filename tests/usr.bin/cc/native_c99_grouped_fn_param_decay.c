#include <stddef.h>

static char *call_it(char *(func)(const char *, size_t *), const char *s, size_t *n) {
	return(func(s, n));
}

static char *id(const char *s, size_t *n) {
	(void)n;
	return((char *)s);
}

int main(void) {
	size_t n = 0;
	return(call_it(id, "x", &n)[0] != 'x');
}
