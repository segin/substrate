/*
 * torture_regex.c  —  128+-point conformance torture for substrate's libregex.
 *
 * Compiles substrate's POSIX layer (usr.lib/regex) under sub_-prefixed names
 * and matches every case against the host glibc regex of the same pattern,
 * comparing the full submatch vector (rm[0..3]) offset for offset.  Substrate's
 * default engine (the self-contained "safe" engine) must reproduce glibc's
 * leftmost-longest match and capture offsets exactly.
 *
 * Build (from the repo root):
 *   cc -I include -I usr.lib/regex/src -I usr.lib/regex/include \
 *      -Dregcomp=sub_regcomp -Dregexec=sub_regexec -Dregfree=sub_regfree \
 *      -Dregerror=sub_regerror -o torture_regex \
 *      tests/lib/regex/host/torture_regex.c \
 *      usr.lib/regex/src/posix_compat.c usr.lib/regex/src/compile.c \
 *      usr.lib/regex/src/exec.c usr.lib/regex/src/engine_safe.c \
 *      usr.lib/regex/src/engine_posix.c usr.lib/regex/src/engine_pcre2_adapter.c \
 *      usr.lib/regex/src/util.c
 *   ./torture_regex
 *
 * The -D renames make this TU's regcomp/regexec resolve to substrate's, leaving
 * the bare names bound to glibc for the reference side.
 */

#include <regex.h>
#include <stdio.h>
#include <string.h>

/* substrate's POSIX layer, under sub_ names (via -D on the command line) */
int  sub_regcomp(regex_t *, const char *, int);
int  sub_regexec(const regex_t *, const char *, size_t, regmatch_t *, int);
void sub_regfree(regex_t *);

#define NSLOT 4   /* compare whole match + up to 3 capture groups */

struct tcase { const char *pat; const char *txt; int ere; };

