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

// Ensure capacity for 'needed' digits (preserves existing)
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
    if (!s) return n;

    // Skip whitespace
    while (isspace(*s)) s++;

    if (*s == '\0') return n;

    // Sign
    int sign = 1;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    // Count digits and calculate scale
    int int_digits = 0;
    int frac_digits = 0;
    int seen_dot = 0;

    const char *p = s;
    while (*p) {
        if (isdigit(*p)) {
            if (seen_dot) frac_digits++;
            else int_digits++;
        } else if (*p == '.') {
            if (seen_dot) break;
            seen_dot = 1;
        } else {
            break;
        }
        p++;
    }

    int total_digits = int_digits + frac_digits;
    if (total_digits == 0) {
        return n; // Zero
    }

    n->sign = sign;
    n->scale = frac_digits;

    // Allocate: 2 decimal digits -> 1 base-100 digit
    int needed = (total_digits + 1) / 2;
    bc_expsize(n, needed);
    n->len = needed;

    // Fill digits from end
    const char *end = p;
    const char *curr = end - 1;
    int digit_idx = 0;

    while (curr >= s) {
        if (*curr == '.') {
            curr--;
            continue;
        }

        int val = 0;
        int multiplier = 1;

        // First decimal digit (low)
        if (isdigit(*curr)) {
            val += (*curr - '0') * multiplier;
            multiplier *= 10;
            curr--;
        }

        // Second decimal digit (high)
        if (curr >= s && *curr == '.') curr--;
        if (curr >= s && isdigit(*curr)) {
            val += (*curr - '0') * multiplier;
            curr--;
        }

        if (digit_idx < n->cap) {
            n->digits[digit_idx++] = val;
        }
    }

    // Trim leading zeros
    while (n->len > 0 && n->digits[n->len - 1] == 0) {
        n->len--;
    }

    if (n->len == 0) n->sign = 0;

    return n;
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

    // Calculate D (total decimal digits in the significant part)
    int msd = n->digits[n->len - 1];
    int digits_in_msd = (msd >= 10) ? 2 : 1;
    int D = (n->len - 1) * 2 + digits_in_msd;
    
    int scale = n->scale;

    if (scale >= D) {
        printf("0.");
        for (int i = 0; i < scale - D; i++) printf("0");

        // Print all digits
        for (int i = n->len - 1; i >= 0; i--) {
            if (i == n->len - 1) printf("%d", n->digits[i]);
            else printf("%02d", n->digits[i]);
        }
    } else {
        int int_part = D - scale;
        int count = 0;

        // Iterate and print, inserting dot
        for (int i = n->len - 1; i >= 0; i--) {
            int val = n->digits[i];

            // For MSD, handle 1 or 2 digits
            if (i == n->len - 1) {
                if (digits_in_msd == 2) {
                    int d1 = val / 10;
                    int d2 = val % 10;
                    printf("%d", d1);
                    count++;
                    if (count == int_part && scale > 0) printf(".");
                    printf("%d", d2);
                    count++;
                    if (count == int_part && scale > 0) printf(".");
                } else {
                    printf("%d", val);
                    count++;
                    if (count == int_part && scale > 0) printf(".");
                }
            } else {
                // Always 2 digits
                int d1 = val / 10;
                int d2 = val % 10;
                printf("%d", d1);
                count++;
                if (count == int_part && scale > 0) printf(".");
                printf("%d", d2);
                count++;
                if (count == int_part && scale > 0) printf(".");
            }
        }
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
