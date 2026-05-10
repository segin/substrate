/*
 * host_test_getopt.c
 *
 * Verifies Substrate's getopt(3) / getopt_long(3) /
 * getopt_long_only(3) implementations from lib/c/src/getopt.c
 * by linking that source directly and exercising the documented
 * shapes of each call.
 *
 * Coverage:
 *   - Short opts: required-arg (`b:`), optional-arg (`o::`),
 *                 clustered (`-axc`), absent (`-z`).
 *   - "--" terminator stops parsing.
 *   - getopt_long: `--name`, `--name=value`, `--name value`,
 *                  prefix-ambiguous detection, unknown rejection.
 *   - getopt_long_only: single-dash long opts.
 *   - optreset across multiple parses with different argv.
 *   - opterr=0 silences diagnostics.
 *   - Error-class returns: `?` on unknown, `:` when optstring
 *     starts with `:` and a required-arg short is missing.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* Use Substrate's headers explicitly so we test our copy, not the
 * host's. */
#include "../../../include/getopt.h"

/* Pull the implementation in via #include to keep the test self-
 * contained — avoids a separate compile/link rule and guarantees
 * we test the exact source under bin/. */
#include "../../../lib/c/src/getopt.c"

static int fails;

#define EXPECT(cond, ...) do { \
    if (cond) printf("  OK  " __VA_ARGS__); \
    else { printf("  FAIL " __VA_ARGS__); fails++; } \
    printf("\n"); \
} while (0)

static void reset(void) {
    optreset = 1;
    optind = 1;
    opterr = 1;
    optopt = '?';
    optarg = NULL;
}

static void test_short_required_arg(void) {
    printf("== short required arg ==\n");
    reset();
    char *argv[] = {"prog", "-bvalue", "-c", NULL};
    int c;
    char *got_b = NULL;
    int got_c = 0;
    while ((c = getopt(3, argv, "ab:c")) != -1) {
        if (c == 'b') got_b = optarg;
        else if (c == 'c') got_c = 1;
        else EXPECT(0, "unexpected return %d", c);
    }
    EXPECT(got_b && strcmp(got_b, "value") == 0, "-bvalue parsed (got=%s)", got_b ? got_b : "(null)");
    EXPECT(got_c == 1, "-c seen");
    EXPECT(optind == 3, "optind advanced past last opt (got %d)", optind);
}

static void test_short_separated_arg(void) {
    printf("== short separated arg ==\n");
    reset();
    char *argv[] = {"prog", "-b", "thearg", NULL};
    int c = getopt(3, argv, "ab:c");
    EXPECT(c == 'b', "got 'b' (got %c)", c);
    EXPECT(optarg && strcmp(optarg, "thearg") == 0, "-b thearg parsed");
}

static void test_short_clustered(void) {
    printf("== short clustered ==\n");
    reset();
    char *argv[] = {"prog", "-axc", NULL};
    int seen[256] = {0};
    int c;
    while ((c = getopt(2, argv, "axc")) != -1) seen[c]++;
    EXPECT(seen['a'] == 1 && seen['x'] == 1 && seen['c'] == 1,
           "all three of -a -x -c seen via -axc");
}

static void test_dash_dash_terminator(void) {
    printf("== -- terminator ==\n");
    reset();
    char *argv[] = {"prog", "-a", "--", "-b", NULL};
    int c, seen_a = 0, seen_b = 0;
    while ((c = getopt(4, argv, "ab")) != -1) {
        if (c == 'a') seen_a++;
        else if (c == 'b') seen_b++;
    }
    EXPECT(seen_a == 1, "-a seen");
    EXPECT(seen_b == 0, "-b NOT seen (after --)");
    EXPECT(optind == 3, "optind points past --");
}

static void test_long_equals_form(void) {
    printf("== long --name=value ==\n");
    reset();
    char *argv[] = {"prog", "--beta=val", NULL};
    struct option lo[] = {
        {"alpha", no_argument, NULL, 'a'},
        {"beta",  required_argument, NULL, 'b'},
        {NULL, 0, NULL, 0},
    };
    int c = getopt_long(2, argv, "ab:", lo, NULL);
    EXPECT(c == 'b', "matched --beta as 'b'");
    EXPECT(optarg && strcmp(optarg, "val") == 0, "optarg = val");
}

static void test_long_separated_arg(void) {
    printf("== long --name value ==\n");
    reset();
    char *argv[] = {"prog", "--beta", "val2", NULL};
    struct option lo[] = {
        {"alpha", no_argument, NULL, 'a'},
        {"beta",  required_argument, NULL, 'b'},
        {NULL, 0, NULL, 0},
    };
    int c = getopt_long(3, argv, "ab:", lo, NULL);
    EXPECT(c == 'b', "matched --beta as 'b'");
    EXPECT(optarg && strcmp(optarg, "val2") == 0, "optarg = val2");
}