static const struct tcase cases[] = {
  /* ---- literals & basics ---- */
  {"abc",            "abc",            0},
  {"abc",            "xabcy",          0},
  {"abc",            "ab",             0},
  {"",               "anything",       0},
  {"a",              "",               0},
  {"hello",          "hello world",    0},
  {"world",          "hello world",    0},
  {"z",              "abc",            0},

  /* ---- the dot metacharacter ---- */
  {".",              "x",              0},
  {".",              "",               0},
  {"a.c",            "abc",            0},
  {"a.c",            "axc",            0},
  {"...",            "abcd",           0},
  {"a.b.c",          "axbyc",          0},

  /* ---- star (greedy, leftmost-longest) ---- */
  {"a*",             "aaaa",           0},
  {"a*",             "bbb",            0},
  {"ab*c",           "ac",             0},
  {"ab*c",           "abbbc",          0},
  {".*",             "anything here",  0},
  {".*x",            "xaxbx",          0},
  {"a.*b",           "a123b456b",      0},
  {"\\(a*\\)",       "aaab",           0},
  {"\\(.*\\)",       "abc",            0},
  {"x\\(a*\\)y",     "xaaay",          0},

  /* ---- plus / question (ERE) ---- */
  {"a+",             "aaaa",           1},
  {"a+",             "b",              1},
  {"ab+c",           "abbbc",          1},
  {"ab?c",           "ac",             1},
  {"ab?c",           "abc",            1},
  {"colou?r",        "color",          1},
  {"colou?r",        "colour",         1},
  {"a+b+",           "aabbb",          1},

  /* ---- anchors ---- */
  {"^abc",           "abc",            0},
  {"^abc",           "xabc",           0},
  {"abc$",           "abc",            0},
  {"abc$",           "abcx",           0},
  {"^$",             "",               0},
  {"^a*$",           "aaa",            0},
  {"^foo",           "foobar",         0},
  {"bar$",           "foobar",         0},
  {"^(.*)$",         "whole line",     1},
  {"^\\(.*\\)$",     "whole line",     0},

  /* ---- character classes ---- */
  {"[abc]",          "cat",            0},
  {"[abc]*",         "cabbage",        0},
  {"[a-z]",          "Hello",          0},
  {"[a-z]*",         "hello123",       0},
  {"[A-Z]",          "hello World",    0},
  {"[0-9]",          "abc7def",        0},
  {"[0-9]*",         "12345abc",       0},
  {"[^0-9]",         "123x456",        0},
  {"[^abc]*",        "xyzabc",         0},
  {"[a-zA-Z]*",      "Hello99",        0},
  {"[[:digit:]]*",   "42abc",          0},
  {"[[:alpha:]]+",   "abc123",         1},
  {"[.,;]",          "a;b",            0},
  {"[]a]",           "]",              0},

  /* ---- alternation (ERE) ---- */
  {"foo|bar",        "a bar b",        1},
  {"foo|bar",        "a foo b",        1},
  {"cat|dog|bird",   "i have a dog",   1},
  {"(foo|bar)baz",   "foobaz",         1},
  {"(foo|bar)baz",   "barbaz",         1},
  {"a|b|c",          "xbz",            1},
  {"(ab|a)(b)?",     "ab",             1},
  {"x(a|b|c)*y",     "xabcy",          1},
  {"(red|green|blue)", "the blue sky", 1},
  {"^(yes|no)$",     "yes",            1},

  /* ---- groups & submatch offsets ---- */
  {"\\(a\\)\\(b\\)", "ab",             0},
  {"\\(a\\)\\(b\\)\\(c\\)", "abc",     0},
  {"(a)(b)(c)",      "abc",            1},
  {"\\(abc\\)*",     "abcabcabc",      0},
  {"\\([0-9]*\\)",   "year2024",       0},
  {"key=\\(.*\\)",   "key=value",      0},
  {"\\(.*\\)=\\(.*\\)", "a=b=c",       0},
  {"([a-z]+)@([a-z]+)", "user@host",   1},
  {"<(.*)>",         "<tag>",          1},
  {"\"(.*)\"",       "say \"hi\" now", 1},
  {"\\(.\\)\\(.\\)\\(.\\)", "xyz",     0},
  {"version ([0-9]+)", "version 7",    1},

  /* ---- nested & optional groups ---- */
  {"((a)(b))",       "ab",             1},
  {"(a(b(c)))",      "abc",            1},
  {"(a+)+",          "aaa",            1},
  {"(ab)*",          "ababab",         1},
  {"(a|(b))c",       "bc",             1},
  {"((foo)?bar)",    "bar",            1},
  {"((foo)?bar)",    "foobar",         1},
  {"(x(y)?z)",       "xz",             1},

  /* ---- intervals {n,m} (ERE) ---- */
  {"a{3}",           "aaaa",           1},
  {"a{2,4}",         "aaaaa",          1},
  {"a{2,}",          "aaaaa",          1},
  {"a{0,2}",         "aaa",            1},
  {"[0-9]{4}",       "year 2024 ad",   1},
  {"(ab){2}",        "ababab",         1},
  {"[a-f]{1,3}",     "abcdef",         1},
  {"x{2,3}y",        "xxxy",           1},
  {".{5}",           "abcdefg",        1},
  {"a{1}b{1}",       "ab",             1},

  /* ---- backreferences (BRE) ---- */
  {"\\(a\\)\\1",     "aa",             0},
  {"\\(ab\\)\\1",    "abab",           0},
  {"\\(.\\)\\1",     "xx",             0},
  {"\\(.\\)\\1",     "xy",             0},
  {"\\(abc\\)\\1",   "abcabc",         0},
  {"\\(a*\\)b\\1",   "aabaa",          0},

  /* ---- greedy / leftmost-longest semantics ---- */
  {"a.*a",           "abracadabra",    0},
  {"<.*>",           "<a><b><c>",      0},
  {"\".*\"",         "\"x\" \"y\"",    0},
  {"[0-9]*",         "",               0},
  {"(a*)(a*)",       "aaaa",           1},
  {"(a+)(a+)",       "aaaa",           1},
  {".*\\(end\\)",    "the end end",    0},
  {"\\(.*\\) \\(.*\\)", "one two three", 0},
  {"(.+)(.+)",       "ab",             1},
  {"a*a*a*",         "aaa",            1},

  /* ---- empty / boundary cases ---- */
  {"",               "",               0},
  {"a?",             "",               1},
  {"(a?)(b?)",       "",               1},
  {"^",              "abc",            0},
  {"$",              "abc",            0},
  {"\\(\\)",         "abc",            0},
  {"x*",             "yyy",            0},
  {"(|a)",           "a",              1},

  /* ---- real-world configure / sed / build patterns ---- */
  {".*\\.\\(.*\\)",  "conftest.o",     0},   /* autoconf OBJEXT */
  {".*\\.\\(.*\\)",  "conftest.obj",   0},
  {"\\(.*\\)\\.tab\\.c", "parser.tab.c", 0},
  {"-l\\(.*\\)",     "-lpthread",      0},
  {"\\([0-9]*\\)\\.\\([0-9]*\\)", "2.69", 0},  /* version split */
  {"^#define \\(.*\\)", "#define FOO 1", 0},
  {"lib\\(.*\\)\\.so", "libcurl.so",   0},
  {"\\(/[^/]*\\)*",  "/usr/local/bin", 0},
  {"[a-zA-Z_][a-zA-Z0-9_]*", "9var x", 0},     /* C identifier */
  {"^[ \t]*",        "   indented",    0},
  {"GNU.*[0-9]\\.[0-9]", "GNU bash 5.2", 0},
  {"\\$\\([A-Z]*\\)", "$HOME/bin",     0},
};

