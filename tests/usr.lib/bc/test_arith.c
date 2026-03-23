#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "num.h"

void test_bc_raw_add() {
    printf("Testing bc_raw_add...\n");

    // Case 1: 50 + 49 = 99 (No carry)
    bc_num *a = bc_from_long(50);
    bc_num *b = bc_from_long(49);
    bc_num *r = bc_new();
    bc_raw_add(r, a, b);

    assert(r->len == 1 && r->digits[0] == 99);
    printf("PASS: 50 + 49 = 99\n");
    bc_free(a); bc_free(b); bc_free(r);

    // Case 2: 50 + 50 = 100 (Carry, length increases)
    a = bc_from_long(50);
    b = bc_from_long(50);
    r = bc_new();
    bc_raw_add(r, a, b);

    assert(r->len == 2 && r->digits[0] == 0 && r->digits[1] == 1);
    printf("PASS: 50 + 50 = 100\n");
    bc_free(a); bc_free(b); bc_free(r);

    // Case 3: 0 + 0 = 0
    a = bc_from_long(0);
    b = bc_from_long(0);
    r = bc_new();
    bc_raw_add(r, a, b);

    assert(r->len == 0 || (r->len == 1 && r->digits[0] == 0));
    printf("PASS: 0 + 0 = 0\n");
    bc_free(a); bc_free(b); bc_free(r);

    // Case 4: Different lengths (1000 + 1 = 1001) -> [0, 10] + [1] = [1, 10]
    a = bc_from_long(1000);
    b = bc_from_long(1);
    r = bc_new();
    bc_raw_add(r, a, b);

    assert(r->len == 2 && r->digits[0] == 1 && r->digits[1] == 10);
    printf("PASS: 1000 + 1 = 1001\n");
    bc_free(a); bc_free(b); bc_free(r);

    // Case 5: Different lengths swapped (1 + 1000 = 1001)
    a = bc_from_long(1);
    b = bc_from_long(1000);
    r = bc_new();
    bc_raw_add(r, a, b);

    assert(r->len == 2 && r->digits[0] == 1 && r->digits[1] == 10);
    printf("PASS: 1 + 1000 = 1001\n");
    bc_free(a); bc_free(b); bc_free(r);

    // Case 6: Multiple carries (9999 + 1 = 10000) -> [99, 99] + [1] = [0, 0, 1]
    a = bc_from_long(9999);
    b = bc_from_long(1);
    r = bc_new();
    bc_raw_add(r, a, b);

    assert(r->len == 3 && r->digits[0] == 0 && r->digits[1] == 0 && r->digits[2] == 1);
    printf("PASS: 9999 + 1 = 10000\n");
    bc_free(a); bc_free(b); bc_free(r);
}

int main() {
    setbuf(stdout, NULL); // Disable buffering for correct ordering with stderr

    test_bc_raw_add();

    // Test case 1: (-1)^-2 = 1
    bc_num *a = bc_from_long(-1);
    bc_num *b = bc_from_long(-2);

    printf("Testing (-1)^-2...\n");
    bc_num *res = bc_pow(a, b);

    // Expect 1
    assert(res->sign == 1 && res->len == 1 && res->digits[0] == 1);
    printf("PASS: (-1)^-2 = 1\n");
    bc_free(res);
    bc_free(b);

    // Test case 2: (-1)^-3 = -1
    b = bc_from_long(-3);
    printf("Testing (-1)^-3...\n");
    res = bc_pow(a, b);

    // Expect -1
    assert(res->sign == -1 && res->len == 1 && res->digits[0] == 1);
    printf("PASS: (-1)^-3 = -1\n");

    bc_free(res);
    bc_free(b);
    bc_free(a);

    // Test case 3: 1^-2 = 1
    a = bc_from_long(1);
    b = bc_from_long(-2);
    printf("Testing 1^-2...\n");
    res = bc_pow(a, b);

    // Expect 1
    assert(res->sign == 1 && res->len == 1 && res->digits[0] == 1);
    printf("PASS: 1^-2 = 1\n");
    bc_free(res);
    bc_free(b);
    bc_free(a);

    // Test case 4: 0^-1 = 0 (and prints error)
    a = bc_from_long(0);
    b = bc_from_long(-1);
    printf("Testing 0^-1...\n");
    res = bc_pow(a, b);

    // Expect 0
    assert(res->sign == 0 && res->len == 0);
    printf("PASS: 0^-1 = 0 (error expected on stderr)\n");
    bc_free(res);
    bc_free(b);
    bc_free(a);

    // Test case 5: 2^-2 = 0
    a = bc_from_long(2);
    b = bc_from_long(-2);
    printf("Testing 2^-2...\n");
    res = bc_pow(a, b);

    // Expect 0
    assert(res->sign == 0 && res->len == 0);
    printf("PASS: 2^-2 = 0\n");
    bc_free(res);
    bc_free(b);
    bc_free(a);

    return 0;
}