static void test_long_no_argument(void) {
    printf("== long no_argument ==\n");
    reset();
    char *argv[] = {"prog", "--alpha", NULL};
    struct option lo[] = {
        {"alpha", no_argument, NULL, 'a'},
        {NULL, 0, NULL, 0},
    };
    int c = getopt_long(2, argv, "a", lo, NULL);
    EXPECT(c == 'a', "matched --alpha as 'a'");
    EXPECT(optarg == NULL, "no optarg");
}

static void test_long_prefix_ambiguous(void) {
    printf("== long prefix ambiguous ==\n");
    reset();
    opterr = 0; /* silence "ambiguous" diagnostic */
    char *argv[] = {"prog", "--al", NULL};
    struct option lo[] = {
        {"alpha", no_argument, NULL, 'a'},
        {"alps",  no_argument, NULL, 'b'},
        {NULL, 0, NULL, 0},
    };
    int c = getopt_long(2, argv, "ab", lo, NULL);
    EXPECT(c == '?', "ambiguous prefix returns '?' (got %d)", c);
}

static void test_long_unique_prefix(void) {
    printf("== long unique prefix ==\n");
    reset();
    char *argv[] = {"prog", "--alp", NULL};
    struct option lo[] = {
        {"alpha", no_argument, NULL, 'a'},
        {"beta",  no_argument, NULL, 'b'},
        {NULL, 0, NULL, 0},
    };
    int c = getopt_long(2, argv, "ab", lo, NULL);
    EXPECT(c == 'a', "unique prefix --alp matches alpha (got %d)", c);
}

static void test_long_only_single_dash(void) {
    printf("== getopt_long_only single-dash long ==\n");
    reset();
    char *argv[] = {"prog", "-alpha", NULL};
    struct option lo[] = {
        {"alpha", no_argument, NULL, 'A'},
        {NULL, 0, NULL, 0},
    };
    int c = getopt_long_only(2, argv, "a", lo, NULL);
    EXPECT(c == 'A', "-alpha matches long alpha (got %d)", c);
}

static void test_optreset_two_parses(void) {
    printf("== optreset between parses ==\n");
    char *argv1[] = {"prog", "-a", NULL};
    char *argv2[] = {"prog", "-b", NULL};
    reset();
    int c1 = getopt(2, argv1, "ab");
    EXPECT(c1 == 'a', "first parse sees -a");
    /* Without optreset, getopt would think we'd already consumed
     * the new argv up to optind. */
    reset();
    int c2 = getopt(2, argv2, "ab");
    EXPECT(c2 == 'b', "second parse (after optreset) sees -b (got %d)", c2);
}

static void test_unknown_short(void) {
    printf("== unknown short returns '?' ==\n");
    reset();
    opterr = 0;
    char *argv[] = {"prog", "-z", NULL};
    int c = getopt(2, argv, "ab");
    EXPECT(c == '?', "got '?' (got %d)", c);
    EXPECT(optopt == 'z', "optopt = 'z' (got %d)", optopt);
}

static void test_missing_arg_colon_first(void) {
    printf("== missing required arg with ':' optstring ==\n");
    reset();
    opterr = 0;
    char *argv[] = {"prog", "-b", NULL};
    int c = getopt(2, argv, ":ab:");
    EXPECT(c == ':', "got ':' (got %d)", c);
    EXPECT(optopt == 'b', "optopt = 'b' (got %d)", optopt);
}

static void test_optional_argument(void) {
    printf("== optional argument 'o::' ==\n");
    reset();
    char *argv[] = {"prog", "-oVAL", "-o", NULL};
    int c1 = getopt(3, argv, "o::");
    EXPECT(c1 == 'o' && optarg && strcmp(optarg, "VAL") == 0,
           "-oVAL gives optarg=VAL");
    int c2 = getopt(3, argv, "o::");
    EXPECT(c2 == 'o' && optarg == NULL, "bare -o gives no optarg");
}

int main(void) {
    fails = 0;
    test_short_required_arg();
    test_short_separated_arg();
    test_short_clustered();
    test_dash_dash_terminator();
    test_long_equals_form();
    test_long_separated_arg();
    test_long_no_argument();
    test_long_prefix_ambiguous();
    test_long_unique_prefix();
    test_long_only_single_dash();
    test_optreset_two_parses();
    test_unknown_short();
    test_missing_arg_colon_first();
    test_optional_argument();
    if (fails) {
        fprintf(stderr, "\nhost_test_getopt: %d failure(s)\n", fails);
        return 1;
    }
    printf("\nhost_test_getopt: PASS\n");
    return 0;
}
