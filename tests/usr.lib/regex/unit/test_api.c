#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include "../test_common.h"

int test_api_basic(void) {
    regex_err_t err;
    regex_t *re = regex_compile("abc", REGEX_FLAG_LITERAL, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_capture_count(re) == 1);
    regex_free(re);
    return 0;
}

int test_api_split_free(void) {
    /* Test NULL */
    regex_split_free(NULL);

    /* Test empty */
    regex_split_result_t empty = {0};
    regex_split_free(&empty);
    TEST_ASSERT(empty.items == NULL);
    TEST_ASSERT(empty.count == 0);

    /* Test with items */
    regex_split_result_t res = {0};
    res.count = 2;
    res.items = malloc(2 * sizeof(char *));
    TEST_ASSERT(res.items != NULL);
    res.items[0] = strdup("hello");
    res.items[1] = strdup("world");
    TEST_ASSERT(res.items[0] != NULL);
    TEST_ASSERT(res.items[1] != NULL);

    regex_split_free(&res);
    TEST_ASSERT(res.items == NULL);
    TEST_ASSERT(res.count == 0);

    /* Test items allocated but count 0 */
    regex_split_result_t res2 = {0};
    res2.count = 0;
    res2.items = malloc(1 * sizeof(char *));
    TEST_ASSERT(res2.items != NULL);
    res2.items[0] = NULL;

    regex_split_free(&res2);
    TEST_ASSERT(res2.items == NULL);
    TEST_ASSERT(res2.count == 0);

    return 0;
}

int test_api_match(void) {
    regex_err_t err;
    size_t caps[2];
    regex_t *re = regex_compile("[a-z]+", REGEX_FLAG_EXTENDED, &err);
    TEST_ASSERT(re != NULL);
    TEST_ASSERT(regex_match(re, "hello", 5, caps, 2, &err) >= 0);
    TEST_ASSERT(caps[0] == 0 && caps[1] == 5);
    regex_free(re);
    return 0;
}


/* Zero-minimum counted repetition.  Regression test: `x{0}` used to compile
 * to a NULL NFA start state and segfault regcomp(), and `x{0,n}` / `x{0,}`
 * used to emit one mandatory copy, so they behaved as `x{1,n+1}` / `x+`.
 * Driven through the POSIX entry points because that is the API grep, less
 * and TDE use, and it is where the damage showed up. */
int test_repeat_zero_min(void) {
    static const struct {
        const char *pattern;
        const char *subject;
        int         should_match;
        int         so;
        int         eo;
    } cases[] = {
        { "a{0}",   "bbb",  1, 0, 0 },   /* used to SIGSEGV in regcomp() */
        { "a{0,0}", "bbb",  1, 0, 0 },
        { "a{0,2}", "bbb",  1, 0, 0 },   /* used to be NOMATCH (as a{1,3}) */
        { "a{0,2}", "aab",  1, 0, 2 },
        { "a{0,}",  "bbb",  1, 0, 0 },   /* used to behave as a+ */
        { "a{0,}",  "aaa",  1, 0, 3 },
        /* Non-zero minimums must keep working -- guards over-correction. */
        { "a{2,3}", "aaaa", 1, 0, 3 },
        { "a{3}",   "aaaa", 1, 0, 3 },
        { "a{2}",   "ab",   0, 0, 0 },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        regex_t re;
        regmatch_t m[1];
        int rc = regcomp(&re, cases[i].pattern, REG_EXTENDED);
        TEST_ASSERT(rc == 0);
        int er = regexec(&re, cases[i].subject, 1, m, 0);
        if (cases[i].should_match) {
            TEST_ASSERT(er == 0);
            TEST_ASSERT(m[0].rm_so == cases[i].so);
            TEST_ASSERT(m[0].rm_eo == cases[i].eo);
        } else {
            TEST_ASSERT(er != 0);
        }
        regfree(&re);
    }
    return 0;
}

/* `^` dominance.  Regression test: prog->uses_bol was a presence flag set by
 * any NODE_BOL anywhere in the AST, and it pruned the whole unanchored scan.
 * So `^foo|bar` could only match at offset 0 and `grep -E '^foo|bar'` missed
 * every `bar` that was not at the start of a line. */
int test_bol_dominance(void) {
    static const struct {
        const char *pattern;
        const char *subject;
        int         should_match;
        int         so;
    } cases[] = {
        /* ^ in only ONE alternative must not anchor the other. */
        { "^foo|bar",   "xxbar",   1, 2 },
        { "bar|^foo",   "xxbar",   1, 2 },
        { "^foo|bar",   "fooy",    1, 0 },
        { "(^foo|bar)", "xxbar",   1, 2 },
        /* Genuinely anchored patterns must still be anchored. */
        { "^abc",       "xabc",    0, 0 },
        { "^abc",       "abcx",    1, 0 },
        { "^a|^b",      "xa",      0, 0 },
        { "^(a|b)",     "xa",      0, 0 },
        /* A leading empty-matching element does not anchor what follows. */
        { "x*^a",       "a",       1, 0 },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        regex_t re;
        regmatch_t m[1];
        int rc = regcomp(&re, cases[i].pattern, REG_EXTENDED);
        TEST_ASSERT(rc == 0);
        int er = regexec(&re, cases[i].subject, 1, m, 0);
        if (cases[i].should_match) {
            TEST_ASSERT(er == 0);
            TEST_ASSERT(m[0].rm_so == cases[i].so);
        } else {
            TEST_ASSERT(er != 0);
        }
        regfree(&re);
    }
    return 0;
}

