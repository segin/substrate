/*
 * prop_totalorder.c — property tests for ISO C23 totalOrder predicate
 */

#define _DEFAULT_SOURCE 1

#include <stdio.h>
#include <math.h>

/* ============================================================
 * Total order properties:
 *   1. Reflexive: totalorder(x, x) == 1
 *   2. Antisymmetric: totalorder(x, y) && !totalorder(y, x)
 *   3. Transitive: totalorder(x, y) && totalorder(y, z) => totalorder(x, z)
 *   4. Total: totalorder(x, y) || totalorder(y, x)
 *   5. NaN at top: totalorder(x, NaN) == 1, !totalorder(NaN, x) for finite x
 * ============================================================ */

static void test_reflexive(void) {
    double vals[] = {0.0, -0.0, 1.0, -1.0, INFINITY, -INFINITY, NAN,
                     1.5e-308, -1.5e-308, 1.23456};
    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        if (!totalorder(vals[i], vals[i])) {
            printf("FAIL: totalorder(%g, %g) should be 1 (reflexive)\n",
                   vals[i], vals[i]);
            return;
        }
    }
}

static void test_antisymmetric(void) {
    double vals[] = {0.0, -0.0, 1.0, -1.0, INFINITY, -INFINITY};
    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        for (int j = 0; j < sizeof(vals) / sizeof(vals[0]); j++) {
            if (vals[i] != vals[j]) {
                int xy = totalorder(vals[i], vals[j]);
                int yx = totalorder(vals[j], vals[i]);
                /* At most one should be true (antisymmetry) */
                if (xy && yx) {
                    printf("FAIL: totalorder(%g, %g)=%d and totalorder(%g, %g)=%d\n",
                           vals[i], vals[j], xy, vals[j], vals[i]);
                    return;
                }
            }
        }
    }
}

static void test_transitive(void) {
    double vals[] = {-2.0, -1.0, 0.0, 1.0, 2.0};
    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        for (int j = 0; j < sizeof(vals) / sizeof(vals[0]); j++) {
            for (int k = 0; k < sizeof(vals) / sizeof(vals[0]); k++) {
                if (totalorder(vals[i], vals[j]) && totalorder(vals[j], vals[k])) {
                    if (!totalorder(vals[i], vals[k])) {
                        printf("FAIL: transitivity (%g <= %g <= %g) => %g <= %g\n",
                               vals[i], vals[j], vals[k], vals[i], vals[k]);
                        return;
                    }
                }
            }
        }
    }
}

static void test_total(void) {
    double vals[] = {-2.0, -1.0, 0.0, 1.0, 2.0};
    for (int i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        for (int j = 0; j < sizeof(vals) / sizeof(vals[0]); j++) {
            /* For any pair, at least one direction holds */
            if (!totalorder(vals[i], vals[j]) && !totalorder(vals[j], vals[i])) {
                printf("FAIL: neither totalorder(%g, %g) nor totalorder(%g, %g)\n",
                       vals[i], vals[j], vals[j], vals[i]);
                return;
            }
        }
    }
}

static void test_nan_order(void) {
    double finite = 0.0;
    if (!totalorder(finite, NAN)) {
        printf("FAIL: totalorder(%g, NaN) should be 1\n", finite);
        return;
    }
    if (totalorder(NAN, finite)) {
        printf("FAIL: totalorder(NaN, %g) should be 0\n", finite);
        return;
    }
}

static void test_ordering(void) {
    /* -Inf < -1 < -0 < +0 < 1 < +Inf */
    int x = totalorder(-INFINITY, -1.0);
    int y = totalorder(-1.0, -0.0);
    int z = totalorder(-0.0, 0.0);
    int w = totalorder(0.0, 1.0);
    int v = totalorder(1.0, INFINITY);

    if (!x) printf("FAIL: -Inf < -1\n");
    if (!y) printf("FAIL: -1 < -0\n");
    if (!z) printf("FAIL: -0 < +0\n");
    if (!w) printf("FAIL: +0 < 1\n");
    if (!v) printf("FAIL: 1 < +Inf\n");
}

int main(void) {
    printf("prop_totalorder: starting\n");

    test_reflexive();
    test_antisymmetric();
    test_transitive();
    test_total();
    test_nan_order();
    test_ordering();

    printf("prop_totalorder: done\n");
    return 0;
}
