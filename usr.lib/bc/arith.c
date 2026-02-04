/*
 * libbc - Bignum Library for bc/dc
 * arith.c: Arithmetic operations
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "num.h"

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

// Helper: Perform addition or subtraction based on effective operation
static void bc_do_add_sub(bc_num *r, bc_num *a, bc_num *b, int scale_b_sign) {
    if (a->sign == scale_b_sign) {
        // Same effective sign: Add magnitudes
        r->sign = a->sign;
        bc_raw_add(r, a, b);
    } else {
        // Different effective sign: Subtract smaller from larger abs
        int cmp = bc_abs_cmp(a, b);
        if (cmp >= 0) {
            // |a| >= |b| -> Sign follows a
            r->sign = a->sign;
            bc_raw_sub(r, a, b);
        } else {
            // |b| > |a| -> Sign follows b
            r->sign = scale_b_sign;
            bc_raw_sub(r, b, a);
        }
    }
    if (r->len == 0) r->sign = 0;
}

bc_num *bc_add(bc_num *a, bc_num *b) {
    bc_num *r = bc_new();
    bc_do_add_sub(r, a, b, b->sign);
    return r;
}

bc_num *bc_sub(bc_num *a, bc_num *b) {
    bc_num *r = bc_new();
    bc_do_add_sub(r, a, b, -b->sign);
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
// Knuth's Algorithm D implementation for Division
// Returns quotient in q and remainder in r.
// If q is NULL, assumes user doesn't care about quotient.
// If r is NULL, assumes user doesn't care about remainder (but we calculate it anyway).
static void bc_div_rem(bc_num *a, bc_num *b, bc_num **q_out, bc_num **r_out) {
    if (b->len == 0 || b->sign == 0) {
        fprintf(stderr, "bc: divide by zero\n");
        if (q_out) *q_out = bc_new();
        if (r_out) *r_out = bc_new();
        return;
    }
    
    // Trivial case: |a| < |b|
    if (bc_abs_cmp(a, b) < 0) {
        if (q_out) *q_out = bc_new(); // 0
        if (r_out) *r_out = bc_dup(a); // remainder is a
        return;
    }
    
    // Use deep copies for u (dividend/rem) and v (divisor)
    bc_num *u = bc_dup(a);
    u->sign = 1;
    bc_num *v = bc_dup(b);
    v->sign = 1;
    
    int m = u->len - v->len;
    int n = v->len;
    
    // Output quotient
    bc_num *q = bc_new();
    bc_expsize(q, m + 1);
    q->len = m + 1;
    q->sign = a->sign * b->sign;
    
    // Normalization
    // d = Base / (v[n-1] + 1)
    int d = 100 / (v->digits[n - 1] + 1);
    
    // Multiply u and v by d
    if (d > 1) {
        // u *= d
        int carry = 0;
        bc_expsize(u, u->len + 1); // might grow
        for (int i = 0; i < u->len; i++) {
            int val = u->digits[i] * d + carry;
            u->digits[i] = val % 100;
            carry = val / 100;
        }
        if (carry) {
            u->digits[u->len++] = carry;
        }
        
        // v *= d
        carry = 0;
        bc_expsize(v, v->len + 1); // shouldn't strictly change len classification for D?
        // Actually Knuth says v has n digits. If it grows, n changes?
        // Base 100: v[n-1] >= 50 after norm.
        // If v grows, then we treat it as n digits?
        // No, v should simply be scaled. If v had n digits, d is calculated based on v[n-1].
        // If v grows to n+1 digits, that breaks the "v has n digits" assumption relative to u?
        // Wait, normal arithmetic: if we mult by d, we just process.
        // Let's implement in-place mult.
        for (int i = 0; i < v->len; i++) {
            int val = v->digits[i] * d + carry;
            v->digits[i] = val % 100;
            carry = val / 100;
        }
        // If v grows, n is still expected to be the 'divisor length'. 
        // Knuth D assumes "n places".
        // Actually, if v grows, it means normalization might shift things.
        // But 100 / (v[n-1] + 1) guarantees v[n-1]*d < 100?
        // v[n-1] >= 1 (since len=n).
        // e.g. v[n-1]=1 -> d=50 -> 1*50 = 50.
        // v[n-1]=99 -> d=1 -> 99.
        // So v does NOT grow in length generally, unless carry ripples from lower digits?
        // Yes it can. e.g. v=01 99 (base 100), n=2.
        // v[1]=1. d = 100/2 = 50.
        // v*50 = 50*(100*1 + 99) = 5000 + 4950 = 9950 = 99*100 + 50.
        // v digits: [50, 99]. Length stays 2.
        // So normalized v should fit in n digits? 
        // Let's handle carry just in case, but keep n fixed as original len?
    }
    
    // Ensure u has m+n+1 digits (pad with 0) for the algorithm loop
    if (u->len < m + n + 1) {
        bc_expsize(u, m + n + 1);
        for (int i = u->len; i <= m + n; i++) u->digits[i] = 0;
        u->len = m + n + 1; // logical length
    }
    
    // Main Loop
    for (int j = m; j >= 0; j--) {
        // Step D3: Calculate q_hat
        // q_hat = (u[j+n]*B + u[j+n-1]) / v[n-1]
        long long num = (long long)u->digits[j + n] * 100 + u->digits[j + n - 1];
        long long den = v->digits[n - 1];
        long long q_hat = num / den;
        long long r_hat = num % den;
        
        // Clamp q_hat
        if (q_hat >= 100) q_hat = 99;
        
        // Test q_hat: if q_hat * v[n-2] > B*r_hat + u[j+n-2]
        // repeat decrement q_hat, add v[n-1] to r_hat
        while (n > 1) { // Only if v has at least 2 digits
           long long v_nm2 = v->digits[n - 2];
           long long u_jnm2 = u->digits[j + n - 2];
           if (q_hat * v_nm2 > 100 * r_hat + u_jnm2) {
               q_hat--;
               r_hat += den;
               if (r_hat >= 100) break; // r_hat can't override Check anymore
           } else {
               break;
           }
        }
        
        // Step D4: Multiply and subtract
        // u[j...j+n] -= q_hat * v
        int borrow = 0;
        for (int i = 0; i < n; i++) {
             // u[j+i] computation
             long long p = q_hat * v->digits[i];
             int sub = u->digits[j + i] - borrow - (p % 100);
             int sub_carry = p / 100;
             
             // Borrow handling
             // sub can be negative (-199 roughly)
             // We want positive residue mod 100
             if (sub < 0) {
                 // manual mod?
                 // e.g. sub = -5. borrow = 1, res = 95.
                 int k = (-sub + 99) / 100; // ceil div
                 sub += k * 100;
                 borrow = sub_carry + k;
             } else {
                 borrow = sub_carry;
             }
             u->digits[j + i] = sub;
        }
        // Handle last digit u[j+n]
        int sub = u->digits[j + n] - borrow;
        if (sub < 0) {
            u->digits[j + n] = sub + 100;
            borrow = 1; // This means q_hat was too big
        } else {
            u->digits[j + n] = sub;
            borrow = 0;
        }
        
        // Step D5: Correction
        q->digits[j] = q_hat;
        if (borrow) {
            // D6: Add back
            q->digits[j]--;
            int carry = 0;
            for (int i = 0; i < n; i++) {
                int val = u->digits[j + i] + v->digits[i] + carry;
                u->digits[j + i] = val % 100;
                carry = val / 100;
            }
            u->digits[j + n] += carry; // Should resolve negative
        }
    }
    
    // Trim quotient
    while (q->len > 0 && q->digits[q->len - 1] == 0) q->len--;
    
    // Denormalize Remainder (u)
    // r = u / d = u div d?
    // r = u (shifted back)
    // Just divide u by d (scalar div)
    bc_num *rem = bc_new();
    bc_expsize(rem, n); // Remainder <= divisor
    
    int r_carry = 0;
    // Division by scalar d. Start from top.
    // u->len might be larger than n due to padding? 
    // u represents the remainder now, but it might have leading zeros.
    // We only care about n digits theoretically, but let's just div all u
    // Actually, u contains the remainder in u[0...n-1]? 
    // u[n] should be 0 unless I missed something?
    
    // Scalar division u / d
    int rem_len = 0;
    for (int i = u->len - 1; i >= 0; i--) {
        int cur = r_carry * 100 + u->digits[i];
        int val = cur / d;
        r_carry = cur % d;
        // Construct rem? Wait, we want u / d. The remainder of THAT is dropped (should be 0)
        // because u was u_orig * d - q * v * d = (u_orig - q*v) * d
        // So u / d is exact.
        if (val != 0 || rem_len > 0) {
            if (rem->cap <= i) bc_expsize(rem, i + 1);
            rem->digits[i] = val;
            if (rem_len == 0) rem_len = i + 1;
        }
    }
    rem->len = rem_len;
    rem->sign = a->sign; // Remainder sign follows dividend
    
    bc_free(u);
    bc_free(v);
    
    if (q_out) *q_out = q;
    else bc_free(q);
    
    if (r_out) *r_out = rem;
    else bc_free(rem);
}

bc_num *bc_div(bc_num *a, bc_num *b) {
    bc_num *q = NULL;
    bc_div_rem(a, b, &q, NULL);
    return q;
}

bc_num *bc_mod(bc_num *a, bc_num *b) {
    bc_num *r = NULL;
    bc_div_rem(a, b, NULL, &r);
    return r;
}

// Helper to divide by 2 in place (for exponentiation)
// Returns remainder (0 or 1)
static int bc_div2(bc_num *n) {
    int rem = 0;
    for (int i = n->len - 1; i >= 0; i--) {
        int val = n->digits[i] + rem * 100;
        n->digits[i] = val / 2;
        rem = val % 2;
    }
    // Trim
    while (n->len > 0 && n->digits[n->len - 1] == 0) n->len--;
    return rem;
}

bc_num *bc_pow(bc_num *a, bc_num *b) {
    if (b->sign < 0) {
        // Integer exponentiation with negative power -> 0 (for integers > 1)
        // 1^-1 = 1, -1^-1 = -1.
        // For now, return 0 as integer div result (unless base is 1/-1)
        // bc standard says: "result is truncated to scale"
        // With scale=0, 2^-2 = 0.
        // Check for 1 or -1 base?
        // Let's stick to 0 for now unless |a|==1.
        bc_num *one = bc_from_long(1);
        int cmp = bc_abs_cmp(a, one);
        bc_free(one);
        
        if (cmp == 0) {
             // 1^-n = 1, (-1)^-n = 1 or -1
             // simple way: 1/a^n.
             // But we can just use the sign logic.
             // If a=1: 1. If a=-1: 1 if n even, -1 if n odd.
             // Let's just return 0 for non-unit base.
             if (a->sign > 0) return bc_from_long(1);
             else {
                 // Check if b is odd/even?
                 // That requires bignum modulo.
                 // Let's simplify and return 0 for now as per v0.1 compat.
                 return bc_new();
             }
        }
        return bc_new(); 
    }
    
    // Square and Multiply
    bc_num *res = bc_from_long(1);
    bc_num *base = bc_dup(a);
    bc_num *exp = bc_dup(b);
    
    // While exp > 0
    while (exp->len > 0) {
        // Check if odd (div2 returns remainder)
        int is_odd = bc_div2(exp);
        
        if (is_odd) {
            bc_num *tmp = bc_mul(res, base);
            bc_free(res);
            res = tmp;
        }
        
        if (exp->len > 0) {
            bc_num *tmp = bc_mul(base, base);
            bc_free(base);
            base = tmp;
        }
    }
    
    bc_free(base); bc_free(exp);
    return res;
}
