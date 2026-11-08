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

    /* the substitution table holds what the ABI enters, and only that */
    { "Sa<char> is a new entry", "_Z1fSaIcES_", I, "f(std::allocator<char>, std::allocator<char>)" },
    { "and in a nested name", "_Z1fNSaIcE1xES_S0_", I,
      "f(std::allocator<char>::x, std::allocator<char>, std::allocator<char>::x)" },
    { "Sb<wchar_t> is too", "_Z1fSbIwES_", I,
      "f(std::basic_string<wchar_t>, std::basic_string<wchar_t>)" },
    { "a substitution as a prefix is no new entry",
      "_ZN4llvm10WriteGraphINS_11EdgeBundlesEEERNS_11raw_ostreamES3_RKT_bRKNS_5TwineE", I,
      "llvm::raw_ostream& llvm::WriteGraph<llvm::EdgeBundles>(llvm::raw_ostream&, "
      "llvm::EdgeBundles const&, bool, llvm::Twine const&)" },
    { "a template parameter as a type is an entry",
      "_ZN4llvm6detail27unwrapReferencedTypedefTypeINS_8DWARFDieEEET_S3_", I,
      "llvm::DWARFDie llvm::detail::unwrapReferencedTypedefType<llvm::DWARFDie>(llvm::DWARFDie)" },
    { "a parameter's entries stay where its text is",
      "_Z18startFromBeginningRmRN4llvm18DIExpressionCursorENS0_8ArrayRefImEE", I,
      "startFromBeginning(unsigned long&, llvm::DIExpressionCursor&, llvm::ArrayRef<unsigned long>)" },
    { "a function's own name is no entry", "_Z1fIL3Foo1EEvS_", I, "void f<(Foo)1>(f)" },

    /* a template function's return type goes before its own name */
    { "after a thunk's words", "_ZThn8_N3Foo3barIiEEvT_", I,
      "non-virtual thunk to void Foo::bar<int>(int) [offset -8]" },
    { "after a virtual thunk's", "_ZTv0_n24_N3Foo3barIiEEvT_", I,
      "virtual thunk to void Foo::bar<int>(int) [offsets 0, -24]" },
    { "with its entries", "_ZN3FooIiE3barIiEEPS0_T_", I, "Foo<int>* Foo<int>::bar<int>(int)" },

    /* an array of arrays: the outer bound first */
    { "two bounds", "_Z1fA2_A3_i", I, "f(int[2][3])" },
    { "three bounds", "_Z1fA2_A3_A4_i", I, "f(int[2][3][4])" },
    { "the element type's entry", "_Z1fA2_A3_iS_", I, "f(int[2][3], int[3])" },
    { "the whole array's entry", "_Z1fA2_A3_iS0_", I, "f(int[2][3], int[2][3])" },

    /* a pointer's or reference's declarator goes before an array's bounds,
     * and only an array's */
    { "a pointer to a template with an array argument", "_Z1fP3FooIA4_iE", I, "f(Foo<int[4]>*)" },
    { "a pointer to an abi-tagged class", "_Z1fP3FooB5cxx11", I, "f(Foo[abi:cxx11]*)" },
    { "a pointer to an array", "_Z1fPA4_i", I, "f(int (*)[4])" },
    { "a pointer to a pointer to an array", "_Z1fPPA4_i", I, "f(int (**)[4])" },
    { "an rvalue reference to an array", "_Z1fOA4_i", I, "f(int (&&)[4])" },
    { "a pointer to an array of arrays", "_Z1fPA2_A3_i", I, "f(int (*)[2][3])" },
    { "entries after the declarator", "_Z1fPA4_iS0_", I, "f(int (*)[4], int (*)[4])" },
    { "the array's entry without it", "_Z1fRA2_A3_iS0_", I, "f(int (&)[2][3], int[2][3])" },
    { "a reference to an array by substitution", "_Z1fA4_iRS_", I, "f(int[4], int (&)[4])" },
    { "to a pointer to one by substitution", "_Z1fPA4_iRS0_", I, "f(int (*)[4], int (*&)[4])" },
    { "to one by template parameter", "_Z1fIPA4_iEvPT_", I, "void f<int (*)[4]>(int (**)[4])" },

    /* a pack expansion: its pattern once per element of the pack it names
     * (GNU prints f<, int>(, int) for the empty pack before an argument) */
    { "an empty pack", "_Z1fIJEEvDpT_", I, "void f<>()" },
    { "a pack after an argument", "_Z1fIiJdcEEvT_DpT0_", I,
      "void f<int, double, char>(int, double, char)" },
    { "a pattern around the parameter", "_Z1fIJidEEvDpRKT_", I,
      "void f<int, double>(int const&, double const&)" },
    { "an empty pack after an argument", "_Z1fIiJEEvT_DpT0_", I, "void f<int>(int)" },
    { "an argument after a pack", "_Z1fIJicEiEvDpT_T0_", I, "void f<int, char, int>(int, char, int)" },
    { "an empty pack before an argument", "_Z1fIJEiEvDpT_T0_", I, "void f<int>(int)" },
    { "the pattern's entry is its last element", "_Z1fIJicEEvDpPT_S1_", I,
      "void f<int, char>(int*, char*, char*)" },

    /* DEMANGLE_RUST takes both of Rust's schemes */
    { "a legacy name", "_ZN3foo3bar17h0123456789abcdefE", R, "foo::bar" },
    { "a C++ name is no Rust one", "_ZN3foo3barEv", R, NULL },

    /* a Rust v0 disambiguator is no part of the name, and 64 bits wide:
     * a crate's is a hash (rustc 1.99's, for a crate lib) */
    { "a crate's disambiguator", "_RCs91N3l2zy1RL_3lib", R, "lib" },
    { "the smallest", "_RCs_3lib", R, "lib" },
    { "the largest, 2^64 - 1", "_RCslYGhA16ahyd_3lib", R, "lib" },
    { "one past it", "_RCslYGhA16ahye_3lib", R, NULL },
    { "a malformed one", "_RCs.3lib", R, NULL },

    /* numbers too big for what they are refused, not wrapped */
    { "a thunk offset at LLONG_MAX", "_ZThn9223372036854775807_N3Foo3barEv", I,
      "non-virtual thunk to Foo::bar() [offset -9223372036854775807]" },
    { "a thunk offset past LLONG_MAX", "_ZThn9223372036854775808_N3Foo3barEv", I, NULL },
    { "a virtual thunk's offset past it", "_ZTv0_n10000000000000000000_N3Foo3barEv", I, NULL },
    { "a D length past SIZE_MAX", "_D3foo__T3barVi18446744073709551619abcZ3bazFZv", D, NULL },
    { "a D number past SIZE_MAX", "_D3foo__T3barVi18446744073709551619_Z3bazFZv", D,
      "foo.bar!(int(18446744073709551619)).baz: void function()" },
    { "a Rust length past SIZE_MAX", "_ZN20496382304121724020foo17h0123456789abcdefE", R, NULL },

    /* a legacy Rust $u escape is a character, or the name is left as it
     * was mangled (as for one past U+10FFFF already, and as c++filt does) */
    { "an escape of a character", "_ZN3foo5$u41$17h0123456789abcdefE", DEMANGLE_AUTO, "foo::A" },
    { "an escape of the last character", "_ZN3foo9$u10ffff$17h0123456789abcdefE", DEMANGLE_AUTO,
      "foo::\xf4\x8f\xbf\xbf" },
    { "an escape of a UTF-16 surrogate", "_ZN3foo7$ud800$17h0123456789abcdefE", DEMANGLE_AUTO,
      "foo::$ud800$::h0123456789abcdef" },
    { "an escape past 32 bits", "_ZN3foo12$u100000041$17h0123456789abcdefE", DEMANGLE_AUTO,
      "foo::$u100000041$::h0123456789abcdef" },
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
