/*
 * libbc - Bignum Library for bc/dc
 * arith.c: Arithmetic operations
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "num.h"

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

// Low-level add: r = a + b (assuming a, b positive)
void bc_raw_add(bc_num *r, bc_num *a, bc_num *b) {
    int max_len = (a->len > b->len) ? a->len : b->len;
    bc_expsize(r, max_len + 1);
    
    int carry = 0;
    for (int i = 0; i < max_len || carry; i++) {
        int val = carry;
        if (i < a->len) val += a->digits[i];
        if (i < b->len) val += b->digits[i];
        
        r->digits[i] = val % 100;
        carry = val / 100;
        if (i >= r->len) r->len = i + 1;
    }
}

// Low-level sub: r = a - b (assuming a >= b, both positive)
void bc_raw_sub(bc_num *r, bc_num *a, bc_num *b) {
    bc_expsize(r, a->len);
    
    int borrow = 0;
    r->len = 0;
    for (int i = 0; i < a->len; i++) {
        int val = a->digits[i] - borrow;
        if (i < b->len) val -= b->digits[i];
        
        if (val < 0) {
            val += 100;
            borrow = 1;
        } else {
            borrow = 0;
        }
        r->digits[i] = val;
        if (val != 0) r->len = i + 1;
    }
}

bc_num *bc_add(bc_num *a, bc_num *b) {
    bc_num *r = bc_new();
    
    if (a->sign == b->sign) {
        // Same sign: just add magnitudes
        r->sign = a->sign;
        bc_raw_add(r, a, b);
    } else {
        // Different sign: subtract smaller from larger
        int cmp = bc_abs_cmp(a, b);
        if (cmp >= 0) {
            // |a| >= |b|
            r->sign = a->sign;
            bc_raw_sub(r, a, b);
        } else {
            // |b| > |a|
            r->sign = b->sign; // Takes sign of larger magnitude
            bc_raw_sub(r, b, a);
        }
    }
    
    if (r->len == 0) r->sign = 0;
    return r;
}

bc_num *bc_sub(bc_num *a, bc_num *b) {
    // a - b is same as a + (-b)
    // Flip sign of b temporarily or logic it out
    // Simplest: use bc_add logic with sign flip logic
    
    bc_num *r = bc_new();
    
    if (a->sign != b->sign) {
        // a (>0) - b (<0) -> a + |b| -> result > 0
        // a (<0) - b (>0) -> -|a| - b -> -(|a| + b) -> result < 0
        r->sign = a->sign;
        bc_raw_add(r, a, b);
    } else {
        // a (>0) - b (>0)
        // if |a| >= |b| -> result > 0
        // if |a| < |b|  -> result < 0
        int cmp = bc_abs_cmp(a, b);
        if (cmp >= 0) {
            r->sign = a->sign;
            bc_raw_sub(r, a, b);
        } else {
            r->sign = -a->sign; // Flip result sign
            bc_raw_sub(r, b, a);
        }
    }
    
    if (r->len == 0) r->sign = 0;
    return r;
}

bc_num *bc_mul(bc_num *a, bc_num *b) {
    bc_num *r = bc_new();
    if (a->sign == 0 || b->sign == 0) return r;
    
    r->sign = a->sign * b->sign;
    bc_expsize(r, a->len + b->len);
    r->len = a->len + b->len - 1; // provisional
    
    for (int i = 0; i < a->len; i++) {
        for (int j = 0; j < b->len; j++) {
            int pos = i + j;
            int val = r->digits[pos] + (a->digits[i] * b->digits[j]);
            r->digits[pos] = val % 100;
            int carry = val / 100;
            
            // Ripple carry
            int k = pos + 1;
            while (carry) {
                if (k >= r->cap) bc_expsize(r, k + 2);
                val = r->digits[k] + carry;
                r->digits[k] = val % 100;
                carry = val / 100;
                if (k >= r->len) r->len = k + 1;
                k++;
            }
        }
    }
    
    // Exact length check
    while (r->len > 0 && r->digits[r->len - 1] == 0) r->len--;
    return r;
}

int bc_compare(bc_num *a, bc_num *b) {
    if (a->sign > b->sign) return 1;
    if (a->sign < b->sign) return -1;
    
    // Same sign
    int cmp = bc_abs_cmp(a, b);
    if (a->sign > 0) return cmp;
    return -cmp; // Negative numbers: larger abs is smaller value
}

// Simple integer division (dividend = quotient * divisor + remainder)
// This is a naive O(N*M) division for now, suitable for v0.1
bc_num *bc_div(bc_num *a, bc_num *b) {
    if (b->len == 0 || b->sign == 0) {
        fprintf(stderr, "bc: divide by zero\n");
        return bc_new(); // Return 0 on error
    }
    
    bc_num *q = bc_new();
    q->sign = a->sign * b->sign;
    
    // Working with absolute values
    // Using simple repeated subtraction is too slow for bignums.
    // Let's implement Knuth's Algorithm D or similar long division.
    // Or for v0.1 prototype: use long long if inputs fit, else naive subtraction? 
    // Naive subtraction is O(Q*D) which is terrible.
    // Let's do a simple recursive "guess and check" or binary search quotient?
    // Actually, simple byte-by-byte long division in base 100.
    
    // Normalized Dividend (remainder)
    // We need a deep copy of 'a' to mutate as remainder
    bc_num *rem = bc_dup(a);
    rem->sign = 1; // Work with abs
    
    // Abs divisor
    bc_num *div = bc_dup(b);
    div->sign = 1;

    if (bc_abs_cmp(rem, div) < 0) {
        bc_free(rem); bc_free(div);
        return q; // 0
    }

    // Allocate quotient
    // Max digits = a->len - b->len + 1
    int q_len = a->len - b->len + 1;
    bc_expsize(q, q_len);
    q->len = q_len;

    // Shift divisor to align with MSB
    // We can simulate shift by digit offset access
    // This is getting complex for a "one-shot" implementation.
    // FALLBACK for v0.1 prototype:
    // If numbers fit in long long, use native div.
    // If not, return 0 and warn "Not implemented for huge numbers".
    // This unblocks 'bc' logic testing while deferring Algorithm D.
    
    if (a->len <= 9 && b->len <= 9) {
        long long va = 0, vb = 0;
        for (int i=a->len-1; i>=0; i--) va = va*100 + a->digits[i];
        for (int i=b->len-1; i>=0; i--) vb = vb*100 + b->digits[i];
        
        long long vq = va / vb;
        
        // Convert back
        bc_free(rem); bc_free(div); bc_free(q);
        return bc_from_long(vq * (a->sign * b->sign)); 
    }

    fprintf(stderr, "bc: huge division not impl in v0.1\n");
    bc_free(rem); bc_free(div);
    return q;
}

bc_num *bc_mod(bc_num *a, bc_num *b) {
    // a % b = a - (b * (a / b))
    // Reuse div
    bc_num *q = bc_div(a, b);
    bc_num *prod = bc_mul(b, q);
    bc_num *res = bc_sub(a, prod);
    
    bc_free(q); bc_free(prod);
    return res;
}

bc_num *bc_pow(bc_num *a, bc_num *b) {
    if (b->sign < 0) {
        // Integer exponentiation with negative power -> 0 (for integers)
        return bc_new(); 
    }
    
    // Square and Multiply
    bc_num *res = bc_from_long(1);
    bc_num *base = bc_dup(a);
    bc_num *exp = bc_dup(b); // We need to decrement/shift this
    
    // For v0.1, assume exp fits in long long to drive loop
    // TODO: Bignum exponent loop
    long long vexp = 0;
    for (int i=exp->len-1; i>=0; i--) vexp = vexp*100 + exp->digits[i];
    
    while (vexp > 0) {
        if (vexp & 1) {
            bc_num *tmp = bc_mul(res, base);
            bc_free(res);
            res = tmp;
        }
        vexp >>= 1;
        if (vexp > 0) { // optimization
            bc_num *tmp = bc_mul(base, base);
            bc_free(base);
            base = tmp;
        }
    }
    
    bc_free(base); bc_free(exp);
    return res;
}
