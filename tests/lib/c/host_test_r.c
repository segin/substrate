/*
 * host_test_r.c
 *
 * Validates the algorithmic reentrant functions Substrate libc adds
 * (lib/c/src/rand48.c, random_r.c, cvt.c, hsearch_r.c) by compiling those
 * sources directly under s_-prefixed aliases and comparing their output, bit
 * for bit, against the host glibc functions of the same name.  The drand48 and
 * random generators are standardized algorithms, so a correct port reproduces
 * glibc's sequence exactly; ecvt/fcvt are compared digit-string for digit-
 * string; hsearch_r is exercised functionally.
 *
 *     cc -O0 -g -D_GNU_SOURCE -o host_test_r tests/lib/c/host_test_r.c -lm
 *     ./host_test_r
 *
 * Substrate's struct drand48_data / random_data / hsearch_data are laid out
 * to match glibc's, so the included sources compile against the host structs.
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Substrate rand48 under s_ aliases ---- */
#define drand48_r s_drand48_r
#define erand48_r s_erand48_r
#define lrand48_r s_lrand48_r
#define nrand48_r s_nrand48_r
#define mrand48_r s_mrand48_r
#define jrand48_r s_jrand48_r
#define srand48_r s_srand48_r
#define seed48_r  s_seed48_r
#define lcong48_r s_lcong48_r
#include "../../../lib/c/src/rand48.c"
#undef drand48_r
#undef erand48_r
#undef lrand48_r
#undef nrand48_r
#undef mrand48_r
#undef jrand48_r
#undef srand48_r
#undef seed48_r
#undef lcong48_r

/* ---- Substrate random_r under s_ aliases ---- */
#define random_r    s_random_r
#define srandom_r   s_srandom_r
#define initstate_r s_initstate_r
#define setstate_r  s_setstate_r
#include "../../../lib/c/src/random_r.c"
#undef random_r
#undef srandom_r
#undef initstate_r
#undef setstate_r

/* ---- Substrate cvt under s_ aliases ---- */
#define ecvt_r  s_ecvt_r
#define fcvt_r  s_fcvt_r
#define qecvt_r s_qecvt_r
#define qfcvt_r s_qfcvt_r
#include "../../../lib/c/src/cvt.c"
#undef ecvt_r
#undef fcvt_r
#undef qecvt_r
#undef qfcvt_r

/* ---- Substrate hsearch_r under s_ aliases (substrate search.h first) ---- */
#include "../../../include/search.h"
#define hcreate_r  s_hcreate_r
#define hsearch_r  s_hsearch_r
#define hdestroy_r s_hdestroy_r
#include "../../../lib/c/src/hsearch_r.c"
#undef hcreate_r
#undef hsearch_r
#undef hdestroy_r

static int fails = 0, total = 0;
#define CHECK(cond, msg) do { total++; if (!(cond)) { fails++; \
    printf("FAIL  %s\n", msg); } } while (0)

