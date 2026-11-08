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
#define D  DEMANGLE_DLANG
#define I  DEMANGLE_ITANIUM

static const dm_case_t cases[] = {
    /* a Rust v0 backreference reaches only what came before its B */
    { "a path referring to itself", "_RB_", R, NULL },
    { "the same, no recursion limit", "_RB_", RN, NULL },
    { "a const referring to its own B", "_RIC0B_E", R, NULL },
    { "the same, no recursion limit", "_RIC0B_E", RN, NULL },
    { "a type referring to its own B", "_RINvC3foo3barBb_E", RN, NULL },
    { "a type referring forward", "_RINvC3foo3barBc_E", RN, NULL },

    /* a D back reference: Q and a base-26 distance back, to an identifier
     * or a type parsed again there (Phobos's writeln, as DMD mangles it) */
    { "identifier and type back references", "_D3std5stdio__T7writelnTAyaZQnFNfQjZv", D,
      "std.stdio.writeln!(immutable(char)[]).writeln: void function(immutable(char)[]) @safe" },
    { "a type back reference to a basic type", "_D3foo3barFQdZv", D,
      "foo.bar: void function(char)" },
    { "a back reference of distance 0", "_D3fooQaFZv", D, NULL },
    { "a back reference before the name", "_D3fooQzFZv", D, NULL },
    { "a type back reference to itself", "_D3foo3barFQaZv", D, NULL },
    { "a back reference too long to count", "_D3fooQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAaFZv", D, NULL },

    /* a literal of a type that is a substitution candidate keeps its type,
     * (Foo)1, where a substitution after it points */
    { "a substitution after a literal's type", "_Z1fIL3Foo1EEvS0_", I, "void f<(Foo)1>(Foo)" },
    { "a nested literal type's substitution", "_Z1fILN1A1BE3EEvS1_", I, "void f<(A::B)3>(A::B)" },
    { "inside a template argument", "_Z1fIN1AIL3Foo2EEEEvS1_", I, "void f<A<(Foo)2>>(Foo)" },
    { "the literal as a template parameter", "_Z1fIL3Foo1EEvT_", I, "void f<(Foo)1>((Foo)1)" },
    { "a builtin literal is its value", "_Z1fILi1EEvv", I, "void f<1>()" },

    /* numbers too big for what they are refused, not wrapped */
    { "a thunk offset at LLONG_MAX", "_ZThn9223372036854775807_N3Foo3barEv", I,
      "non-virtual thunk to Foo::bar() [offset -9223372036854775807]" },
    { "a thunk offset past LLONG_MAX", "_ZThn9223372036854775808_N3Foo3barEv", I, NULL },
    { "a virtual thunk's offset past it", "_ZTv0_n10000000000000000000_N3Foo3barEv", I, NULL },
    { "a D length past SIZE_MAX", "_D3foo__T3barVi18446744073709551619abcZ3bazFZv", D, NULL },
    { "a D number past SIZE_MAX", "_D3foo__T3barVi18446744073709551619_Z3bazFZv", D,
      "foo.bar!(int(18446744073709551619)).baz: void function()" },
    { "a Rust length past SIZE_MAX", "_ZN20496382304121724020foo17h0123456789abcdefE", R, NULL },
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
