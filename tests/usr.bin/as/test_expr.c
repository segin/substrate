/*
 * The assembler's expressions: what each means, against what GNU as
 * makes of the same text.
 *
 * Built for the host with usr.bin/as/as_expr.c alone.
 */
#include "as_expr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define NOT_CONST   1   /* parses; has no value without its symbols */
#define NOT_EXPR    2   /* does not parse */
#define DIV_ZERO    3

static const struct {
    const char *text;
    long long value;
    int outcome;
} values[] = {
    /* Shifts bind as multiplication does, tighter than addition. */
    { "1+2<<3", 17, 0 },
    { "1<<2+1", 5, 0 },
    { "64>>2>>1", 8, 0 },
    /* | & ^ are one rank, above + and -, and group from the left. */
    { "1|1+1", 2, 0 },
    { "6^3&1", 1, 0 },
    { "7&3|8", 11, 0 },
    { "5&6^7|8", 11, 0 },
    { "0xff&~0xf", 0xf0, 0 },
    { "5!2", -3, 0 },                       /* or-not */
    { "0!0", -1, 0 },
    /* Comparisons rank with + and -, and are -1 when true. */
    { "1==1", -1, 0 },
    { "1==2", 0, 0 },
    { "3<>4", -1, 0 },
    { "1!=2", -1, 0 },
    { "3<4", -1, 0 },
    { "3<=3", -1, 0 },
    { "5>=6", 0, 0 },
    { "2>1==1", 0, 0 },                     /* (2>1)==1: -1 is not 1 */
    { "2-1==1", -1, 0 },
    { "-1<0", -1, 0 },
    /* && and || are lowest, and give 1. */
    { "1&&2", 1, 0 },
    { "1&&0", 0, 0 },
    { "0||0", 0, 0 },
    { "2||0", 1, 0 },
    { "!0", 1, 0 },
    { "!5", 0, 0 },
    /* The usual. */
    { "2+3*4", 14, 0 },
    { "(2+3)*4", 20, 0 },
    { "100-50-25", 25, 0 },
    { "8/2/2", 2, 0 },
    { "8/(2/2)", 8, 0 },
    { "10%3", 1, 0 },
    { "-7/2", -3, 0 },
    { "-7%2", -1, 0 },
    { "7/-2", -3, 0 },
    { "-(-3)", 3, 0 },
    { "+7", 7, 0 },
    { "5-+3", 2, 0 },
    { "5--3", 8, 0 },
    { "~0", -1, 0 },
    { "(((7)))", 7, 0 },
    { " 3 ", 3, 0 },
    { "1 +2", 3, 0 },
    /* Numbers. */
    { "0x10+010+0b10", 26, 0 },
    { "0X1F", 31, 0 },
    { "0B11", 3, 0 },
    { "0", 0, 0 },
    { "00", 0, 0 },
    { "0xffffffffffffffff", -1, 0 },
    { "18446744073709551615", -1, 0 },
    { "0x8000000000000000", (long long)0x8000000000000000ULL, 0 },
    { "'a", 97, 0 },
    { "'a'", 97, 0 },
    { "'\\n", 10, 0 },
    { "'a+1", 98, 0 },
    /* Sixty-four bits, no traps. */
    { "1<<63", (long long)0x8000000000000000ULL, 0 },
    { "1<<64", 0, 0 },
    { "1<<-1", 0, 0 },
    { "8>>64", 0, 0 },
    { "-8>>1", 0x7ffffffffffffffcLL, 0 },   /* of the bits, not the sign */
    { "0x7fffffffffffffff+1", (long long)0x8000000000000000ULL, 0 },
    { "0x8000000000000000/-1", (long long)0x8000000000000000ULL, 0 },
    { "0x8000000000000000%-1", 0, 0 },
    { "-0x8000000000000000", (long long)0x8000000000000000ULL, 0 },
    { "1/0", 0, DIV_ZERO },
    { "1%0", 0, DIV_ZERO },
    /* What is not a number. */
    { "foo", 0, NOT_CONST },
    { "foo+4", 0, NOT_CONST },
    { "1f", 0, NOT_CONST },
    { "0b", 0, NOT_CONST },                 /* local label 0, backwards */
    { "", 0, NOT_EXPR },
    { "1+", 0, NOT_EXPR },
    { "(1", 0, NOT_EXPR },
    { "1)", 0, NOT_EXPR },
    { "1 2", 0, NOT_EXPR },
    { "2**3", 0, NOT_EXPR },
    { "12abc", 0, NOT_EXPR },
    { "0x", 0, NOT_EXPR },
    { "18446744073709551616", 0, NOT_EXPR },
    { "#", 0, NOT_EXPR },
};

