#include "cp_test.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; \
} } while (0)

int main(void)
{
    unsigned char a[8] = {0};
    unsigned char b[8] = {0, 0, 0, 1, 0, 0, 0, 0};

    CHECK(cp_test_buf_all_zero(a, sizeof(a)) == 1);
    CHECK(cp_test_buf_all_zero(b, sizeof(b)) == 0);
    CHECK(cp_test_buf_all_zero((const unsigned char *)"\0\0\0", 3) == 1);
    CHECK(cp_test_buf_all_zero((const unsigned char *)"A", 1) == 0);
    return 0;
}
