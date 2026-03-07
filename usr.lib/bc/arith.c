/*
 * libbc - Bignum Library for bc/dc
 * arith.c: Arithmetic operations
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "num.h"

// Math function constants and utilities will go here.
// For now, full POSIX scale alignment.

static void shift_scale(bc_num *n, int target_scale) {
    if (n->scale == target_scale) return;
    int diff = target_scale - n->scale;
    
    // Multiply n by 10^diff.
    // Base-100 shift:
    int b100_shift = diff / 2;
    int b10_rem = diff % 2;
    
    if (b100_shift > 0) {
        bc_expsize(n, n->len + b100_shift + 1);
        memmove(n->digits + b100_shift, n->digits, n->len);
        memset(n->digits, 0, b100_shift);
        n->len += b100_shift;
    }
    
    if (b10_rem > 0) {
        // Multiply by 10
        int carry = 0;
        bc_expsize(n, n->len + 1);
        for (int i = 0; i < n->len; i++) {
            int val = n->digits[i] * 10 + carry;
            n->digits[i] = val % 100;
            carry = val / 100;
        }
        if (carry) n->digits[n->len++] = carry;
    }
    n->scale = target_scale;
}

// Align a and b to the same scale, padding with 0 digits.
bc_num *bc_align_scale(bc_num *a, bc_num *b, bc_num **a_aligned, bc_num **b_aligned) {
    int max_scale = a->scale > b->scale ? a->scale : b->scale;
    
    *a_aligned = bc_dup(a);
    *b_aligned = bc_dup(b);
    
    shift_scale(*a_aligned, max_scale);
    shift_scale(*b_aligned, max_scale);
    
    return NULL; // Void really
}

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
    bc_num *aa, *bb;
    bc_align_scale(a, b, &aa, &bb);
    r->scale = aa->scale;
    
    if (aa->sign == bb->sign) {
        r->sign = aa->sign;
        bc_raw_add(r, aa, bb);
    } else {
        int cmp = bc_abs_cmp(aa, bb);
        if (cmp >= 0) {
            r->sign = aa->sign;
            bc_raw_sub(r, aa, bb);
        } else {
            r->sign = bb->sign;
            bc_raw_sub(r, bb, aa);
        }
    }
    
    if (r->len == 0) r->sign = 0;
    bc_free(aa); bc_free(bb);
    return r;
}

bc_num *bc_sub(bc_num *a, bc_num *b) {
    bc_num *b_neg = bc_dup(b);
    b_neg->sign = -b_neg->sign;
    bc_num *r = bc_add(a, b_neg);
    bc_free(b_neg);
    return r;
}

bc_num *bc_mul(bc_num *a, bc_num *b) {
    bc_num *r = bc_new();
    if (bc_is_zero(a) || bc_is_zero(b)) {
        // POSIX: scale of result is min(a.scale+b.scale, max(scale, a.scale, b.scale))
        int s = a->scale + b->scale;
        int max_s = bc_scale > a->scale ? bc_scale : a->scale;
        if (b->scale > max_s) max_s = b->scale;
        r->scale = s < max_s ? s : max_s;
        return r;
    }
    
    r->sign = a->sign * b->sign;
    bc_expsize(r, a->len + b->len);
    r->len = a->len + b->len;
    
    for (int i = 0; i < a->len; i++) {
        int carry = 0;
        for (int j = 0; j < b->len; j++) {
            int pos = i + j;
            int val = r->digits[pos] + (a->digits[i] * b->digits[j]) + carry;
            r->digits[pos] = val % 100;
            carry = val / 100;
        }
        r->digits[i + b->len] += carry;
    }
    
    bc_trim(r);
    
    // Scale handling
    int raw_scale = a->scale + b->scale;
    int max_s = bc_scale > a->scale ? bc_scale : a->scale;
    if (b->scale > max_s) max_s = b->scale;
    int target_scale = raw_scale < max_s ? raw_scale : max_s;
    
    if (raw_scale > target_scale) {
        // Truncate bottom digits (right shift)
        int diff = raw_scale - target_scale;
        int b100_shift = diff / 2;
        int b10_rem = diff % 2;
        
        if (b100_shift > 0) {
            if (b100_shift >= r->len) {
                r->len = 0;
                r->sign = 0;
                r->scale = target_scale;
                return r;
            }
            memmove(r->digits, r->digits + b100_shift, r->len - b100_shift);
            r->len -= b100_shift;
        }
        
        if (b10_rem > 0) {
            int rem = 0;
            for (int i = r->len - 1; i >= 0; i--) {
                int val = r->digits[i] + rem * 100;
                r->digits[i] = val / 10;
                rem = val % 10;
            }
            bc_trim(r);
        }
    }
    
    r->scale = target_scale;
    return r;
}

// Full Knuth D Division with Scale.
static void bc_div_rem(bc_num *a, bc_num *b, bc_num **q_out, bc_num **r_out, int target_scale) {
    if (bc_is_zero(b)) {
        bc_error("divide by zero");
        if (q_out) *q_out = bc_new();
        if (r_out) *r_out = bc_new();
        return;
    }
    
    // To divide to `target_scale` decimal places, we can shift dividend a
    // by target_scale + b->scale - a->scale.
    // e.g. a=1 (s=0), b=3 (s=0), ts=5. Shift a by 5. a=100000. 100000/3 = 33333.
    int shift = target_scale + b->scale - a->scale;
    bc_num *u = bc_dup(a);
    u->sign = 1;
    
    if (shift > 0) {
        int b100 = shift / 2;
        int b10 = shift % 2;
        if (b100 > 0) {
            bc_expsize(u, u->len + b100 + 1);
            memmove(u->digits + b100, u->digits, u->len);
            memset(u->digits, 0, b100);
            u->len += b100;
        }
        if (b10 > 0) {
            int carry = 0;
            bc_expsize(u, u->len + 1);
            for (int i = 0; i < u->len; i++) {
                int val = u->digits[i] * 10 + carry;
                u->digits[i] = val % 100;
                carry = val / 100;
            }
            if (carry) u->digits[u->len++] = carry;
        }
    } else if (shift < 0) {
        // Truncate? Division might just need padded 'u' for extra precision, 
        // if shift < 0, a has MORE precision than we want printed, but we can't just truncate dividend 
        // without losing division accuracy. Wait, if shift is negative, it means a->scale is huge.
        // Actually for a / b, output scale is target_scale.
        // The exact quotient is a.val / b.val * 10^(b.scale - a.scale).
        // If we want it at target_scale, we compute (a.val * 10^(target_scale + b.scale - a.scale)) / b.val.
        // If shift < 0, we must right-shift `a` BEFORE division. 
        // However, standard bc division is truncating.
        int shr = -shift;
        int b100 = shr / 2;
        int b10 = shr % 2;
        if (b100 > 0) {
            if (b100 >= u->len) u->len = 0;
            else {
                memmove(u->digits, u->digits + b100, u->len - b100);
                u->len -= b100;
            }
        }
        if (b10 > 0) {
            int rem = 0;
            for (int i = u->len - 1; i >= 0; i--) {
                int val = u->digits[i] + rem * 100;
                u->digits[i] = val / 10;
                rem = val % 10;
            }
            bc_trim(u);
        }
    }
    
    bc_num *v = bc_dup(b);
    v->sign = 1;
    
    // Same Knuth D as before...
    bc_num *q = bc_new();
    
    if (bc_abs_cmp(u, v) < 0) {
        q->scale = target_scale;
        if (q_out) *q_out = q; else bc_free(q);
        if (r_out) *r_out = bc_dup(a); // Modulo remainder rule? POSIX: a - (a/b)*b
        bc_free(u); bc_free(v);
        return;
    }
    
    int m = u->len - v->len;
    int n = v->len;
    
    bc_expsize(q, m + 1);
    q->len = m + 1;
    q->sign = a->sign * b->sign;
    
    int d = 100 / (v->digits[n - 1] + 1);
    
    if (d > 1) {
        int carry = 0;
        bc_expsize(u, u->len + 1);
        for (int i = 0; i < u->len; i++) {
            int val = u->digits[i] * d + carry;
            u->digits[i] = val % 100;
            carry = val / 100;
        }
        if (carry) u->digits[u->len++] = carry;
        
        carry = 0;
        for (int i = 0; i < v->len; i++) {
            int val = v->digits[i] * d + carry;
            v->digits[i] = val % 100;
            carry = val / 100;
        }
    }
    
    if (u->len < m + n + 1) {
        bc_expsize(u, m + n + 1);
        for (int i = u->len; i <= m + n; i++) u->digits[i] = 0;
        u->len = m + n + 1;
    }
    
    for (int j = m; j >= 0; j--) {
        long long num = (long long)u->digits[j + n] * 100 + u->digits[j + n - 1];
        long long den = v->digits[n - 1];
        long long q_hat = num / den;
        long long r_hat = num % den;
        
        if (q_hat >= 100) q_hat = 99;
        
        while (n > 1) {
           long long v_nm2 = v->digits[n - 2];
           long long u_jnm2 = u->digits[j + n - 2];
           if (q_hat * v_nm2 > 100 * r_hat + u_jnm2) {
               q_hat--;
               r_hat += den;
               if (r_hat >= 100) break;
           } else break;
        }
        
        int borrow = 0;
        for (int i = 0; i < n; i++) {
             long long p = q_hat * v->digits[i];
             int sub = u->digits[j + i] - borrow - (p % 100);
             int sub_carry = p / 100;
             if (sub < 0) {
                 int k = (-sub + 99) / 100;
                 sub += k * 100;
                 borrow = sub_carry + k;
             } else borrow = sub_carry;
             u->digits[j + i] = sub;
        }
        int sub = u->digits[j + n] - borrow;
        if (sub < 0) {
            u->digits[j + n] = sub + 100;
            borrow = 1;
        } else {
            u->digits[j + n] = sub;
            borrow = 0;
        }
        
        q->digits[j] = q_hat;
        if (borrow) {
            q->digits[j]--;
            int carry = 0;
            for (int i = 0; i < n; i++) {
                int val = u->digits[j + i] + v->digits[i] + carry;
                u->digits[j + i] = val % 100;
                carry = val / 100;
            }
            u->digits[j + n] += carry;
        }
    }
    
    bc_trim(q);
    q->scale = target_scale;
    
    // Modulo remainder: posix a - (a/b)*b 
    // calculated at max scale
    if (r_out) {
        bc_num *tmp_q = bc_dup(q);
        bc_num *tmp_mul = bc_mul(tmp_q, b);
        *r_out = bc_sub(a, tmp_mul);
        bc_free(tmp_q); bc_free(tmp_mul);
    }
    
    bc_free(u); bc_free(v);
    if (q_out) *q_out = q; else bc_free(q);
}

bc_num *bc_div(bc_num *a, bc_num *b) {
    bc_num *q = NULL;
    bc_div_rem(a, b, &q, NULL, bc_scale);
    return q;
}

bc_num *bc_mod(bc_num *a, bc_num *b) {
    bc_num *r = NULL;
    // Modulo output scale is max(scale, a.scale, b.scale)
    int s = bc_scale > a->scale ? bc_scale : a->scale;
    if (b->scale > s) s = b->scale;
    
    // We pass division scale? Actually POSIX mod uses division at scale `bc_scale`.
    bc_div_rem(a, b, NULL, &r, bc_scale);
    r->scale = s;
    return r;
}

bc_num *bc_pow(bc_num *a, bc_num *b) {
    if (bc_is_neg(b)) {
        if (bc_is_zero(a)) {
            bc_error("divide by zero");
            return bc_new();
        }
        // Negative exponent: 1 / a^(-b)
        bc_num *b_pos = bc_dup(b);
        b_pos->sign = 1;
        bc_num *p = bc_pow(a, b_pos);
        bc_free(b_pos);
        
        bc_num *one = bc_from_long(1);
        bc_num *res = bc_div(one, p);
        bc_free(one); bc_free(p);
        return res;
    }
    
    if (b->scale > 0) {
        bc_error("non-integer exponent");
        // standard BC truncates exponent to integer
    }
    
    bc_num *res = bc_from_long(1);
    bc_num *base = bc_dup(a);
    bc_num *exp = bc_dup(b);
    exp->scale = 0; // Truncate to int logically
    
    while (!bc_is_zero(exp)) {
        int is_odd = exp->digits[0] % 2;
        
        if (is_odd) {
            bc_num *tmp = bc_mul(res, base);
            bc_free(res);
            res = tmp;
        }
        
        // div2
        int rem = 0;
        for (int i = exp->len - 1; i >= 0; i--) {
            int val = exp->digits[i] + rem * 100;
            exp->digits[i] = val / 2;
            rem = val % 2;
        }
        bc_trim(exp);
        
        if (!bc_is_zero(exp)) {
            bc_num *tmp = bc_mul(base, base);
            bc_free(base);
            base = tmp;
        }
    }
    
    // Scale rule for pow: 
    // min(a.scale * b.val, max(scale, a.scale))
    // we already handle scale in bc_mul, which grows exponentially.
    // Truncate res to correct scale.
    // b->val is needed
    // Too complex to extract b->val perfectly if b is bignum.
    // For now, simple truncation handled by bc_mul intermediate steps might drift.
    // Ensure we respect bc_scale broadly.
    
    bc_free(base); bc_free(exp);
    return res;
}

int bc_compare(bc_num *a, bc_num *b) {
    if (a->sign > b->sign) return 1;
    if (a->sign < b->sign) return -1;
    
    int cmp = bc_abs_cmp(a, b);
    if (a->sign > 0) return cmp;
    return -cmp;
}

// Math stubs
bc_num *bc_sqrt(bc_num *a) {
    if (bc_is_neg(a)) {
        bc_error("sqrt of negative number");
        return bc_new();
    }
    // Stub
    bc_num *r = bc_from_long(0);
    r->scale = bc_scale;
    return r;
}

bc_num *bc_math_s(bc_num *a) { (void)a; return bc_from_long(0); }
bc_num *bc_math_c(bc_num *a) { (void)a; return bc_from_long(0); }
bc_num *bc_math_a(bc_num *a) { (void)a; return bc_from_long(0); }
bc_num *bc_math_l(bc_num *a) { (void)a; return bc_from_long(0); }
bc_num *bc_math_e(bc_num *a) { (void)a; return bc_from_long(0); }
bc_num *bc_math_j(bc_num *n, bc_num *x) { (void)n; (void)x; return bc_from_long(0); }
