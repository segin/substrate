#include <stdarg.h>

int sum(int n, ...) {
	va_list ap;
	int i;
	int s = 0;
	va_start(ap, n);
	for (i = 0; i < n; ++i) {
		s += va_arg(ap, int);
	}
	va_end(ap);
	return s;
}

int main(void) {
	return sum(3, 3, 4, 5) == 12 ? 0 : 1;
}
