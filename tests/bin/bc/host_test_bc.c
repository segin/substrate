#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../../../usr.lib/bc/num.h"

/* arith.c declarations (no separate header) */
bc_num *bc_add(bc_num *a, bc_num *b);
bc_num *bc_sub(bc_num *a, bc_num *b);
bc_num *bc_mul(bc_num *a, bc_num *b);
bc_num *bc_div(bc_num *a, bc_num *b);
int     bc_compare(bc_num *a, bc_num *b);

static void test_bc_from_long(void) {
    bc_num *n = bc_from_long(42);
    assert(n != NULL);
    assert(bc_num_to_long(n) == 42);
    bc_free(n);

    bc_num *neg = bc_from_long(-7);
    assert(neg != NULL);
    assert(bc_num_to_long(neg) == -7);
    bc_free(neg);
}

static void test_bc_add(void) {
    bc_num *a = bc_from_long(10);
    bc_num *b = bc_from_long(5);
    bc_num *r = bc_add(a, b);
    assert(bc_num_to_long(r) == 15);
    bc_free(a); bc_free(b); bc_free(r);
}

static void test_bc_sub(void) {
    bc_num *a = bc_from_long(10);
    bc_num *b = bc_from_long(3);
    bc_num *r = bc_sub(a, b);
    assert(bc_num_to_long(r) == 7);
    bc_free(a); bc_free(b); bc_free(r);
}

static void test_bc_mul(void) {
    bc_num *a = bc_from_long(6);
    bc_num *b = bc_from_long(7);
    bc_num *r = bc_mul(a, b);
    assert(bc_num_to_long(r) == 42);
    bc_free(a); bc_free(b); bc_free(r);
}

static void test_bc_div(void) {
    bc_num *a = bc_from_long(20);
    bc_num *b = bc_from_long(4);
    bc_num *r = bc_div(a, b);
    assert(bc_num_to_long(r) == 5);
    bc_free(a); bc_free(b); bc_free(r);
}

static void test_bc_compare(void) {
    bc_num *a = bc_from_long(5);
    bc_num *b = bc_from_long(3);
    assert(bc_compare(a, b) > 0);
    assert(bc_compare(b, a) < 0);
    assert(bc_compare(a, a) == 0);
    bc_free(a); bc_free(b);
}

int main(void) {
    test_bc_from_long();
    test_bc_add();
    test_bc_sub();
    test_bc_mul();
    test_bc_div();
    test_bc_compare();
    printf("All bc tests passed!\n");
    return 0;
}
