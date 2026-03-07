/*
 * libbc - Bignum Library for bc/dc
 * num.c: Core management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include "num.h"

int bc_scale = 0;
int bc_ibase = 10;
int bc_obase = 10;

void bc_error(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "bc: runtime error: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    // In strict bc, math errors might not abort the interpreter, just the statement.
    // For libbc, we log to stderr.
}

void bc_warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "bc: warning: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

bc_num *bc_new(void) {
    bc_num *n = malloc(sizeof(bc_num));
    if (!n) {
        perror("bc_new: malloc");
        exit(1);
    }
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

// Ensure capacity for 'needed' base-100 digits (preserves existing)
void bc_expsize(bc_num *n, int needed) {
    if (needed <= n->cap) return;
    int new_cap = needed < 8 ? 8 : needed * 2;
    void *new_digits = realloc(n->digits, new_cap);
    if (!new_digits) {
        perror("bc_expsize: realloc");
        exit(1);
    }
    n->digits = new_digits;
    memset(n->digits + n->cap, 0, new_cap - n->cap);
    n->cap = new_cap;
}

// Trim leading zero digits (both length and scale correctness)
void bc_trim(bc_num *n) {
    while (n->len > 0 && n->digits[n->len - 1] == 0) {
        n->len--;
    }
    if (n->len == 0) {
        n->sign = 0;
    }
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

    bc_expsize(n, 10);
    int i = 0;
    while (v > 0) {
        n->digits[i++] = v % 100;
        v /= 100;
    }
    n->len = i;
    return n;
}

long long bc_num_to_long(bc_num *n) {
    if (!n || n->len == 0) return 0;
    long long res = 0;
    long long p = 1;
    // This only works for the integer part, which is what bc_scale/ibase need.
    // Integer part starts at base-100 digit 'n->scale / 2'
    int start = (n->scale + 1) / 2;
    for (int i = start; i < n->len; i++) {
        res += (long long)n->digits[i] * p;
        p *= 100;
    }
    // Handle the half-digit if scale is odd? No, scale is decimal digits.
    // If scale=1, "1.2" -> [20, 1]. val=12. Integer is 1.
    // digits[0]=20. digits[1]=1. start = 1. res = 1.
    return n->sign < 0 ? -res : res;
}

static int char_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

// Parse string honoring ibase. Support for ibase 2-36.
// Note: single-digit decimal fraction parsing uses ibase.
bc_num *bc_from_string_base(const char *s, int ibase) {
    bc_num *n = bc_new();
    if (!s) return n;

    while (isspace(*s)) s++;
    if (*s == '\0') return n;

    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    // A parsed number is mathematically: sum(d_i * ibase^i) * ibase^-frac_len
    // We will build it via multiplication: val = val * ibase + digit
    bc_num *ibase_num = bc_from_long(ibase);
    bc_num *val = bc_from_long(0);
    
    int frac_len = 0;
    int seen_dot = 0;

    while (*s) {
        if (*s == '.') {
            if (seen_dot) break;
            seen_dot = 1;
            s++;
            continue;
        }
        
        int d = char_val(*s);
        if (d < 0 || d >= ibase) {
            // For ibase <= 16, single character A-F. 
            // What if char is invalid? Stop.
            
            // Wait, bc manual: digits greater than or equal to ibase are set to ibase-1
            if (d >= ibase && d <= 35) {
                d = ibase - 1;
            } else {
                break; // Not a recognized digit at all
            }
        }
        
        if (seen_dot) frac_len++;
        
        bc_num *d_num = bc_from_long(d);
        bc_num *tmp_mul = bc_mul(val, ibase_num);
        bc_num *tmp_add = bc_add(tmp_mul, d_num);
        
        bc_free(val);
        bc_free(tmp_mul);
        bc_free(d_num);
        
        val = tmp_add;
        s++;
    }
    
    bc_free(ibase_num);
    
    // Now val holds the integer representing all digits.
    // It is conceptually val / (ibase ^ frac_len).
    // Let's keep it simple for now if ibase == 10.
    if (ibase == 10) {
        // Just set scale.
        // Wait, val is currently a base-100 representation of the integer value.
        // We need to shift it to align scale properly.
        // If frac_len is say 3, "1.234" -> val=1234, scale=3. 
        // 1234 is base-100: [34, 12]
        val->scale = frac_len;
        val->sign = bc_is_zero(val) ? 0 : sign;
        bc_free(n);
        return val;
    }
    
    // Non-10 ibase handles fractions via division:
    // val_num = int_val / (ibase^frac_len)
    if (frac_len > 0) {
        bc_num *ib = bc_from_long(ibase);
        bc_num *pow_f = bc_from_long(frac_len);
        bc_num *denom = bc_pow(ib, pow_f);
        bc_free(ib);
        bc_free(pow_f);
        
        // Wait, bc POSIX: "fractional part of the constant is scale 
        // determined by number of digits". 
        // Division uses max(scale, obj->scale). Let's temporarily bump bc_scale.
        
        // This is tricky because bc_div uses bc_scale.
        // In reality, constants are converted using floating point division.
        bc_num *res = bc_div(val, denom);
        res->sign = bc_is_zero(res) ? 0 : sign;
        bc_free(val);
        bc_free(denom);
        bc_free(n);
        return res;
    }
    
    val->sign = bc_is_zero(val) ? 0 : sign;
    bc_free(n);
    return val;
}

bc_num *bc_from_string(const char *s, int ibase) {
    if (ibase < 2 || ibase > 36) ibase = 10;
    return bc_from_string_base(s, ibase);
}

// Print base 10 (optimised)
void bc_print(bc_num *n) {
    if (!n) {
        printf("(null)");
        return;
    }
    if (n->sign == 0 || n->len == 0) {
        printf("0");
        if (n->scale > 0) {
            printf(".");
            for (int i = 0; i < n->scale; i++) printf("0");
        }
        return;
    }
    if (n->sign < 0) printf("-");

    int msd = n->digits[n->len - 1];
    int digits_in_msd = (msd >= 10) ? 2 : 1;
    int D = (n->len - 1) * 2 + digits_in_msd;
    
    int scale = n->scale;

    if (scale >= D) {
        printf("0.");
        for (int i = 0; i < scale - D; i++) printf("0");

        for (int i = n->len - 1; i >= 0; i--) {
            if (i == n->len - 1) printf("%d", n->digits[i]);
            else printf("%02d", n->digits[i]);
        }
    } else {
        int int_part = D - scale;
        int count = 0;

        for (int i = n->len - 1; i >= 0; i--) {
            int val = n->digits[i];

            if (i == n->len - 1) {
                if (digits_in_msd == 2) {
                    printf("%d", val / 10);
                    count++;
                    if (count == int_part && scale > 0) printf(".");
                    printf("%d", val % 10);
                    count++;
                    if (count == int_part && scale > 0) printf(".");
                } else {
                    printf("%d", val);
                    count++;
                    if (count == int_part && scale > 0) printf(".");
                }
            } else {
                printf("%d", val / 10);
                count++;
                if (count == int_part && scale > 0) printf(".");
                printf("%d", val % 10);
                count++;
                if (count == int_part && scale > 0) printf(".");
            }
        }
    }
}

void bc_print_base(bc_num *n, int obase) {
    if (obase == 10 || obase < 2) {
        bc_print(n);
        return;
    }
    
    if (bc_is_zero(n)) {
        if (obase <= 16) printf("0\n");
        else printf(" 0\n");
        return;
    }
    
    if (n->sign < 0) printf("-");
    
    // Integer part conversion
    // Copy integer part... (simple truncation for now)
    // To truncate, we divide by 10^scale or similar if we supported floating point ops well.
    // For now, if it's obase > 10, handle integers via successive division.
    // TODO: implement full obase fractional conversion
    if (n->scale > 0) {
        bc_warn("libbc: obase > 10 for fractional numbers not fully implemented");
        bc_print(n);
        return;
    }
    
    bc_num *copy = bc_dup(n);
    copy->sign = 1;
    copy->scale = 0; // Truncate fractional part for integer obase conversion
    bc_num *base = bc_from_long(obase);
    
    // Collect digits
    int max_digits = 1024;
    int *out_digits = malloc(max_digits * sizeof(int));
    int ds = 0;
    
    int old_scale = bc_scale;
    bc_scale = 0;
    
    while (!bc_is_zero(copy)) {
        bc_num *q = bc_div(copy, base);
        bc_num *r = bc_mod(copy, base);
        // Extract remainder integer
        int rem = (int)bc_num_to_long(r);
        if (ds < max_digits) out_digits[ds++] = rem;
        bc_free(copy);
        bc_free(r);
        copy = q;
    }
    bc_free(copy);
    bc_free(base);
    bc_scale = old_scale;
    
    for (int i = ds - 1; i >= 0; i--) {
        if (obase <= 16) {
            int d = out_digits[i];
            if (d < 10) printf("%d", d);
            else printf("%c", 'A' + (d - 10));
        } else {
            printf(" %02d", out_digits[i]);
        }
    }
    free(out_digits);
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
// MUST handle scale correctly
int bc_abs_cmp(bc_num *a, bc_num *b) {
    // We cannot just compare length if scales differ.
    // Align scales dynamically without allocating.
    // Total decimal length (int + frac) ...
    // Much easier: use align function
    bc_num *aa, *bb;
    bc_align_scale(a, b, &aa, &bb);
    
    int res = 0;
    if (aa->len > bb->len) res = 1;
    else if (aa->len < bb->len) res = -1;
    else {
        for (int i = aa->len - 1; i >= 0; i--) {
            if (aa->digits[i] > bb->digits[i]) { res = 1; break; }
            if (aa->digits[i] < bb->digits[i]) { res = -1; break; }
        }
    }
    bc_free(aa);
    bc_free(bb);
    return res;
}