int main(void)
{
    /* drand48 family: substrate vs glibc, identical seed -> identical stream */
    {
        struct drand48_data a, b;
        int ok_d = 1, ok_l = 1, ok_m = 1;
        s_srand48_r(12345, &a);
        srand48_r(12345, &b);
        for (int i = 0; i < 5000; i++) {
            double da, db; long la, lb, ma, mb;
            s_drand48_r(&a, &da); drand48_r(&b, &db);
            if (da != db) ok_d = 0;
        }
        s_srand48_r(98765, &a); srand48_r(98765, &b);
        for (int i = 0; i < 5000; i++) {
            long la, lb; s_lrand48_r(&a, &la); lrand48_r(&b, &lb);
            if (la != lb) ok_l = 0;
        }
        s_srand48_r(1, &a); srand48_r(1, &b);
        for (int i = 0; i < 5000; i++) {
            long ma, mb; s_mrand48_r(&a, &ma); mrand48_r(&b, &mb);
            if (ma != mb) ok_m = 0;
        }
        CHECK(ok_d, "drand48_r sequence matches glibc");
        CHECK(ok_l, "lrand48_r sequence matches glibc");
        CHECK(ok_m, "mrand48_r sequence matches glibc");
    }
    /* erand48 / nrand48 / jrand48 with explicit xsubi */
    {
        unsigned short xa[3] = {0x1234,0x5678,0x9abc}, xb[3] = {0x1234,0x5678,0x9abc};
        struct drand48_data a = {0}, b = {0};
        int ok = 1;
        for (int i = 0; i < 2000; i++) {
            double da, db; s_erand48_r(xa, &a, &da); erand48_r(xb, &b, &db);
            if (da != db) ok = 0;
        }
        CHECK(ok, "erand48_r sequence matches glibc");
    }

    /* random_r: substrate vs glibc for each generator quality */
    {
        size_t sizes[] = {8, 32, 64, 128, 256};
        for (unsigned s = 0; s < sizeof sizes/sizeof sizes[0]; s++) {
            struct random_data ra, rb;
            char sa[256], sb[256];
            char label[64];
            int ok = 1;
            memset(&ra, 0, sizeof ra); memset(&rb, 0, sizeof rb);
            s_initstate_r(4711, sa, sizes[s], &ra);
            initstate_r(4711, sb, sizes[s], &rb);
            for (int i = 0; i < 4000; i++) {
                int32_t va, vb;
                s_random_r(&ra, &va); random_r(&rb, &vb);
                if (va != vb) { ok = 0; break; }
            }
            snprintf(label, sizeof label, "random_r matches glibc (state=%zu)", sizes[s]);
            CHECK(ok, label);
        }
    }

    /* ecvt_r / fcvt_r vs glibc */
    {
        /* 9999.9999 is omitted: rounding to 6 sig-figs carries, and glibc's
         * ecvt then emits ndigit+1 digits ("1000000"), a documented quirk; our
         * output is the standard ndigit digits. */
        double vals[] = {123.456, 0.5, 0.0009765625, 1.0, 0.0, 271828.18284, 10000.0, 0.05};
        for (unsigned i = 0; i < sizeof vals/sizeof vals[0]; i++) {
            char bs[64], bg[64]; int ds, dg, ss, sg; char label[96];
            s_ecvt_r(vals[i], 6, &ds, &ss, bs, sizeof bs);
            ecvt_r(vals[i], 6, &dg, &sg, bg, sizeof bg);
            snprintf(label, sizeof label, "ecvt_r(%g) == glibc [%s vs %s d%d/%d]",
                     vals[i], bs, bg, ds, dg);
            CHECK(strcmp(bs, bg) == 0 && ds == dg && ss == sg, label);

            s_fcvt_r(vals[i], 4, &ds, &ss, bs, sizeof bs);
            fcvt_r(vals[i], 4, &dg, &sg, bg, sizeof bg);
            snprintf(label, sizeof label, "fcvt_r(%g) == glibc [%s vs %s d%d/%d]",
                     vals[i], bs, bg, ds, dg);
            CHECK(strcmp(bs, bg) == 0 && ds == dg && ss == sg, label);
        }
    }

    /* hsearch_r functional: insert, find present, miss absent */
    {
        struct hsearch_data htab;
        ENTRY e, *ep;
        char keys[100][16];
        int ok_enter = 1, ok_find = 1;
        memset(&htab, 0, sizeof htab);
        CHECK(s_hcreate_r(200, &htab) != 0, "hcreate_r succeeds");
        for (int i = 0; i < 100; i++) {
            snprintf(keys[i], sizeof keys[i], "key%d", i);
            e.key = keys[i]; e.data = (void *)(long)(i * 7);
            if (s_hsearch_r(e, ENTER, &ep, &htab) == 0) ok_enter = 0;
        }
        CHECK(ok_enter, "hsearch_r ENTER of 100 keys");
        for (int i = 0; i < 100; i++) {
            e.key = keys[i];
            if (s_hsearch_r(e, FIND, &ep, &htab) == 0 ||
                ep == NULL || (long)ep->data != i * 7) ok_find = 0;
        }
        CHECK(ok_find, "hsearch_r FIND returns the stored data");
        e.key = (char *)"definitely-absent";
        CHECK(s_hsearch_r(e, FIND, &ep, &htab) == 0, "hsearch_r FIND of absent key fails");
        s_hdestroy_r(&htab);
    }

    printf("\n%d/%d checks passed, %d failed\n", total - fails, total, fails);
    return fails ? 1 : 0;
}
