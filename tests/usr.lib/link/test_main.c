#include <stdio.h>

int test_ln_basename_const(void);

int main(void) {
    int failures = 0;
    failures += test_ln_basename_const();

    if (failures) {
        fprintf(stderr, "liblink tests: %d failures\n", failures);
        return 1;
    }
    printf("liblink tests: ok\n");
    return 0;
}