/* What `.` may refuse to match.  POSIX: only REG_NEWLINE excludes <newline>,
 * and <newline> is LF alone.  `.` used to reject both LF and CR regardless of
 * flags, so `a.c` matched neither "a\nc" nor "a\rc". */
int test_dot_newline(void) {
    static const struct {
        const char *subject;
        int         cflags_newline;
        int         should_match;
    } cases[] = {
        { "a\nc", 0, 1 },   /* no REG_NEWLINE: `.` matches LF */
        { "a\nc", 1, 0 },   /* REG_NEWLINE: it does not */
        { "a\rc", 0, 1 },   /* CR is an ordinary character ... */
        { "a\rc", 1, 1 },   /* ... even under REG_NEWLINE */
        { "abc",  0, 1 },
        { "abc",  1, 1 },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        regex_t re;
        int cf = REG_EXTENDED | (cases[i].cflags_newline ? REG_NEWLINE : 0);
        int rc = regcomp(&re, "a.c", cf);
        TEST_ASSERT(rc == 0);
        int er = regexec(&re, cases[i].subject, 0, NULL, 0);
        TEST_ASSERT((er == 0) == (cases[i].should_match != 0));
        regfree(&re);
    }
    return 0;
}

/* BRE vs ERE repetition syntax.  Regression test: `+`, `?` and `{m,n}` were
 * treated as operators in BOTH dialects, so in the BRE that grep uses by
 * default `a\{3\}` and `a\+` matched nothing and a literal `a+b` did not
 * match "a+b". */
int test_bre_ere_repeat(void) {
    static const struct {
        const char *pattern;
        int         extended;
        const char *subject;
        int         should_match;
        int         eo;
    } cases[] = {
        /* BRE: the operators are backslash-spelled ... */
        { "a\\{3\\}",  0, "aaaa", 1, 3 },
        { "a\\{2,\\}", 0, "aaaa", 1, 4 },
        { "a\\+",      0, "aaa",  1, 3 },
        { "a\\?",      0, "ab",   1, 1 },
        { "a*",        0, "aaa",  1, 3 },   /* `*` is an operator in both */
        /* ... and the bare forms are ordinary characters. */
        { "a+b",       0, "a+b",  1, 3 },
        { "a?b",       0, "a?b",  1, 3 },
        { "a{3}",      0, "a{3}", 1, 4 },
        /* ERE: the bare forms are the operators. */
        { "a{3}",      1, "aaaa", 1, 3 },
        { "a+",        1, "aaa",  1, 3 },
        { "a?",        1, "b",    1, 0 },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        regex_t re;
        regmatch_t m[1];
        int rc = regcomp(&re, cases[i].pattern,
                         cases[i].extended ? REG_EXTENDED : 0);
        TEST_ASSERT(rc == 0);
        int er = regexec(&re, cases[i].subject, 1, m, 0);
        if (cases[i].should_match) {
            TEST_ASSERT(er == 0);
            TEST_ASSERT(m[0].rm_eo == cases[i].eo);
        } else {
            TEST_ASSERT(er != 0);
        }
        regfree(&re);
    }
    return 0;
}

/* Leading `]` in a bracket expression.  Regression test: the item loop
 * stopped at `]` before consuming anything, so `[]]` parsed as an empty
 * class plus a stray `]`.  An empty class matches nothing, so `[]]`,
 * `[^]]` and `[]a]` were dead for every input while still compiling OK. */
int test_bracket_leading_rbracket(void) {
    static const struct {
        const char *pattern;
        const char *subject;
        int         should_match;
    } cases[] = {
        { "[]]",   "x]y", 1 },
        { "[^]]",  "x",   1 },
        { "[^]]",  "]",   0 },
        { "[]a]",  "a",   1 },
        { "[]a]",  "]",   1 },
        { "[]-a]", "_",   1 },   /* the literal ] can start a range */
        { "[a]]",  "a]",  1 },   /* ordinary: class {a} then literal ] */
        { "[abc]", "xbz", 1 },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        regex_t re;
        int rc = regcomp(&re, cases[i].pattern, REG_EXTENDED);
        TEST_ASSERT(rc == 0);
        int er = regexec(&re, cases[i].subject, 0, NULL, 0);
        TEST_ASSERT((er == 0) == (cases[i].should_match != 0));
        regfree(&re);
    }
    return 0;
}

/* Linear-time unanchored scanning.  Regression test: dfa_match_span() kept
 * stepping the DFA after it entered a dead (empty NFA set) state, charging
 * one step of the shared budget per remaining byte.  Each scan start then
 * cost O(n) and the whole scan O(n^2), so the 1,000,000-step default budget
 * ran out at a subject of ~1414 bytes and the match was reported as a
 * timeout -- which regexec() turns into a plain "no match".  Measured
 * threshold scaled as sqrt(budget), confirming the quadratic behaviour. */
int test_long_subject_scan(void) {
    static const size_t fillers[] = { 1500, 4000, 20000, 100000 };
    size_t i;

    for (i = 0; i < sizeof(fillers) / sizeof(fillers[0]); ++i) {
        size_t n = fillers[i];
        char *s = (char *)malloc(n + 8);
        regex_t re;
        regmatch_t m[1];
        int rc;
        int er;

        TEST_ASSERT(s != NULL);
        memset(s, 'z', n);
        memcpy(s + n, "needle", 6);
        s[n + 6] = '\0';

        rc = regcomp(&re, "needle", REG_EXTENDED);
        TEST_ASSERT(rc == 0);
        er = regexec(&re, s, 1, m, 0);
        TEST_ASSERT(er == 0);
        TEST_ASSERT((size_t)m[0].rm_so == n);
        regfree(&re);
        free(s);
    }
    return 0;
}
