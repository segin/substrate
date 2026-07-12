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
/*
 * Compare two bc_num digit arrays as raw integers (ignoring scale and sign),
 * tolerating leading zeros.  Used inside bc_div_rem where u and v have been
 * shifted into the integer operands for Knuth division: bc_abs_cmp() would
 * align them by *scale* first, which is wrong here (e.g. it would pad the
 * divisor 2 -> 2.0000 and report 1.2500 < 2.0000, making the early-out return
 * a bogus zero quotient).
 */
static int raw_int_cmp(bc_num *a, bc_num *b) {
    int al = a->len, bl = b->len;
    while (al > 0 && a->digits[al - 1] == 0) al--;
    while (bl > 0 && b->digits[bl - 1] == 0) bl--;
    if (al != bl) return al > bl ? 1 : -1;
    for (int i = al - 1; i >= 0; i--)
        if (a->digits[i] != b->digits[i])
            return a->digits[i] > b->digits[i] ? 1 : -1;
    return 0;
}

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
    
    if (raw_int_cmp(u, v) < 0) {
        q->scale = target_scale;
        if (q_out) *q_out = q; else bc_free(q);
        if (r_out) {
            // Quotient is zero, so a % b == a exactly -- but the remainder is
            // still shown at the POSIX scale max(scale(a), scale + scale(b)),
            // padding a with trailing zeros (220.7137 % 2701.9103 -> 220.71370
            // at scale 1).
            bc_num *rem = bc_dup(a);
            int want = target_scale + b->scale;
            if (want < a->scale) want = a->scale;
            if (rem->scale < want) shift_scale(rem, want);
            *r_out = rem;
        }
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

        /*
         * Knuth Algorithm D, step D3: refine the trial digit so it is at
         * most one too large.  The "q_hat == base" overflow case and the
         * v[n-2] test MUST share one loop: every time q_hat is decremented
         * r_hat has to grow by den so the two stay consistent.  An earlier
         * version capped q_hat to 99 with a separate `if` but left r_hat at
         * num%den (the remainder for the *uncapped* q_hat); the stale, too
         * small r_hat then let the v[n-2] test decrement q_hat further than
         * it should, under-counting the quotient digit for divisors whose
         * leading two-digit estimate overflows (e.g. 1.25 / 1.1180339798).
         */
        while (q_hat >= 100 ||
               (n > 1 && q_hat * v->digits[n - 2] > 100 * r_hat + u->digits[j + n - 2])) {
            q_hat--;
            r_hat += den;
            if (r_hat >= 100) break;
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
    
    // Modulo remainder: posix a - (a/b)*b.  The product q*b must be EXACT
    // (scale = q.scale + b.scale); bc_mul()'s normal POSIX scale cap of
    // min(A+B, max(scale,A,B)) would truncate it and make the remainder
    // wrong (e.g. 7915.7597 % 3026.4668 lost its low digits).  Bump bc_scale
    // over the cap for this one multiply so the full product survives; the
    // remainder then carries the correct scale of max(scale(a), scale+scale(b)).
    if (r_out) {
        bc_num *tmp_q = bc_dup(q);
        int saved_scale = bc_scale;
        bc_scale = q->scale + b->scale;
        bc_num *tmp_mul = bc_mul(tmp_q, b);
        bc_scale = saved_scale;
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
    /*
     * POSIX/GNU define a % b = a - (a/b)*b, where a/b is taken at the current
     * scale.  bc_div_rem computes exactly that remainder, and bc_sub already
     * gives it the correct result scale of max(scale(a), scale + scale(b)).
     * Do NOT overwrite r->scale afterwards: the scale field is an exponent,
     * not a width, so relabeling it without re-scaling the digit array
     * silently multiplies/divides the value by a power of ten (it made
     * 30048.6967 % 53.941 print .0000040 instead of .0000039516).
     */
    bc_div_rem(a, b, NULL, &r, bc_scale);
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

/*
 * Square root via Newton's iteration x <- (x + a/x)/2.
 *
 * POSIX/GNU bc: result scale is max(scale, scale(a)).  Iterate at two guard
 * digits of extra precision so the last reported digit is correct, then
 * truncate to the result scale by dividing by 1 at that scale.  Newton
 * converges quadratically for any positive start, so x0 = a is fine.
 */
bc_num *bc_sqrt(bc_num *a) {
    if (bc_is_neg(a)) {
        bc_error("square root of negative number");
        return bc_from_long(0);
    }
    int rscale = bc_scale > a->scale ? bc_scale : a->scale;
    if (bc_is_zero(a)) {
        bc_num *z = bc_from_long(0);
        z->scale = rscale;
        return z;
    }
    /* GNU bc returns its scale-0 constant 1 for sqrt of any value equal to 1
     * (so scale=20;sqrt(1) prints "1", not "1.000...").  Match that. */
    {
        bc_num *one_chk = bc_from_long(1);
        int eq1 = (bc_compare(a, one_chk) == 0);
        bc_free(one_chk);
        if (eq1) return bc_from_long(1);
    }

    int saved = bc_scale;
    bc_scale = rscale + 2;                 /* guard digits */

    bc_num *one = bc_from_long(1);
    bc_num *two = bc_from_long(2);
    bc_num *x   = bc_dup(a);               /* initial guess */

    for (int i = 0; i < 200; i++) {
        bc_num *adx = bc_div(a, x);        /* a / x         */
        bc_num *sum = bc_add(x, adx);      /* x + a/x       */
        bc_num *nx  = bc_div(sum, two);    /* (x + a/x) / 2 */
        bc_free(adx);
        bc_free(sum);
        int done = (bc_compare(nx, x) == 0);
        bc_free(x);
        x = nx;
        if (done) break;
    }

    /*
     * Truncate the two guard decimal digits (one whole base-100 digit) to
     * land on the result scale.  Done by hand rather than `bc_div(x, 1)`:
     * bc_div() truncating a fractional-scale dividend to scale 0 currently
     * loses the integer part.
     */
    if (x->scale >= rscale + 2 && x->len > 0) {
        memmove(x->digits, x->digits + 1, (size_t)(x->len - 1));
        x->len--;
        x->scale -= 2;
    }
    bc_trim(x);
    bc_free(one);
    bc_free(two);
    bc_scale = saved;
    return x;
}

/* ----------------------------------------------------------------------
 * Transcendental functions for the -l math library.
 *
 * All compute at a guard scale (result scale + headroom) and truncate to the
 * result scale at the end, matching GNU bc's truncating semantics.  The
 * algorithms are standard series with argument reduction; nothing here is
 * borrowed from another bc's library.
 * -------------------------------------------------------------------- */

/* Truncate n toward zero from its current scale down to rscale. */
static void trunc_to_scale(bc_num *n, int rscale) {
    if (n->scale <= rscale) return;
    int drop = n->scale - rscale;
    int b100 = drop / 2, b10 = drop % 2;
    if (b100 > 0) {
        if (b100 >= n->len) { n->len = 0; }
        else { memmove(n->digits, n->digits + b100, (size_t)(n->len - b100));
               n->len -= b100; }
    }
    if (b10 > 0) {
        int rem = 0;
        for (int i = n->len - 1; i >= 0; i--) {
            int v = n->digits[i] + rem * 100;
            n->digits[i] = v / 10;
            rem = v % 10;
        }
    }
    n->scale = rscale;
    bc_trim(n);
}

static bc_num *mk_abs(bc_num *x) {
    bc_num *r = bc_dup(x);
    r->sign = (x->sign != 0) ? 1 : 0;
    return r;
}

/* e(x) = exp(x): halve x until |x|<1, sum the Maclaurin series, square back. */
bc_num *bc_math_e(bc_num *x) {
    int rscale = bc_scale, saved = bc_scale;
    /*
     * e^x has roughly |x|*log10(e) ~ 0.4343*|x| integer digits, and the k
     * repeated squarings at the end amplify the series' relative error by
     * about 2^k.  A fixed rscale+10 guard therefore loses the low fractional
     * digits for large x (e(179.6) ~ 1e78 only agreed to ~25 figures).  Grow
     * the working scale with the magnitude of the result so rscale survives.
     */
    long ip = bc_num_to_long(x); if (ip < 0) ip = -ip;
    int idig = (int)((ip * 4343L) / 10000L) + 1;
    int wscale = rscale + idig + 20;
    bc_scale = wscale;
    bc_num *two = bc_from_long(2), *one = bc_from_long(1);
    bc_num *xr = bc_dup(x);
    int k = 0;
    for (;;) {
        bc_num *ax = mk_abs(xr);
        int c = bc_compare(ax, one);
        bc_free(ax);
        if (c < 0 || k > 4000) break;
        bc_num *h = bc_div(xr, two); bc_free(xr); xr = h; k++;
    }
    bc_num *sum = bc_from_long(1), *term = bc_from_long(1);
    for (int n = 1; n < 100000; n++) {
        bc_num *t1 = bc_mul(term, xr); bc_free(term);
        bc_num *nn = bc_from_long(n);
        term = bc_div(t1, nn); bc_free(t1); bc_free(nn);
        bc_num *ns = bc_add(sum, term); bc_free(sum); sum = ns;
        if (bc_is_zero(term)) break;
    }
    for (int i = 0; i < k; i++) {
        bc_num *s2 = bc_mul(sum, sum); bc_free(sum);
        trunc_to_scale(s2, wscale); sum = s2;
    }
    bc_free(xr); bc_free(two); bc_free(one); bc_free(term);
    bc_scale = saved;
    trunc_to_scale(sum, rscale);
    return sum;
}

/* l(x) = ln(x) via 2*atanh((x-1)/(x+1)), reducing x toward 1 using ln(2). */
static bc_num *atanh_series(bc_num *t) {       /* sum t^(2k+1)/(2k+1) */
    bc_num *sum = bc_dup(t);
    bc_num *t2  = bc_mul(t, t);
    bc_num *pw  = bc_dup(t);
    for (int k = 1; k < 100000; k++) {
        bc_num *np = bc_mul(pw, t2); bc_free(pw); pw = np;
        bc_num *den = bc_from_long(2 * k + 1);
        bc_num *tk = bc_div(pw, den); bc_free(den);
        bc_num *ns = bc_add(sum, tk); bc_free(sum); sum = ns;
        int z = bc_is_zero(tk); bc_free(tk);
        if (z) break;
    }
    bc_free(t2); bc_free(pw);
    return sum;
}

bc_num *bc_math_l(bc_num *x) {
    int rscale = bc_scale, wscale = rscale + 10, saved = bc_scale;
    if (bc_is_neg(x) || bc_is_zero(x)) { bc_error("ln of non-positive number"); return bc_from_long(0); }
    bc_scale = wscale;
    bc_num *one = bc_from_long(1), *two = bc_from_long(2);
    /* ln(2) via atanh(1/3) */
    bc_num *three_tmp = bc_from_long(3);
    bc_num *third = bc_div(one, three_tmp);
    bc_free(three_tmp);
    bc_num *ln2s = atanh_series(third); bc_free(third);
    bc_num *ln2 = bc_mul(ln2s, two); bc_free(ln2s); trunc_to_scale(ln2, wscale);
    /* reduce x toward [2/3, 3/2] counting powers of two */
    bc_num *xr = bc_dup(x);
    bc_num *n3 = bc_from_long(3), *n2 = bc_from_long(2);
    bc_num *threeh = bc_div(n3, two);   /* 1.5 */
    bc_num *twoth  = bc_div(n2, n3);    /* .666 */
    bc_free(n3); bc_free(n2);
    long e = 0;
    while (bc_compare(xr, threeh) > 0) { bc_num *h = bc_div(xr, two); bc_free(xr); xr = h; e++; }
    while (bc_compare(xr, twoth)  < 0) { bc_num *d = bc_mul(xr, two); bc_free(xr); xr = d; e--; }
    bc_num *num = bc_sub(xr, one), *den = bc_add(xr, one);
    bc_num *t = bc_div(num, den); bc_free(num); bc_free(den);
    bc_num *as = atanh_series(t); bc_free(t);
    bc_num *ln = bc_mul(as, two); bc_free(as);
    bc_num *ek = bc_from_long(e);
    bc_num *eln2 = bc_mul(ek, ln2); bc_free(ek);
    bc_num *res = bc_add(ln, eln2); bc_free(ln); bc_free(eln2);
    bc_free(xr); bc_free(one); bc_free(two); bc_free(threeh); bc_free(twoth); bc_free(ln2);
    bc_scale = saved;
    trunc_to_scale(res, rscale);
    return res;
}

/* a(x) = atan(x).  Half-angle reduction atan(x)=2*atan(x/(1+sqrt(1+x^2)))
 * brings |x| small for fast series convergence; works for all x. */
bc_num *bc_math_a(bc_num *x) {
    int rscale = bc_scale, wscale = rscale + 12, saved = bc_scale;
    bc_scale = wscale;
    bc_num *one = bc_from_long(1), *two = bc_from_long(2);
    bc_num *xr = bc_dup(x);
    int k = 0;
    bc_num *ten_tmp = bc_from_long(10);
    bc_num *tenth = bc_div(one, ten_tmp);   /* reduce until |x|<0.1 */
    bc_free(ten_tmp);
    for (;;) {
        bc_num *ax = mk_abs(xr); int c = bc_compare(ax, tenth); bc_free(ax);
        if (c < 0 || k > 4000) break;
        bc_num *x2 = bc_mul(xr, xr);
        bc_num *s = bc_add(one, x2); bc_free(x2);
        bc_num *rt = bc_sqrt(s); bc_free(s);
        bc_num *d = bc_add(one, rt); bc_free(rt);
        bc_num *nx = bc_div(xr, d); bc_free(d); bc_free(xr); xr = nx; k++;
    }
    bc_free(tenth);
    /* series: atan(t) = sum (-1)^n t^(2n+1)/(2n+1) */
    bc_num *sum = bc_dup(xr);
    bc_num *t2 = bc_mul(xr, xr);
    bc_num *pw = bc_dup(xr);
    for (int n = 1; n < 100000; n++) {
        bc_num *np = bc_mul(pw, t2); bc_free(pw); pw = np;
        bc_num *den = bc_from_long(2 * n + 1);
        bc_num *tn = bc_div(pw, den); bc_free(den);
        bc_num *ns = (n & 1) ? bc_sub(sum, tn) : bc_add(sum, tn);
        bc_free(sum); sum = ns;
        int z = bc_is_zero(tn); bc_free(tn);
        if (z) break;
    }
    bc_free(t2); bc_free(pw); bc_free(xr);
    for (int i = 0; i < k; i++) { bc_num *d = bc_mul(sum, two); bc_free(sum);
        trunc_to_scale(d, wscale); sum = d; }
    bc_free(one); bc_free(two);
    bc_scale = saved;
    trunc_to_scale(sum, rscale);
    return sum;
}

/* pi = 4*atan(1) at the current working scale. */
static bc_num *compute_pi(void) {
    bc_num *one = bc_from_long(1), *four = bc_from_long(4);
    bc_num *a1 = bc_math_a(one);
    bc_num *pi = bc_mul(a1, four);
    bc_free(one); bc_free(four); bc_free(a1);
    return pi;
}

/* Reduce x into (-pi, pi]; returns new bc_num. */
static bc_num *reduce_angle(bc_num *x, bc_num *pi, bc_num *twopi) {
    bc_num *r = bc_dup(x);
    /* r = r - twopi*floor(r/twopi + 1/2) via mod; use bc_mod toward range */
    bc_num *q = bc_div(r, twopi);
    /* round q to nearest integer: trunc(q + 0.5*sign) */
    bc_num *h1 = bc_from_long(1), *h2 = bc_from_long(2);
    bc_num *half = bc_div(h1, h2);
    bc_free(h1); bc_free(h2);
    bc_num *adj = bc_is_neg(q) ? bc_sub(q, half) : bc_add(q, half);
    trunc_to_scale(adj, 0);            /* floor toward zero of rounded */
    bc_num *qt = bc_mul(adj, twopi);
    bc_num *nr = bc_sub(r, qt);
    bc_free(r); bc_free(q); bc_free(half); bc_free(adj); bc_free(qt);
    (void)pi;
    return nr;
}

/* sin/cos shared Taylor evaluator on a reduced angle. parity 0 -> sin, 1 -> cos. */
static bc_num *bc_sincos(bc_num *x, int want_cos) {
    int rscale = bc_scale, wscale = rscale + 12, saved = bc_scale;
    bc_scale = wscale;
    bc_num *pi = compute_pi();
    bc_num *two = bc_from_long(2);
    bc_num *twopi = bc_mul(pi, two);
    bc_num *xr;
    if (want_cos) {                       /* cos(x) = sin(x + pi/2) */
        bc_num *halfpi = bc_div(pi, two);
        bc_num *xs = bc_add(x, halfpi); bc_free(halfpi);
        xr = reduce_angle(xs, pi, twopi); bc_free(xs);
    } else {
        xr = reduce_angle(x, pi, twopi);
    }
    /*
     * Taylor: sin(t) = sum_{n>=0} (-1)^n t^(2n+1)/(2n+1)!  Build each term
     * incrementally from the previous one: term_n = term_{n-1} * t^2 /
     * ((2n)(2n+1)).  The factor (2n)(2n+1) is only the *increment* of the
     * factorial, so it must divide the previous term (which already carries
     * (2n-1)!), NOT the full power t^(2n+1) — dividing the full power by just
     * (2n)(2n+1) drops the lower factorial factors and makes the series
     * diverge for |t| around 1 (cos via +pi/2 blew up to garbage).
     */
    bc_num *sum = bc_dup(xr);
    bc_num *t2 = bc_mul(xr, xr);
    bc_num *term = bc_dup(xr);
    for (int n = 1; n < 100000; n++) {
        bc_num *p1 = bc_mul(term, t2); bc_free(term);
        bc_num *d1 = bc_from_long((long long)(2 * n) * (2 * n + 1));
        term = bc_div(p1, d1); bc_free(p1); bc_free(d1);
        bc_num *ns = (n & 1) ? bc_sub(sum, term) : bc_add(sum, term);
        bc_free(sum); sum = ns;
        if (bc_is_zero(term)) break;
    }
    bc_free(t2); bc_free(term); bc_free(xr); bc_free(pi); bc_free(two); bc_free(twopi);
    bc_scale = saved;
    trunc_to_scale(sum, rscale);
    return sum;
}

bc_num *bc_math_s(bc_num *x) { return bc_sincos(x, 0); }
bc_num *bc_math_c(bc_num *x) { return bc_sincos(x, 1); }

/* j(n,x) = Bessel J_n via its ascending series. */
bc_num *bc_math_j(bc_num *nnum, bc_num *x) {
    int rscale = bc_scale, wscale = rscale + 12, saved = bc_scale;
    long n = bc_num_to_long(nnum);
    int neg = 0;
    if (n < 0) { n = -n; neg = (n & 1); }
    bc_scale = wscale;
    bc_num *two = bc_from_long(2);
    bc_num *xh = bc_div(x, two);                 /* x/2 */
    bc_num *xh2 = bc_mul(xh, xh);                /* (x/2)^2 */
    /* term0 = (x/2)^n / n!  */
    bc_num *term = bc_from_long(1);
    for (long i = 0; i < n; i++) { bc_num *t = bc_mul(term, xh); bc_free(term); term = t; }
    for (long i = 2; i <= n; i++) { bc_num *d = bc_from_long(i); bc_num *t = bc_div(term, d); bc_free(d); bc_free(term); term = t; }
    trunc_to_scale(term, wscale);
    bc_num *sum = bc_dup(term);
    for (long m = 1; m < 100000; m++) {
        /* term_m = -term_{m-1} * (x/2)^2 / (m*(m+n)) */
        bc_num *t1 = bc_mul(term, xh2); bc_free(term);
        bc_num *den = bc_from_long((long long)m * (m + n));
        bc_num *t2 = bc_div(t1, den); bc_free(t1); bc_free(den);
        t2->sign = -t2->sign;
        term = t2;
        bc_num *ns = bc_add(sum, term); bc_free(sum); sum = ns;
        if (bc_is_zero(term)) break;
    }
    if (neg) sum->sign = -sum->sign;
    bc_free(term); bc_free(two); bc_free(xh); bc_free(xh2);
    bc_scale = saved;
    trunc_to_scale(sum, rscale);
    return sum;
}
