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

bc_num *bc_from_string(const char *s) {
    bc_num *n = bc_new();
    if (!s || !*s) return n; // Zero

    int sign = 1;
    while (isspace(*s)) s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    // Buffer for digits
    size_t slen = strlen(s);
    unsigned char *buf = malloc(slen + 1);
    int blen = 0;
    int scale_cnt = 0;
    int point_seen = 0;

    while (*s) {
        if (isdigit(*s)) {
            buf[blen++] = *s - '0';
            if (point_seen) scale_cnt++;
        } else if (*s == '.') {
            if (point_seen) break;
            point_seen = 1;
        } else {
            break;
        }
        s++;
    }

    // Strip leading zeros
    int start = 0;
    while (start < blen && buf[start] == 0) {
        start++;
    }

    if (start == blen) {
        // Zero
        free(buf);
        return n; // 0
    }

    n->sign = sign;
    n->scale = scale_cnt; // Not really used yet for arith but stored

    // Number of decimal digits
    int num_digits = blen - start;

    // Base 100 digits needed: ceil(num_digits / 2)
    int n_len = (num_digits + 1) / 2;
    bc_expsize(n, n_len);
    n->len = n_len;

    // Convert
    // Iterate from end (least significant)
    // i goes from 0 to n_len-1
    // digit[i] corresponds to buf[end - 1 - 2*i] and buf[end - 2 - 2*i]
    // where end = blen

    for (int i = 0; i < n_len; i++) {
        int low_idx = blen - 1 - (2 * i);
        int val = buf[low_idx];

        int high_idx = low_idx - 1;
        if (high_idx >= start) {
            val += buf[high_idx] * 10;
        }
        n->digits[i] = val;
    }

    free(buf);
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
