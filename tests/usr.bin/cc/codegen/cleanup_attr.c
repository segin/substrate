/*
 * __attribute__((cleanup(fn))) calls fn with the variable's address as
 * the variable goes out of scope, the last declared first, whichever way
 * the scope is left: past its end, return (after the value returned is
 * computed), break, continue, a goto out of it, the end of a statement
 * expression.  cc parsed the attribute and called nothing, so Linux's
 * guard() and scoped_guard() never released their locks and __free()
 * never freed.  Each cleanup prints its variable's tag, in the order gcc
 * runs them.
 */
#include <stdio.h>

static void out(int *p) {
    printf(" ~%d", *p);
}

#define CLEAN __attribute__((cleanup(out)))

static int ret_nested(int k) {
    int a CLEAN = 1;
    {
        int b CLEAN = 2;
        if (k == 0) {
            int c CLEAN = 3;
            return a + b + c; /* 6, computed before ~3 ~2 ~1 */
        }
    }
    return a;
}

static int ret_value_first(void) {
    int v CLEAN = 10;
    return v++; /* 10: the cleanup sees 11 */
}

static void loops(int k) {
    int i;

    for (i = 0; i < 4; i++) {
        int x CLEAN = 20 + i;
        if (i == 1 + k) {
            continue;
        }
        if (i == 2 + k) {
            break;
        }
        printf(" b%d", i);
    }
    while (1) {
        int w CLEAN = 30;
        {
            int inner CLEAN = 31;
            break;
        }
    }
    switch (k) {
    case 0: {
        int s CLEAN = 40;
        printf(" s");
        break;
    }
    default:
        break;
    }
}

static void gotos(int k) {
    int a CLEAN = 50;
    int n = 0;
again:
    {
        int b CLEAN = 51 + n;
        {
            int c CLEAN = 60 + n;
            if (n++ < 1 + k) {
                goto again; /* ~6x ~5x, then back */
            }
            goto done; /* ~6x ~5x */
        }
    }
done:
    printf(" done");
    {
        int d CLEAN = 70;
        goto same; /* stays in d's scope: no cleanup yet */
    same:
        printf(" same");
    }
}

static int stmt_expr(int k) {
    int r = ({
        int t CLEAN = 80;
        t + k + 1;
    });
    return r;
}

static int shadow(int k) {
    int x CLEAN = 90;
    {
        int x CLEAN = 91;
        if (k == 0) {
            return x;
        }
    }
    return x;
}

/* the shape of Linux's scoped_guard(): a for whose step is a goto to an if (0)'s label that breaks */
static void scoped(int k) {
    for (int g CLEAN = 100; g; ({ goto lbl; }))
        if (0) {
        lbl:
            break;
        } else {
            printf(" in");
            if (k) {
                return;
            }
        }
    printf(" after");
}

int main(int argc, char **argv) {
    int k = argc - 1; /* 0, unknown to the compiler */

    (void)argv;
    printf("ret_nested %d\n", ret_nested(k));
    printf("ret_value_first %d\n", ret_value_first());
    printf("loops");
    loops(k);
    printf("\ngotos");
    gotos(k);
    printf("\nstmt_expr %d\n", stmt_expr(k));
    printf("shadow %d\n", shadow(k));
    printf("scoped");
    scoped(k);
    printf("\n");
    return 0;
}
