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
    int s = n->scale;
    int start = s / 2;
    long long p = 1;
    
    for (int i = start; i < n->len; i++) {
        int val = n->digits[i];
        if (i == start && (s % 2) != 0) {
            val /= 10;
        }
        res += (long long)val * p;
        if (i == start && (s % 2) != 0) p = 10;
        else p *= 100;
    }
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
    if (!s) return bc_new();

    while (isspace(*s)) s++;
    if (*s == '\0') return bc_new();

    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

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
            if (d >= ibase && d <= 35) d = ibase - 1;
            else break;
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
    
    if (ibase == 10) {
        val->scale = frac_len;
        val->sign = bc_is_zero(val) ? 0 : sign;
        bc_trim(val);
        return val;
    }
    
    if (frac_len > 0) {
        bc_num *ib = bc_from_long(ibase);
        bc_num *pow_f = bc_from_long(frac_len);
        bc_num *denom = bc_pow(ib, pow_f);
        bc_free(ib);
        bc_free(pow_f);
        
        int old_scale = bc_scale;
        bc_scale = frac_len; 
        bc_num *res = bc_div(val, denom);
        bc_scale = old_scale;
        
        res->sign = bc_is_zero(res) ? 0 : sign;
        bc_free(val);
        bc_free(denom);
        return res;
    }
    
    val->sign = bc_is_zero(val) ? 0 : sign;
    bc_trim(val);
    return val;
}

bc_num *bc_from_string(const char *s, int ibase) {
    if (ibase < 2 || ibase > 36) ibase = 10;
    return bc_from_string_base(s, ibase);
}

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
    
    bc_num *copy = bc_dup(n);
    copy->sign = 1;
    copy->scale = 0; // Treat digits as integer

    bc_num *ten = bc_from_long(10);
    bc_num *sc = bc_from_long(n->scale);
    bc_num *pow10 = bc_pow(ten, sc);
    bc_free(ten);
    bc_free(sc);

    int old_scale = bc_scale;
    bc_scale = 0;

    bc_num *int_part = bc_div(copy, pow10);
    bc_num *frac_int = bc_mod(copy, pow10);
    bc_free(copy);

    bc_num *base = bc_from_long(obase);
    
    // Collect integer digits
    int max_digits = 1024;
    int *out_digits = malloc(max_digits * sizeof(int));
    int ds = 0;
    
    bc_num *curr_int = int_part; // Transfer ownership
    if (bc_is_zero(curr_int)) {
        if (ds < max_digits) out_digits[ds++] = 0;
        bc_free(curr_int);
    } else {
        while (!bc_is_zero(curr_int)) {
            bc_num *q = bc_div(curr_int, base);
            bc_num *r = bc_mod(curr_int, base);
            int rem = (int)bc_num_to_long(r);
            if (ds < max_digits) out_digits[ds++] = rem;
            bc_free(curr_int);
            bc_free(r);
            curr_int = q;
        }
        bc_free(curr_int); // Free the final quotient which was 0
    }
    
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

    // Fractional part
    if (n->scale > 0) {
        printf(".");
        int digits_to_print = old_scale > n->scale ? old_scale : n->scale;

        bc_num *curr_frac_int = frac_int; // keep ownership
        for (int i = 0; i < digits_to_print; i++) {
            bc_num *prod = bc_mul(curr_frac_int, base);
            bc_num *d_num = bc_div(prod, pow10);
            bc_num *new_frac_int = bc_mod(prod, pow10);

            int d = bc_num_to_long(d_num);
            if (obase <= 16) {
                if (d < 10) printf("%d", d);
                else printf("%c", 'A' + (d - 10));
            } else {
                printf(" %02d", d);
            }

            bc_free(curr_frac_int);
            curr_frac_int = new_frac_int;
            bc_free(prod);
            bc_free(d_num);
        }
        bc_free(curr_frac_int);
    } else {
        bc_free(frac_int);
    }

    bc_free(base);
    bc_free(pow10);
    bc_scale = old_scale;
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
    /* We cannot just compare length if scales differ.
       Align scales dynamically using the align function. */
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
