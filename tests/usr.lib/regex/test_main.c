#include <stdio.h>

int test_api_basic(void);
int test_api_match(void);
int test_replace_basic(void);
int test_dos_limits(void);
int test_streaming_basic(void);
int test_utf8_literal(void);
int test_unicode_case(void);
int test_ascii_toupper(void);

int main(void) {
    int failures = 0;
    failures += test_api_basic();
    failures += test_api_match();
    failures += test_replace_basic();
    failures += test_dos_limits();
    failures += test_streaming_basic();
    failures += test_utf8_literal();
    failures += test_unicode_case();
    failures += test_unicode_case();
    failures += test_ascii_toupper();

    if (failures) {
        fprintf(stderr, "regex tests: %d failures\n", failures);
        return 1;
    }
    printf("regex tests: ok\n");
    return 0;
}
