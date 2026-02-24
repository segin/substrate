#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "num.h"

int main() {
    // Test case 1: (-1)^-2 = 1
    bc_num *a = bc_from_long(-1);
    bc_num *b = bc_from_long(-2);

    printf("Testing (-1)^-2...\n");
    bc_num *res = bc_pow(a, b);

    // Expect 1
    if (res->sign == 1 && res->len == 1 && res->digits[0] == 1) {
        printf("PASS: (-1)^-2 = 1\n");
    } else {
        printf("FAIL: (-1)^-2 = ");
        if (res->sign == 0) printf("0\n");
        else printf("%d (len=%d)\n", res->sign * res->digits[0], res->len);
    }
    bc_free(res);
    bc_free(b);

    // Test case 2: (-1)^-3 = -1
    b = bc_from_long(-3);
    printf("Testing (-1)^-3...\n");
    res = bc_pow(a, b);

    // Expect -1
    if (res->sign == -1 && res->len == 1 && res->digits[0] == 1) {
        printf("PASS: (-1)^-3 = -1\n");
    } else {
        printf("FAIL: (-1)^-3 = ");
        if (res->sign == 0) printf("0\n");
        else printf("%d (len=%d)\n", res->sign * res->digits[0], res->len);
    }

    bc_free(res);
    bc_free(b);
    bc_free(a);

    // Test case 3: 1^-2 = 1
    a = bc_from_long(1);
    b = bc_from_long(-2);
    printf("Testing 1^-2...\n");
    res = bc_pow(a, b);

    // Expect 1
    if (res->sign == 1 && res->len == 1 && res->digits[0] == 1) {
        printf("PASS: 1^-2 = 1\n");
    } else {
        printf("FAIL: 1^-2 = ");
        if (res->sign == 0) printf("0\n");
        else printf("%d (len=%d)\n", res->sign * res->digits[0], res->len);
    }
    bc_free(res);
    bc_free(b);
    bc_free(a);

    return 0;
}
