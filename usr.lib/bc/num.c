/*
 * libbc - Bignum Library for bc/dc
 * num.c: Core management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "num.h"

bc_num *bc_new(void) {
    bc_num *n = malloc(sizeof(bc_num));
    n->digits = NULL;
    n->len = 0;
    n->scale = 0;
    n->sign = 0;
    n->cap = 0;
    return n;
}

void bc_free(bc_num *n) {
    if (!n) return;
    if (n->digits) free(n->digits);
    free(n);
}

// Ensure capacity for 'needed' digits (preserves existing)
void bc_expsize(bc_num *n, int needed) {
    if (needed <= n->cap) return;
    int new_cap = needed < 8 ? 8 : needed * 2;
    n->digits = realloc(n->digits, new_cap);
    memset(n->digits + n->cap, 0, new_cap - n->cap);
    n->cap = new_cap;
}

bc_num *bc_from_long(long long v) {
    bc_num *n = bc_new();
    if (v == 0) {
        n->sign = 0;
        return n;
    }
    
    if (v < 0) {
        n->sign = -1;
        v = -v;
    } else {
        n->sign = 1;
    }

    // Convert digits (base 100)
    // Max long long ~9e18 => 10 base-100 digits
    bc_expsize(n, 10);
    int i = 0;
    while (v > 0) {
        n->digits[i++] = v % 100;
        v /= 100;
    }
    n->len = i;
    return n;
}

void bc_print(bc_num *n) {
    if (!n) {
        printf("(null)");
        return;
    }
    if (n->sign == 0 || n->len == 0) {
        printf("0");
        return;
    }
    if (n->sign < 0) printf("-");

    // Print integer part
    // Scale handled later, for now assuming scale=0 for v0.1 tests
    // Digits are stored little-endian.
    // i goes from len-1 down to 0
    
    // For integer:
    int i = n->len - 1;
    printf("%d", n->digits[i]); // First digit (no zero pad)
    i--;
    for (; i >= 0; i--) {
        printf("%02d", n->digits[i]);
    }
}

bc_num *bc_dup(bc_num *src) {
    bc_num *n = bc_new();
    n->sign = src->sign;
    n->scale = src->scale;
    if (src->len > 0) {
        bc_expsize(n, src->len);
        memcpy(n->digits, src->digits, src->len);
        n->len = src->len;
    }
    return n;
}

// Helper: compare absolute values
// Returns 1 if |a| > |b|, -1 if |a| < |b|, 0 if equal
int bc_abs_cmp(bc_num *a, bc_num *b) {
    if (a->len > b->len) return 1;
    if (a->len < b->len) return -1;
    for (int i = a->len - 1; i >= 0; i--) {
        if (a->digits[i] > b->digits[i]) return 1;
        if (a->digits[i] < b->digits[i]) return -1;
    }
    return 0;
}