static int run_one(const struct tcase *tc, int verbose)
{
    regex_t sr, gr;
    regmatch_t sm[NSLOT], gm[NSLOT];
    int src, grc, sx, gx, i, ok = 1;

    src = sub_regcomp(&sr, tc->pat, tc->ere ? REG_EXTENDED : 0);
    grc = regcomp(&gr, tc->pat, tc->ere ? REG_EXTENDED : 0);

    /* Both must agree on compilability. */
    if ((src == 0) != (grc == 0)) {
        if (verbose) printf("FAIL [compile] /%s/ ere=%d  sub_rc=%d glibc_rc=%d\n",
                            tc->pat, tc->ere, src, grc);
        if (src == 0) sub_regfree(&sr);
        if (grc == 0) regfree(&gr);
        return 0;
    }
    if (src != 0) return 1;   /* both rejected the pattern: agree */

    for (i = 0; i < NSLOT; i++) { sm[i].rm_so = sm[i].rm_eo = -1;
                                  gm[i].rm_so = gm[i].rm_eo = -1; }
    sx = sub_regexec(&sr, tc->txt, NSLOT, sm, 0);
    gx = regexec(&gr, tc->txt, NSLOT, gm, 0);

    if ((sx == 0) != (gx == 0)) {
        ok = 0;
    } else if (sx == 0) {
        for (i = 0; i < NSLOT; i++)
            if (sm[i].rm_so != gm[i].rm_so || sm[i].rm_eo != gm[i].rm_eo)
                ok = 0;
    }
    if (!ok && verbose) {
        printf("FAIL /%s/ ere=%d on \"%s\"\n", tc->pat, tc->ere, tc->txt);
        printf("      sub  : match=%d", sx == 0);
        for (i = 0; i < NSLOT; i++) printf(" [%d,%d]", sm[i].rm_so, sm[i].rm_eo);
        printf("\n      glibc: match=%d", gx == 0);
        for (i = 0; i < NSLOT; i++) printf(" [%d,%d]", gm[i].rm_so, gm[i].rm_eo);
        printf("\n");
    }
    sub_regfree(&sr);
    regfree(&gr);
    return ok;
}

int main(int argc, char **argv)
{
    int verbose = (argc > 1 && strcmp(argv[1], "-v") == 0);
    int n = (int)(sizeof cases / sizeof cases[0]);
    int pass = 0, i;

    for (i = 0; i < n; i++)
        pass += run_one(&cases[i], 1);

    printf("\nlibregex torture: %d/%d points passed (vs host glibc)\n", pass, n);
    if (verbose) printf("(total cases compiled: %d)\n", n);
    return pass == n ? 0 : 1;
}
