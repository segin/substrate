/*
 * test_cases.c - libdemangle's results, symbol by symbol.
 *
 * Each case is a mangled name, the options, and what demangle() must
 * return: the exact string, or NULL where the name must be refused.
 * The C++ results agree with GNU c++filt's, but for spacing (libdemangle
 * writes int[2], not int [2]) and the offsets a thunk names; the Rust
 * results with rustc-demangle's alternate form, which leaves off the
 * hashes; the D ones with this library's own form.
 *
 * Usage: test_cases
 */
#include <demangle.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *what;       /* what the case shows */
    const char *mangled;
    unsigned options;
    const char *want;       /* NULL: refused */
} dm_case_t;

#define R  DEMANGLE_RUST
#define RN (DEMANGLE_RUST | DEMANGLE_NO_RECURSE_LIMIT)

static const dm_case_t cases[] = {
    /* a Rust v0 backreference reaches only what came before its B */
    { "a path referring to itself", "_RB_", R, NULL },
    { "the same, no recursion limit", "_RB_", RN, NULL },
    { "a const referring to its own B", "_RIC0B_E", R, NULL },
    { "the same, no recursion limit", "_RIC0B_E", RN, NULL },
    { "a type referring to its own B", "_RINvC3foo3barBb_E", RN, NULL },
    { "a type referring forward", "_RINvC3foo3barBc_E", RN, NULL },
};

int
main(void)
{
    size_t i;
    int failed = 0;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const dm_case_t *c = &cases[i];
        char *got = demangle(c->mangled, (int)c->options);

        if ((got == NULL) != (c->want == NULL) ||
            (got != NULL && strcmp(got, c->want) != 0)) {
            fprintf(stderr, "FAIL: %s: %s -> %s, want %s\n", c->what, c->mangled,
                got != NULL ? got : "NULL", c->want != NULL ? c->want : "NULL");
            failed++;
        }
        free(got);
    }
    if (failed != 0) {
        return 1;
    }
    printf("PASS: test_cases (%zu)\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