static void check_values(void) {
    size_t i;

    for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        as_expr_t *e = as_parse_expr_string(values[i].text, "t.s", 1);
        long long v = 0;
        int rc;
        int got;

        if (e == NULL) {
            got = NOT_EXPR;
        } else {
            rc = as_expr_eval(e, NULL, NULL, &v);
            got = rc == AS_EXPR_EVAL_OK ? 0 : rc == AS_EXPR_EVAL_ERROR ? DIV_ZERO : NOT_CONST;
        }
        as_expr_free(e);
        if (got != values[i].outcome || (got == 0 && v != values[i].value)) {
            printf("FAIL \"%s\": outcome %d value %lld, wanted outcome %d value %lld\n",
                   values[i].text, got, v, values[i].outcome, values[i].value);
            failures++;
        }
    }
}

static int lookup(void *cookie, const char *name, long long *out) {
    (void)cookie;
    if (strcmp(name, "K") == 0) {
        *out = 8;
        return 1;
    }
    if (strcmp(name, ".") == 0) {
        *out = 0x100;
        return 1;
    }
    return 0;
}

static const struct {
    const char *text;
    int ok;
    long long value;
    const char *add;        /* "1f" stands for a local reference */
    const char *sub;
} linears[] = {
    { "a", 1, 0, "a", NULL },
    { "a+4", 1, 4, "a", NULL },
    { "4+a", 1, 4, "a", NULL },
    { "a-4", 1, -4, "a", NULL },
    { "a+1+1", 1, 2, "a", NULL },
    { "a+2*K", 1, 16, "a", NULL },
    { "b-a", 1, 0, "b", "a" },
    { "b-a+1", 1, 1, "b", "a" },
    { "1+b-a", 1, 1, "b", "a" },
    { "b-(a+4)", 1, -4, "b", "a" },
    { "b-(a-4)", 1, 4, "b", "a" },
    { "-a", 1, 0, NULL, "a" },
    { "-a+b", 1, 0, "b", "a" },
    { "4-a", 1, 4, NULL, "a" },
    { "a-.", 1, -0x100, "a", NULL },
    { ".-a", 1, 0x100, NULL, "a" },
    { "K", 1, 8, NULL, NULL },
    { "K<<1|1", 1, 17, NULL, NULL },
    { "1f", 1, 0, "1f", NULL },
    { "1f-a+2", 1, 2, "1f", "a" },
    { "a-1b", 1, 0, "a", "1f" },
    { "a+b", 0, 0, NULL, NULL },
    { "a-b-c", 0, 0, NULL, NULL },
    { "2*a", 0, 0, NULL, NULL },
    { "a<<1", 0, 0, NULL, NULL },
    { "(b-a)*2", 0, 0, NULL, NULL },
    { "~a", 0, 0, NULL, NULL },
    { "-(b-a)", 0, 0, NULL, NULL },
    { "a==b", 0, 0, NULL, NULL },
};

static int same_term(const char *want, const char *sym, const as_expr_t *local) {
    if (want == NULL) {
        return sym == NULL && local == NULL;
    }
    if (strcmp(want, "1f") == 0) {
        return sym == NULL && local != NULL && local->kind == AS_EXPR_LOCAL_REF;
    }
    return local == NULL && sym != NULL && strcmp(sym, want) == 0;
}

