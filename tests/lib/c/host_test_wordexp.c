/*
 * host_test_wordexp.c
 *
 * Verifies Substrate's wordexp(3) / wordfree(3) implementation from
 * lib/c/src/wordexp.c by linking that source directly and exercising the
 * documented expansions.  Self-contained — compiles standalone on the host
 * (against the host's glob/pwd) so no Substrate runtime is needed:
 *
 *     cc -O0 -g -o host_test_wordexp tests/lib/c/host_test_wordexp.c
 *     ./host_test_wordexp
 *
 * Substrate's <wordexp.h> and glibc's share the _WORDEXP_H include guard, so
 * including ours explicitly first suppresses the host's conflicting copy; the
 * impl's <glob.h>/<pwd.h> then resolve to the host's, which are interface-
 * compatible with what wordexp.c uses.
 *
 * Coverage:
 *   - literal words, IFS field splitting, leading/trailing/collapsed runs
 *   - parameter expansion $VAR / ${VAR}, greedy name matching, unset -> empty
 *   - quoting: '...' literal, "..." expands but no split, backslash escape
 *   - tilde expansion ~ and ~/path
 *   - $$ -> PID
 *   - error classes: WRDE_BADVAL (WRDE_UNDEF), WRDE_BADCHAR, WRDE_CMDSUB,
 *     WRDE_SYNTAX
 */

#include "../../../include/wordexp.h"   /* defines _WORDEXP_H -> suppresses host's */
#include <glob.h>
#include <pwd.h>
#include <unistd.h>
#include "../../../lib/c/src/wordexp.c"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, total = 0;

static void chk(const char *in, int flags, const char *expect)
{
    wordexp_t we;
    char got[1024] = "";
    int r;

    memset(&we, 0, sizeof we);
    r = wordexp(in, &we, flags);
    if (r == 0) {
        for (size_t i = 0; i < we.we_wordc; i++) {
            if (i) strcat(got, "|");
            strcat(got, we.we_wordv[we.we_offs + i]);
        }
    } else {
        snprintf(got, sizeof got, "<err %d>", r);
    }
    total++;
    if (strcmp(got, expect) != 0) {
        fails++;
        printf("FAIL  in=[%s] got=[%s] want=[%s]\n", in, got, expect);
    } else {
        printf("ok    in=[%s] => [%s]\n", in, got);
    }
    if (r == 0) wordfree(&we);
}

int main(void)
{
    char ex[64];

    setenv("FOO", "bar", 1);
    setenv("TWO", "a b", 1);
    setenv("FOOy", "Z", 1);
    setenv("MISSINGb", "M", 1);
    unsetenv("MISSING");
    setenv("HOME", "/home/tester", 1);

    chk("hello", 0, "hello");
    chk("a b c", 0, "a|b|c");
    chk("  spaced   out  ", 0, "spaced|out");
    chk("$FOO", 0, "bar");
    chk("x${FOO}y", 0, "xbary");
    chk("$FOOy", 0, "Z");                 /* greedy name: variable FOOy */
    chk("${FOO}baz", 0, "barbaz");
    chk("$TWO", 0, "a|b");                /* unquoted expansion splits */
    chk("\"$TWO\"", 0, "a b");            /* quoted expansion does not */
    chk("'$FOO'", 0, "$FOO");             /* single quotes literal */
    chk("\"$FOO\"", 0, "bar");            /* double quotes expand */
    chk("$MISSING", 0, "");               /* unset -> empty */
    chk("a${MISSING}b", 0, "ab");
    chk("~", 0, "/home/tester");
    chk("~/x", 0, "/home/tester/x");
    chk("a\\ b", 0, "a b");               /* escaped space: one word */
    chk("\"a   b\"", 0, "a   b");
    chk("$MISSING", WRDE_UNDEF, "<err 3>");  /* WRDE_BADVAL */
    chk("a|b", 0, "<err 2>");                /* WRDE_BADCHAR */
    chk("`cmd`", 0, "<err 4>");              /* WRDE_CMDSUB */
    chk("$(cmd)", 0, "<err 4>");
    chk("\"unbalanced", 0, "<err 5>");       /* WRDE_SYNTAX */

    snprintf(ex, sizeof ex, "lit%ld", (long)getpid());
    chk("lit$$", 0, ex);                     /* $$ -> PID */

    printf("\n%d/%d passed, %d failed\n", total - fails, total, fails);
    return fails ? 1 : 0;
}