static void check_linears(void) {
    size_t i;

    for (i = 0; i < sizeof(linears) / sizeof(linears[0]); ++i) {
        as_expr_t *e = as_parse_expr_string(linears[i].text, "t.s", 1);
        as_expr_linear_t l;
        int rc = as_expr_eval_linear(e, lookup, NULL, &l);

        if ((rc == AS_EXPR_EVAL_OK) != linears[i].ok ||
            (rc == AS_EXPR_EVAL_OK &&
             (l.value != linears[i].value ||
              !same_term(linears[i].add, l.add_symbol, l.add_local) ||
              !same_term(linears[i].sub, l.sub_symbol, l.sub_local)))) {
            printf("FAIL linear \"%s\": rc %d value %lld add %s sub %s\n", linears[i].text, rc,
                   l.value, l.add_symbol ? l.add_symbol : "-", l.sub_symbol ? l.sub_symbol : "-");
            failures++;
        }
        as_expr_free(e);
    }
}

static void check_numbers(void) {
    static const struct {
        const char *text;
        int ok;
        long long value;
    } n[] = {
        { "42", 1, 42 }, { "-42", 1, -42 }, { "+42", 1, 42 }, { " 7 ", 1, 7 },
        { "0x10", 1, 16 }, { "010", 1, 8 }, { "0b101", 1, 5 }, { "-0x10", 1, -16 },
        { "0xffffffffffffffff", 1, -1 },
        { "", 0, 0 }, { "-", 0, 0 }, { "1+1", 0, 0 }, { "x", 0, 0 }, { "12x", 0, 0 },
        { "0x1ffffffffffffffff", 0, 0 }, { "1 2", 0, 0 },
    };
    size_t i;

    for (i = 0; i < sizeof(n) / sizeof(n[0]); ++i) {
        long long v = 0;
        int rc = as_expr_parse_number(n[i].text, &v);

        if ((rc == 0) != n[i].ok || (rc == 0 && v != n[i].value)) {
            printf("FAIL number \"%s\": rc %d value %lld\n", n[i].text, rc, v);
            failures++;
        }
    }
}

/* A line long enough to have overflowed the stack is refused; one nested
 * deeper than any program writes is refused; neither takes the process. */
static void check_limits(void) {
    size_t terms = 200000;
    char *s = (char *)malloc(terms * 2 + 2);
    size_t i;
    as_expr_t *e;
    long long v;

    if (s == NULL) {
        printf("FAIL: no memory for the test\n");
        failures++;
        return;
    }
    for (i = 0; i < terms; ++i) {
        s[i * 2] = '1';
        s[i * 2 + 1] = '+';
    }
    s[terms * 2] = '1';
    s[terms * 2 + 1] = '\0';
    e = as_parse_expr_string(s, NULL, 0);
    if (e != NULL) {
        printf("FAIL: an expression of %zu terms was accepted\n", terms);
        failures++;
    }
    as_expr_free(e);

    memset(s, '(', 100000);
    s[100000] = '1';
    s[100001] = '\0';
    e = as_parse_expr_string(s, NULL, 0);
    if (e != NULL) {
        printf("FAIL: 100000 open parentheses were accepted\n");
        failures++;
    }
    as_expr_free(e);

    memset(s, '-', 100000);
    e = as_parse_expr_string(s, NULL, 0);
    if (e != NULL) {
        printf("FAIL: 100000 unary minuses were accepted\n");
        failures++;
    }
    as_expr_free(e);

    /* And one of a size programs do write is taken. */
    for (i = 0; i < 1000; ++i) {
        s[i * 2] = '1';
        s[i * 2 + 1] = '+';
    }
    s[2000] = '1';
    s[2001] = '\0';
    if (as_expr_eval_string(s, NULL, NULL, &v) != AS_EXPR_EVAL_OK || v != 1001) {
        printf("FAIL: a sum of 1001 ones\n");
        failures++;
    }
    free(s);
}

int main(void) {
    long long v;

    check_values();
    check_linears();
    check_numbers();
    check_limits();

    if (as_expr_eval_string("K*2+.", lookup, NULL, &v) != AS_EXPR_EVAL_OK || v != 0x110) {
        printf("FAIL: a looked-up constant and the location counter\n");
        failures++;
    }
    if (as_expr_eval_string("K+other", lookup, NULL, &v) != AS_EXPR_EVAL_NOT_CONST) {
        printf("FAIL: an unknown symbol has no value\n");
        failures++;
    }

    if (failures != 0) {
        printf("%d failure(s)\n", failures);
        return 1;
    }
    printf("ok: expressions\n");
    return 0;
}
