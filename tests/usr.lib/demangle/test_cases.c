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
    { "the expansion is an entry after it", "_Z1fIJicEEvDpPT_S2_", I,
      "void f<int, char>(int*, char*, int*, char*)" },

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

    /* a Rust v0 namespace says how a name prints: lower case as it is,
     * upper case braced with its number (rustc 1.99's symbols, and GNU's) */
    { "a value", "_RNvCs91N3l2zy1RL_3lib14takes_dyn_iter", R, "lib::takes_dyn_iter" },
    { "a type's value", "_RNvNtCs_3lib3Bar3baz", R, "lib::Bar::baz" },
    { "a value named like a shim", "_RNvCsaVUCjkH8HsD_7___rustc35___rust_no_alloc_shim_is_unstable_v2", R,
      "__rustc::__rust_no_alloc_shim_is_unstable_v2" },
    { "a value named closure", "_RNvCshgxSpmajvKg_7mycrate7closure", R, "mycrate::closure" },
    { "a closure", "_RNCNvCs91N3l2zy1RL_3lib9instances0", R, "lib::instances::{closure#0}" },
    { "the second closure", "_RNCCs_3libs_0", R, "lib::{closure#1}" },
    { "a named closure", "_RNCCs_3libs0_3foo", R, "lib::{closure:foo#2}" },
    { "a shim", "_RNSCs_3lib6vtable", R, "lib::{shim:vtable#0}" },
    { "another namespace", "_RNXCs_3lib1x", R, "lib::{X:x#0}" },
    { "no namespace", "_RN0Cs_3lib1x", R, NULL },

    /* a Rust v0 instance's instantiating crate is part of it, unprinted */
    { "by back reference", "_RINvCs91N3l2zy1RL_3lib22takes_dyn_iter_generichEB2_", R,
      "lib::takes_dyn_iter_generic::<u8>" },
    { "by crate root", "_RINvNtNtNtCs4bQBTqW3ynz_4core4iter7sources5empty5emptyhECs91N3l2zy1RL_3lib",
      R, "core::iter::sources::empty::empty::<u8>" },
    { "two of them", "_RINvCs_3lib1fhEB2_B2_", R, NULL },
    { "a back reference there is not followed", "_RINvCs_3lib1fhEB9_", R, "lib::f::<u8>" },
    { "nor need it point back", "_RINvCs_3lib1fhEBz_", R, "lib::f::<u8>" },
    { "but it must be one", "_RINvCs_3lib1fhEB.", R, NULL },

    /* a Rust v0 value's generic arguments take the turbofish, a type's not */
    { "a type's in a value's", "_RINvNtCs4bQBTqW3ynz_4core3ptr9drop_glueINtNtCskDOlpTX1PlC_5alloc7raw_vec6RawVechEECs91N3l2zy1RL_3lib",
      R, "core::ptr::drop_glue::<alloc::raw_vec::RawVec<u8>>" },
    { "an impl's trait", "_RNvXCs_3libINtB2_3GenhENtB2_5Trait3baz", R, "<lib::Gen<u8> as lib::Trait>::baz" },
    { "a value inside a type's argument", "_RINvCs_3lib1fINtB2_3FooKINvB2_1ghEEE", R, NULL },

    /* a Rust v0 impl path may have a disambiguator (rustc 1.99's symbols) */
    { "the first inherent impl", "_RNvMCs91N3l2zy1RL_3libNtB2_3Bar3one", R, "<lib::Bar>::one" },
    { "the second", "_RNvMs_Cs91N3l2zy1RL_3libNtB4_3Bar3two", R, "<lib::Bar>::two" },
    { "a trait impl", "_RNvXs0_Cs91N3l2zy1RL_3libNtB5_3BarNtB5_5Trait3baz", R,
      "<lib::Bar as lib::Trait>::baz" },
    { "a generic one", "_RNvXs1_Cs91N3l2zy1RL_3libINtB5_3GenhENtB5_5Trait3bazB5_", R,
      "<lib::Gen<u8> as lib::Trait>::baz" },
    { "a malformed one", "_RNvMs.Cs_3libNtB4_3Bar3two", R, NULL },

    /* a Rust v0 dyn type's erased lifetime bound is not printed */
    { "dyn with the erased lifetime", "_RINvCs_3lib2idODNtB2_4SendEL_E", R, "lib::id::<*mut dyn lib::Send>" },
    { "two traits", "_RINvCs_3lib2idODNtB2_4SendNtB2_4SyncEL_E", R, "lib::id::<*mut dyn lib::Send + lib::Sync>" },

    /* a Rust v0 dyn trait's associated-type bindings */
    { "a binding", "_RINvCs_3lib2idODNtB2_8Iteratorp4ItemlEL_E", R,
      "lib::id::<*mut dyn lib::Iterator<Item = i32>>" },
    { "two", "_RINvCs_3lib2idODNtB2_5Traitp1Ahp1BlEL_E", R, "lib::id::<*mut dyn lib::Trait<A = u8, B = i32>>" },
    { "among the trait's own arguments", "_RINvCs_3lib2idODINtB2_2FnThEEp6OutputhEL_E", R,
      "lib::id::<*mut dyn lib::Fn<(u8,), Output = u8>>" },
    { "with another trait after", "_RINvCs_3lib2idODNtB2_8Iteratorp4ItemhNtB2_4SendEL_E", R,
      "lib::id::<*mut dyn lib::Iterator<Item = u8> + lib::Send>" },
    { "rustc's", "_RINvCs91N3l2zy1RL_3lib2idODNtNtNtNtCs4bQBTqW3ynz_4core4iter6traits8iterator8Iteratorp4ItemlEL_EB2_", R,
      "lib::id::<*mut dyn core::iter::traits::iterator::Iterator<Item = i32>>" },
    { "a binding with no type", "_RINvCs_3lib2idODNtB2_8Iteratorp4ItemEL_E", R, NULL },

    /* a Rust v0 function type's ABI (rustc 1.99's symbols) */
    { "extern \"C\"", "_RINvCs91N3l2zy1RL_3lib2idFKCmEmEB2_", R, "lib::id::<extern \"C\" fn(u32) -> u32>" },
    { "extern \"system\"", "_RINvCs91N3l2zy1RL_3lib2idFK6systemmEmEB2_", R,
      "lib::id::<extern \"system\" fn(u32) -> u32>" },
    { "a - in the ABI's name", "_RINvCs91N3l2zy1RL_3lib2idFK8C_unwindEuEB2_", R,
      "lib::id::<extern \"C-unwind\" fn()>" },
    { "and unsafe", "_RINvCs91N3l2zy1RL_3lib2idFUK6systemEuEB2_", R,
      "lib::id::<unsafe extern \"system\" fn()>" },
    { "no ABI", "_RINvCs_3lib2idFmEmE", R, "lib::id::<fn(u32) -> u32>" },
    { "an ABI that is no identifier", "_RINvCs_3lib2idFK.EuE", R, NULL },

    /* a Rust v0 binder, for<'a>, is printed, and names its lifetimes */
    { "rustc's", "_RINvCs91N3l2zy1RL_3lib2idFG_RL0_hERL0_hEB2_", R,
      "lib::id::<for<'a> fn(&'a u8) -> &'a u8>" },
    { "two lifetimes, innermost first", "_RINvCs_3lib2idFG0_RL0_hRL1_hEuE", R,
      "lib::id::<for<'a, 'b> fn(&'b u8, &'a u8)>" },
    { "a binder inside another", "_RINvCs_3lib2idFG_FG_RL0_hRL1_hEuEuE", R,
      "lib::id::<for<'a> fn(for<'b> fn(&'b u8, &'a u8))>" },
    { "a dyn type's", "_RINvCs_3lib2idODG_INtB2_5TraitL0_EEL_E", R,
      "lib::id::<*mut dyn for<'a> lib::Trait<'a>>" },
    { "a lifetime bound by none", "_RINvCs_3lib2idRL0_hE", R, NULL },
    { "one past the binder's", "_RINvCs_3lib2idFG_RL1_hEuE", R, NULL },
    { "a binder of too many", "_RINvCs_3lib2idFGzzzzzz_EuE", R, NULL },

    /* a Rust v0 reference's erased lifetime is not printed */
    { "a reference", "_RINvCs_3lib2idRL_hE", R, "lib::id::<&u8>" },
    { "a mutable one", "_RINvCs_3lib2idQL_hE", R, "lib::id::<&mut u8>" },
    { "one with a lifetime", "_RINvCs_3lib2idFG_QL0_hEuE", R, "lib::id::<for<'a> fn(&'a mut u8)>" },

    /* D takes DEMANGLE_NO_PARAMS and DEMANGLE_NO_VERBOSE */
    { "no parameters", "_D3foo3barFiZv", D | DEMANGLE_NO_PARAMS, "foo.bar" },
    { "nor a variable's type", "_D3foo1xi", D | DEMANGLE_NO_PARAMS, "foo.x" },
    { "a template's arguments stay", "_D3std5stdio__T7writelnTAyaZQnFNfQjZv", D | DEMANGLE_NO_PARAMS,
      "std.stdio.writeln!(immutable(char)[]).writeln" },
    { "no attributes", "_D3std5stdio__T7writelnTAyaZQnFNfQjZv", D | DEMANGLE_NO_VERBOSE,
      "std.stdio.writeln!(immutable(char)[]).writeln: void function(immutable(char)[])" },
    { "nor a delegate's", "_D3foo1xDFNaNbZv", D | DEMANGLE_NO_VERBOSE, "foo.x: void delegate()" },
    { "and with them", "_D3foo1xDFNaNbZv", D, "foo.x: void delegate() pure nothrow" },
    { "a malformed type is still refused", "_D3foo3barFiZ", D | DEMANGLE_NO_PARAMS, NULL },

    /* a Rust v0 const is printed as its type says (an array's length here;
     * GNU prints past 64 bits 0x0000000000000000_, rustc-demangle as here) */
    { "an integer in decimal", "_RINvCs91N3l2zy1RL_3lib2idAhj5_EB2_", R, "lib::id::<[u8; 5]>" },
    { "0 is no bool", "_RINvCs_3lib2idAhj0_E", R, "lib::id::<[u8; 0]>" },
    { "nor 1", "_RINvCs_3lib2idAhj1_E", R, "lib::id::<[u8; 1]>" },
    { "the largest", "_RINvCs_3lib2idAhjffffffffffffffff_E", R, "lib::id::<[u8; 18446744073709551615]>" },
    { "past 64 bits, in hex", "_RINvCs_3lib2idAhj10000000000000000_E", R, "lib::id::<[u8; 0x10000000000000000]>" },
    { "an unsigned one is never negative", "_RINvCs_3lib2idAhjn5_E", R, NULL },
    { "a bool is true or false, length or not", "_RINvCs_3lib2idAhb1_E", R, "lib::id::<[u8; true]>" },

    /* a Rust v0 generic argument is told by its first letter: K a const
     * (rustc 1.99's const generics), L a lifetime, anything else a type */
    { "a usize", "_RINvCs91N3l2zy1RL_3lib2cuKj4_EB2_", R, "lib::cu::<4>" },
    { "a negative i32", "_RINvCs91N3l2zy1RL_3lib2ciKln7_EB2_", R, "lib::ci::<-7>" },
    { "a bool", "_RINvCs91N3l2zy1RL_3lib2cbKb1_EB2_", R, "lib::cb::<true>" },
    { "a char", "_RINvCs91N3l2zy1RL_3lib2ccKc61_EB2_", R, "lib::cc::<'a'>" },
    { "a newline", "_RINvCs91N3l2zy1RL_3lib2ccKca_EB2_", R, "lib::cc::<'\\n'>" },
    { "beyond ASCII", "_RINvCs91N3l2zy1RL_3lib2ccKce9_EB2_", R, "lib::cc::<'\\u{e9}'>" },
    { "after a type", "_RINvCs91N3l2zy1RL_3lib3arrhKj3_EB2_", R, "lib::arr::<u8, 3>" },
    { "a const placeholder", "_RINvCs_3lib1fKpE", R, "lib::f::<_>" },
    { "a K with no const", "_RINvCs_3lib1fKE", R, NULL },
    { "a lifetime", "_RINvCs_3lib1fL_E", R, "lib::f::<'_>" },

    /* a Rust v0 identifier beyond ASCII is Punycode, _ its delimiter
     * (rustc 1.99's symbols) */
    { "an ASCII part and one character", "_RNvNtCs91N3l2zy1RL_3lib3uniu8gdel_5qa", R, "lib::uni::gödel" },
    { "no ASCII part", "_RNvNtCs91N3l2zy1RL_3lib3uniu7_1lqs71d", R, "lib::uni::東京" },
    { "a _ in the ASCII part", "_RNvNtCs91N3l2zy1RL_3lib3uniu8__x_y_fqa", R, "lib::uni::ñ_x_y" },
    { "two characters", "_RNvNtCs91N3l2zy1RL_3lib3uniu13caf_crme_60ag", R, "lib::uni::café_crème" },
    { "one alone", "_RNvNtCs91N3l2zy1RL_3lib3uniu3tda", R, "lib::uni::ü" },
    { "a type and its method", "_RNvMNtCs91N3l2zy1RL_3lib3uniNtB2_u9Gre_6ka8iu6ma_hia", R, "<lib::uni::Größe>::maß" },
    { "a delta past U+10FFFF", "_RNvCs_3libu6zzzzzz", R, NULL },

    /* an Itanium bool literal is false or true (libstdc++'s, LLVM's) */
    { "false", "_ZNSt10moneypunctIcLb0EED0Ev", I, "std::moneypunct<char, false>::~moneypunct()" },
    { "true", "_ZGVN5boost4asio3ssl6detail12openssl_initILb1EE9instance_E", I,
      "guard variable for boost::asio::ssl::detail::openssl_init<true>::instance_" },
    { "several", "_Z1fILb1ELb0ELb1EEvv", I, "void f<true, false, true>()" },
    { "any other value keeps its type", "_Z1fILb2EEvv", I, "void f<(bool)2>()" },

    /* any other Itanium literal is printed by its type, as GNU's */
    { "unsigned", "_ZTIN4llvm12instrumentor13InstructionIOIJLj32EEEE", I, "typeinfo for llvm::instrumentor::InstructionIO<32u>" },
    { "long", "_Z1fILl8EEvv", I, "void f<8l>()" },
    { "unsigned long", "_Z1fILm8EEvv", I, "void f<8ul>()" },
    { "long long, negative", "_Z1fILxn8EEvv", I, "void f<-8ll>()" },
    { "unsigned long long", "_Z1fILy8EEvv", I, "void f<8ull>()" },
    { "a short keeps its type", "_Z1fILt7EEvv", I, "void f<(unsigned short)7>()" },
    { "a char", "_Z1fILc65EEvv", I, "void f<(char)65>()" },
    { "a float its encoding", "_Z1fILf3f800000EEvv", I, "void f<(float)[3f800000]>()" },
    { "a double", "_Z1fILd3ff0000000000000EEvv", I, "void f<(double)[3ff0000000000000]>()" },
    { "an int its value alone", "_Z1fILin1EEvv", I, "void f<-1>()" },

    /* an Itanium local name: its encoding ends at its E */
    { "a function's variable", "_ZZ4mainvE1x", I, "main()::x" },
    { "with no parameters", "_ZZ4mainE1x", I, "main::x" },
    { "its guard variable", "_ZGVZ4mainvE1x", I, "guard variable for main()::x" },
    { "a local class's member", "_ZZ4mainvEN1S1fEv", I, "main()::S::f()" },
    { "local in local", "_ZZZ1fvEN1S1gEvE1y", I, "f()::S::g()::y" },
    { "a discriminator", "_ZZ4mainvE1x_0", I, "main()::x" },
    { "a long one", "_ZZ4mainvE1x__12_", I, "main()::x" },
    { "a string literal", "_ZZ4mainvEs", I, "main()::string literal" },
    { "a const member function's", "_ZZNK1A1fEvE1x", I, "A::f() const::x" },
    { "a default argument's", "_ZZ1fiEd_1x", I, "f(int)::{default arg#1}::x" },
    { "the one before it", "_ZZ1fiEd0_1x", I, "f(int)::{default arg#2}::x" },

    /* an Itanium lambda or unnamed type is numbered from #1, as GNU's */
    { "a lambda's typeinfo (LLVM's)", "_ZTIZNK4llvm14VPPhiAccessors15incoming_blocksEvEUlmE_", I,
      "typeinfo for llvm::VPPhiAccessors::incoming_blocks() const::{lambda(unsigned long)#1}" },
    { "no parameters", "_ZTIZ4mainvEUlvE_", I, "typeinfo for main()::{lambda()#1}" },
    { "the second", "_ZZ4mainvEUlvE0_", I, "main()::{lambda()#2}" },
    { "its call operator", "_ZZ4mainvENKUlvE_clEv", I, "main()::{lambda()#1}::operator()() const" },
    { "as a template argument", "_ZN1A1gIZNS_1fEvEUlvE_EEvT_", I,
      "void A::g<A::f()::{lambda()#1}>(A::f()::{lambda()#1})" },
    { "an unnamed type", "_ZN1AUt_E", I, "A::{unnamed type#1}" },
    { "the second", "_ZN1AUt0_E", I, "A::{unnamed type#2}" },

    /* the Itanium assignment and logical operator names */
    { "operator= (Qt's)", "_ZN4QUrlaSERKS_", I, "QUrl::operator=(QUrl const&)" },
    { "operator+=", "_ZN1ApLERKS_", I, "A::operator+=(A const&)" },
    { "operator<<=", "_ZN1AlSEi", I, "A::operator<<=(int)" },
    { "operator>>=", "_ZN1ArSEi", I, "A::operator>>=(int)" },
    { "operator%=", "_ZN1ArMEi", I, "A::operator%=(int)" },
    { "operator&&", "_ZN1AaaEi", I, "A::operator&&(int)" },
    { "operator||", "_ZN1AooEi", I, "A::operator||(int)" },
    { "operator co_await", "_ZN1AawEv", I, "A::operator co_await()" },

    /* the Itanium builtin types GNU names */
    { "char8_t (libstdc++'s)", "_ZTIDu", I, "typeinfo for char8_t" },
    { "_Float16", "_ZTIDF16_", I, "typeinfo for _Float16" },
    { "_Float32x", "_ZTIPDF32x", I, "typeinfo for _Float32x*" },
    { "_Float128", "_ZSt8to_charsPcS_DF128_", I, "std::to_chars(char*, char*, _Float128)" },
    { "std::bfloat16_t", "_ZTIDF16b", I, "typeinfo for std::bfloat16_t" },
    { "nullptr's type", "_Z1fDn", I, "f(decltype(nullptr))" },

    /* the Itanium special names GNU knows */
    { "a transaction clone (libstdc++'s)", "_ZGTtNSt11logic_errorD1Ev", I,
      "transaction clone for std::logic_error::~logic_error()" },
    { "a non-transaction clone", "_ZGTnNSt11logic_errorD1Ev", I, "non-transaction clone for std::logic_error::~logic_error()" },
    { "a TLS init function", "_ZTHN1A1xE", I, "TLS init function for A::x" },
    { "a TLS wrapper function", "_ZTW1x", I, "TLS wrapper function for x" },
    { "a hidden alias", "_ZGA1f", I, "hidden alias for f" },
    { "a construction vtable", "_ZTC1B8_1A", I, "construction vtable for A-in-B" },
    { "a module's initializer (libstdc++'s)", "_ZGIW3stdW6compat", I, "initializer for module std.compat" },
    { "a T that is none of them", "_ZTX1x", I, NULL },

    /* the Itanium standard abbreviations, in full as GNU c++filt prints them */
    { "std::ostream (Boost's)", "_ZN5boost10test_tools9tt_detail15print_log_valueIPKcEclERSoS4_", I,
      "boost::test_tools::tt_detail::print_log_value<char const*>::operator()(std::basic_ostream<char, "
      "std::char_traits<char>>&, char const*)" },
    { "std::string", "_Z1fSs", I, "f(std::basic_string<char, std::char_traits<char>, std::allocator<char>>)" },
    { "a member of it", "_ZNSs4sizeEv", I, "std::basic_string<char, std::char_traits<char>, std::allocator<char>>::size()" },
    { "its destructor", "_ZNSsD1Ev", I,
      "std::basic_string<char, std::char_traits<char>, std::allocator<char>>::~basic_string()" },
    { "std::allocator's constructor", "_ZNSaIcEC1ERKS_", I, "std::allocator<char>::allocator(std::allocator<char> const&)" },
    { "std::basic_iostream's", "_ZNSdC2Ev", I, "std::basic_iostream<char, std::char_traits<char>>::basic_iostream()" },
    { "short, if not verbose", "_Z1fSs", I | DEMANGLE_NO_VERBOSE, "f(std::string)" },
    { "but a constructor's in full", "_ZNSoC1Ev", I | DEMANGLE_NO_VERBOSE,
      "std::basic_ostream<char, std::char_traits<char>>::basic_ostream()" },

    /* an Itanium pointer or reference to a function: void (*)(int) */
    { "a pointer (Qt's)", "_ZN16QLoggingCategory13installFilterEPFvPS_E", I,
      "QLoggingCategory::installFilter(void (*)(QLoggingCategory*))" },
    { "a reference", "_Z1fRFviE", I, "f(void (&)(int))" },
    { "a pointer to one", "_Z1fPPFviE", I, "f(void (**)(int))" },
    { "by substitution", "_Z1fPFviES0_", I, "f(void (*)(int), void (*)(int))" },
    { "a reference to one by substitution", "_Z1fPFviERS0_", I, "f(void (*)(int), void (*&)(int))" },
    { "to the function by substitution", "_Z1fPFviERS_", I, "f(void (*)(int), void (&)(int))" },
    { "by template parameter", "_Z1fIFviEEvPT_", I, "void f<void (int)>(void (*)(int))" },

    /* the function an Itanium local name is in has no return type printed */
    { "a static in a template function", "_ZZN1A1fIiEEPiT_E1x", I, "A::f<int>(int)::x" },
    { "its guard variable", "_ZGVZN1A1fIiEEPiT_E1x", I, "guard variable for A::f<int>(int)::x" },
    { "a lambda's call operator", "_ZZ1fIiEvvENKUlvE_clEv", I, "f<int>()::{lambda()#1}::operator()() const" },
    { "as a template argument", "_ZN1B1gIZN1A1fIiEEvvEUlvE_EEvT_", I,
      "void B::g<A::f<int>()::{lambda()#1}>(A::f<int>()::{lambda()#1})" },
    { "but the outer function's is", "_ZN1B1gIZN1A1fIiEEvvEUlvE_EEPiT_", I,
      "int* B::g<A::f<int>()::{lambda()#1}>(A::f<int>()::{lambda()#1})" },

    /* an Itanium pointer to member: the class in the declarator */
    { "to a member function (LLVM's)", "_ZN4llvm11logicalview18LVScopeCompileUnit28processRangeLocationCoverageEMNS0_10LVLocationEFbvE", I,
      "llvm::logicalview::LVScopeCompileUnit::processRangeLocationCoverage(bool (llvm::logicalview::LVLocation::*)())" },
    { "to a const one", "_Z1fM1AKFvvE", I, "f(void (A::*)() const)" },
    { "to data", "_Z1fM1Ai", I, "f(int A::*)" },
    { "to const data", "_Z1fM1AKi", I, "f(int const A::*)" },
    { "to an array", "_Z1fM1AA4_i", I, "f(int (A::*)[4])" },
    { "a reference to one by template parameter", "_ZN4llvm13map_to_vectorIRNS_11SmallVectorIPNS_5ValueELj4EEEMS2_KFPNS_4TypeEvEEEDaOT_OT0_", I,
      "auto llvm::map_to_vector<llvm::SmallVector<llvm::Value*, 4u>&, llvm::Type* (llvm::Value::*)() const>"
      "(llvm::SmallVector<llvm::Value*, 4u>&, llvm::Type* (llvm::Value::*&&)() const)" },

    /* a template operator< or operator<< has a space before its arguments */
    { "operator<< (LLVM's)", "_ZN4llvm4PBQPlsINS_11raw_ostreamEEERT_S4_RKNS0_6VectorE", I,
      "llvm::raw_ostream& llvm::PBQP::operator<< <llvm::raw_ostream>(llvm::raw_ostream&, llvm::PBQP::Vector const&)" },
    { "operator<", "_ZltIiEbRK1AIT_ES4_", I, "bool operator< <int>(A<int> const&, A<int> const&)" },
    { "operator> needs none", "_ZgtIiEbRK1AIT_ES4_", I, "bool operator><int>(A<int> const&, A<int> const&)" },

    /* an Itanium array's qualifier is its elements', before its bounds */
    { "a const array", "_Z1fKA4_i", I, "f(int const[4])" },
    { "a reference to one", "_Z1fRKA22_c", I, "f(char const (&)[22])" },
    { "a reference to a volatile one", "_Z1fRVA2_A3_i", I, "f(int volatile (&)[2][3])" },
    { "by template parameter", "_Z1fIA22_cEvRKT_", I, "void f<char[22]>(char const (&)[22])" },
    { "by substitution", "_Z1fA4_iRKS_", I, "f(int[4], int const (&)[4])" },
    { "a const function's pointer", "_Z1fPKFviE", I, "f(void (*)(int) const)" },

    /* GCC's unified and comdat constructors and destructors */
    { "C4", "_ZN1AC4Ev", I, "A::A()" },
    { "D4", "_ZN1AD4Ev", I, "A::~A()" },
    { "C5", "_ZN1AC5Ev", I, "A::A()" },
    { "D5", "_ZN1AD5Ev", I, "A::~A()" },
    { "a local static in one (LLVM's)", "_ZGVZN4llvm3cas6ondisk19InternalRefArrayRefC4ESt9nullopt_tE11PlaceHolder", I,
      "guard variable for llvm::cas::ondisk::InternalRefArrayRef::InternalRefArrayRef(std::nullopt_t)::PlaceHolder" },
    { "no D3", "_ZN1AD3Ev", I, NULL },

    /* an Itanium entity as a template argument, L _Z <encoding> E */
    { "a variable", "_Z1fIL_Z1xEEvv", I, "void f<x>()" },
    { "a member (LLVM's)", "_ZN4llvm6detail9IEEEFloat17initFromIEEEAPIntIL_ZNS_11APFloatBase9semBFloatEEEEvRKNS_5APIntE", I,
      "void llvm::detail::IEEEFloat::initFromIEEEAPInt<llvm::APFloatBase::semBFloat>(llvm::APInt const&)" },
    { "a function", "_Z1fIL_Z1gvEEvv", I, "void f<g()>()" },
    { "a template function", "_Z1fIL_Z1gIiEvT_EEvv", I, "void f<void g<int>(int)>()" },
    { "with no E", "_Z1fIL_Z1x", I, NULL },

    /* an Itanium noexcept function type, Do F...E */
    { "a pointer to one", "_Z1fPDoFviE", I, "f(void (*)(int) noexcept)" },
    { "a reference", "_Z1fRDoFviE", I, "f(void (&)(int) noexcept)" },
    { "by substitution, one entry", "_Z1fPDoFviES_", I, "f(void (*)(int) noexcept, void (int) noexcept)" },
    { "a member function's", "_Z1fM1ADoFvvE", I, "f(void (A::*)() noexcept)" },
    { "as a template argument (LLVM's)",
      "_ZN4llvm8erase_ifINSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEPDoFiiEEEvRT_T0_", I,
      "void llvm::erase_if<std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>, int (*)(int) noexcept>"
      "(std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>>&, int (*)(int) noexcept)" },

    /* an Itanium data-member prefix, M: a lambda in a member's initializer */
    { "the lambda", "_ZN1A1BMUlvE_E", I, "A::B::{lambda()#1}" },
    { "its call operator", "_ZNK1A1BMUlvE_clEv", I, "A::B::{lambda()#1}::operator()() const" },
    { "its typeinfo (LLVM's)", "_ZTIN4llvm12dwarf_linker8parallel15DWARFLinkerImpl11LinkContext16getUnitForOffsetMUlmE_E", I,
      "typeinfo for llvm::dwarf_linker::parallel::DWARFLinkerImpl::LinkContext::getUnitForOffset::{lambda(unsigned long)#1}" },
    { "an M with nothing after it", "_ZN1AME", I, NULL },

    /* an Itanium template parameter as a nested name's prefix, T::B */
    { "a member type of one", "_Z1fI1AEvNT_1BE", I, "void f<A>(A::B)" },
    { "the parameter is an entry", "_Z1fI1AEvNT_1BES1_", I, "void f<A>(A::B, A)" },
    { "and the prefix after it", "_Z1fI1AEvNT_1B1CES2_", I, "void f<A>(A::B::C, A::B)" },
    { "a return type (LLVM's)", "_ZN4llvm17JumpThreadingPass19runExternalAnalysisINS_22BlockFrequencyAnalysisEEEPNT_6ResultEv", I,
      "llvm::BlockFrequencyAnalysis::Result* llvm::JumpThreadingPass::runExternalAnalysis<llvm::BlockFrequencyAnalysis>()" },

    /* Itanium expressions as template arguments, X <expression> E */
    { "a template parameter (LLVM's)", "_ZN4llvmeqIjLj4ELj4ESt4lessIjEEEbRKNS_8SmallSetIT_XT0_ET2_EERKNS3_IS4_XT1_ES5_EE", I,
      "bool llvm::operator==<unsigned int, 4u, 4u, std::less<unsigned int>>(llvm::SmallSet<unsigned int, 4u, "
      "std::less<unsigned int>> const&, llvm::SmallSet<unsigned int, 4u, std::less<unsigned int>> const&)" },
    { "a member's address", "_Z1fIXadL_ZN1A1gEvEEEvv", I, "void f<&A::g>()" },
    { "a const member's, parenthesized", "_ZTIN4llvm9sandboxir13GenericSetterIXadL_ZNKS0_10CondBrInst12getConditionEvEEXadL_ZNS2_12setConditionEPNS0_5ValueEEEEE", I,
      "typeinfo for llvm::sandboxir::GenericSetter<&(llvm::sandboxir::CondBrInst::getCondition() const), "
      "&llvm::sandboxir::CondBrInst::setCondition>" },
    { "a variable's", "_Z1fIXadL_ZN1A1xEEEEvv", I, "void f<&A::x>()" },
    { "a free function's", "_Z1fIXadL_Z1gvEEEvv", I, "void f<&(g())>()" },
    { "a scoped name", "_ZN4llvm14DomTreeBuilder12ApplyUpdatesINS_17DominatorTreeBaseINS_10BasicBlockELb0EEEEEvRT_RNS_9GraphDiffINS5_7NodePtrEXsrS5_15IsPostDominatorEEEPS9_", I,
      "void llvm::DomTreeBuilder::ApplyUpdates<llvm::DominatorTreeBase<llvm::BasicBlock, false>>(llvm::DominatorTreeBase<llvm::BasicBlock, false>&, "
      "llvm::GraphDiff<llvm::DominatorTreeBase<llvm::BasicBlock, false>::NodePtr, llvm::DominatorTreeBase<llvm::BasicBlock, false>::IsPostDominator>&, "
      "llvm::GraphDiff<llvm::DominatorTreeBase<llvm::BasicBlock, false>::NodePtr, llvm::DominatorTreeBase<llvm::BasicBlock, false>::IsPostDominator>*)" },

    /* an Itanium array bound that is a template parameter */
    { "a reference to one (LLVM's)", "_Z18ebcdicFieldToASCIIILm6EENSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEERAT__Kc", I,
      "std::__cxx11::basic_string<char, std::char_traits<char>, std::allocator<char>> ebcdicFieldToASCII<6ul>(char const (&)[6ul])" },
    { "an array", "_Z1fILm6EEvAT__c", I, "void f<6ul>(char[6ul])" },
    { "of arrays", "_Z1fILm2ELm3EEvRAT__AT0__c", I, "void f<2ul, 3ul>(char (&)[2ul][3ul])" },
    { "no bound", "_Z1fA_i", I, "f(int[])" },

    /* an Itanium constructor template has no return type */
    { "std::string's (libstdc++'s)", "_ZNSsC1IPcEET_S1_RKSaIcE", I,
      "std::basic_string<char, std::char_traits<char>, std::allocator<char>>::basic_string<char*>(char*, char*, std::allocator<char> const&)" },
    { "a class's", "_ZN1AC1IiEET_", I, "A::A<int>(int)" },
    { "a function template's still has", "_ZN1A1fIiEEvT_", I, "void A::f<int>(int)" },

    /* an Itanium abi tag: a constructor repeats the name without it */
    { "a destructor (libstdc++'s)", "_ZNSt8ios_base7failureB5cxx11D0Ev", I, "std::ios_base::failure[abi:cxx11]::~failure()" },
    { "a constructor", "_ZNSt8ios_base7failureB5cxx11C1EPKcRKSt10error_code", I,
      "std::ios_base::failure[abi:cxx11]::failure(char const*, std::error_code const&)" },
    { "two tags", "_Z1fB3fooB3bari", I, "f[abi:foo][abi:bar](int)" },
    { "a constructor of a twice-tagged class", "_ZN1AB3fooB3barC1Ev", I, "A[abi:foo][abi:bar]::A()" },

    /* an Itanium encoding inside a type enters no name of its function */
    { "a lambda's typeinfo (LLVM's)", "_ZTIZN4llvm6cgdata15StreamCacheDataC4EjRKNS_9FileCacheERKNS_5TwineEEUlmS7_E_", I,
      "typeinfo for llvm::cgdata::StreamCacheData::StreamCacheData(unsigned int, llvm::FileCache const&, llvm::Twine const&)"
      "::{lambda(unsigned long, llvm::Twine const&)#1}" },
    { "a lambda's parameters", "_ZTIZN1A1fERKNS_1BEEUlS2_E_", I, "typeinfo for A::f(A::B const&)::{lambda(A::B const&)#1}" },

    /* an Itanium name in an expression enters no substitution */
    { "enable_if's condition (LLVM's)", "_ZN4llvm18SaturatingMultiplyImEENSt9enable_ifIX13is_unsigned_vIT_EES2_E4typeES2_S2_Pb", I,
      "std::enable_if<is_unsigned_v<unsigned long>, unsigned long>::type "
      "llvm::SaturatingMultiply<unsigned long>(unsigned long, unsigned long, bool*)" },
    { "after it, the entry before", "_Z1fIiEv1AIX1vIT_EES0_E", I, "void f<int>(A<v<int>, A>)" },

    /* an Itanium local name used as a type is one entry, whole */
    { "a local class", "_Z1fZ1gvE1AS_", I, "f(g()::A, g()::A)" },
    { "a local class's member class", "_Z1fZ1gvEN1A1BES_S0_", I, "f(g()::A::B, A, g()::A::B)" },

    /* a template parameter in an Itanium lambda's signature is its auto */
    { "a generic lambda's call operator", "_ZZ1fvENKUlT_E_clIiEEDaS_", I,
      "auto f()::{lambda(auto:1)#1}::operator()<int>(int) const" },
    { "two of them", "_ZZ1fvENKUlT_T0_E_clIicEEDaS_S0_", I,
      "auto f()::{lambda(auto:1, auto:2)#1}::operator()<int, char>(int, char) const" },
    { "an entry of the enclosing template's", "_Z1gIPiZ1fIcEvT_EUlS2_E_EvS0_S3_", I,
      "void g<int*, f<char>(char)::{lambda(auto:1)#1}>(int*, f<char>(char)::{lambda(auto:1)#1})" },
    /* an entry with one in it is the argument outside the signature: a
     * reference to it, as resolved the first time it was printed so */
    { "a reference to one, outside", "_Z1gIZ1fvEUlOT_E_EvS1_", I,
      "void g<f()::{lambda(auto:1&&)#1}>(f()::{lambda(auto:1&&)#1}&&)" },
    { "a qualified one", "_Z1gIZ1fvEUlRKT_E_EvS2_", I,
      "void g<f()::{lambda(auto:1 const&)#1}>(f()::{lambda(auto:1 const&)#1} const&)" },
    { "a reference, as first resolved", "_Z1kIZ1gIZ1fvEUlOT_E_EvS2_EUlvE_EvS2_", I,
      "void k<g<f()::{lambda(auto:1&&)#1}>(f()::{lambda(auto:1&&)#1}&&)::{lambda()#1}>"
      "(f()::{lambda(auto:1&&)#1}&&)" },

    /* an entry that is a template parameter alone is the argument of the
     * template where it is used; under a reference, of the one where it
     * was first printed under a reference, as GNU c++filt has it */
    { "the parameter, where it is used", "_Z1fIiZ1gIcEvT_E1AEvS1_", I,
      "void f<int, g<char>(char)::A>(int)" },
    { "after one under a reference", "_Z1fIiZ1gIcEvOT_E1AEvS1_", I,
      "void f<int, g<char>(char&&)::A>(int)" },
    { "under a reference the first time", "_Z1fIiZ1gIcEvT_E1AEvRS1_", I,
      "void f<int, g<char>(char)::A>(int&)" },
    { "under one again", "_Z1fIiZ1gIcEvOT_E1AEvRS1_", I,
      "void f<int, g<char>(char&&)::A>(char&)" },

    /* Itanium operator expressions, as GNU c++filt prints them */
    { "a binary operator", "_Z1fIiEDTplfp_Li1EET_", I, "decltype ({parm#1}+(1)) f<int>(int)" },
    { "two literals", "_Z1fIiEDTmlLi2ELi3EET_", I, "decltype ((2)*(3)) f<int>(int)" },
    { "> parenthesized once more", "_Z1fIiEDTgtfp_Li0EET_", I, "decltype (({parm#1}>(0))) f<int>(int)" },
    { "a unary one", "_Z1fIiEDTntfp_ET_", I, "decltype (!{parm#1}) f<int>(int)" },
    { "prefix ++", "_Z1fIiEDTpp_fp_ET_", I, "decltype (++{parm#1}) f<int>(int)" },
    { "postfix ++", "_Z1fIiEDTppfp_ET_", I, "decltype ({parm#1}++) f<int>(int)" },
    { "a unary left fold", "_Z1fIJiEEDTflplfp_EDpT_", I, "decltype ((...+{parm#1})) f<int>(int)" },
    { "a unary right fold", "_Z1fIJiEEDTfrplfp_EDpT_", I, "decltype (({parm#1}+...)) f<int>(int)" },
    { "a binary left fold", "_Z1fIJiEEDTfLplfp_Li0EEDpT_", I, "decltype (({parm#1}+...+(0))) f<int>(int)" },
    { "a binary right fold", "_Z1fIJiEEDTfRplLi0Efp_EDpT_", I, "decltype (((0)+...+{parm#1})) f<int>(int)" },
    { "a call", "_Z1fIiEDTcl1gfp_EET_", I, "decltype (g({parm#1})) f<int>(int)" },
    { "a call of a template", "_Z1fIiEDTcl1gIT_EEET_", I, "decltype ((g<int>)()) f<int>(int)" },
    { "a call of a function named outright", "_Z1fIiEDTclL_Z1gvEEET_", I, "decltype (g()) f<int>(int)" },
    { "a template parameter pack expanded", "_Z1fIJicEEDTcl1gspT_EEDpT_", I,
      "decltype (g(int, char)) f<int, char>(int, char)" },
    { "a function parameter pack expanded", "_Z1fIJicEEDTcl1gspfp_EEDpT_", I,
      "decltype (g({parm#1}...)) f<int, char>(int, char)" },
    { "an index", "_Z1fIiEDTixfp_Li0EET_", I, "decltype ({parm#1}[0]) f<int>(int)" },
    { "a conditional", "_Z1fIiEDTquLb1ELi1ELi2EET_", I, "decltype ((true)?(1) : (2)) f<int>(int)" },
    { "sizeof a type", "_Z1fIiEDTstT_ET_", I, "decltype (sizeof (int)) f<int>(int)" },
    { "sizeof an expression", "_Z1fIiEDTszfp_ET_", I, "decltype (sizeof {parm#1}) f<int>(int)" },
    { "sizeof... a pack, its length", "_Z1fIJicEEDTsZT_EDpT_", I, "decltype (2) f<int, char>(int, char)" },
    { "sizeof... an empty one", "_Z1fIJEEDTsZT_EDpT_", I, "decltype (0) f<>()" },
    { "sizeof... a function parameter pack", "_Z1fIiEDTsZfp_ES0_", I, "decltype (0) f<int>(decltype (0))" },
    { "a cast", "_Z1fIiEDTcvT_fp_ET_", I, "decltype ((int){parm#1}) f<int>(int)" },
    { "member access", "_Z1fIiEDTptfp_1xET_", I, "decltype ({parm#1}->x) f<int>(int)" },
    { "noexcept", "_Z1fIiEDTnxfp_ET_", I, "decltype (noexcept({parm#1})) f<int>(int)" },
    { "! of a qualified name (Boost's)",
      "_ZZN5boost4asio9execution6detail17any_executor_base16target_fns_tableINS0_10io_context19basic_executor_typeISaIvELm0EEEEEPKNS3_10target_fnsEbPNSt9enable_ifIXntsrSt7is_sameIT_vE5valueEvE4typeEE16fns_with_execute", I,
      "boost::asio::execution::detail::any_executor_base::target_fns_table<boost::asio::io_context::basic_executor_type<std::allocator<void>, 0ul>>"
      "(bool, std::enable_if<!std::is_same<boost::asio::io_context::basic_executor_type<std::allocator<void>, 0ul>, void>::value, void>::type*)"
      "::fns_with_execute" },

    /* a function whose return type has a declarator takes its parameters
     * inside it, after the * */
    { "returning a function pointer", "_Z1fIFPFvvEiEEvv", I, "void f<void (*(int))()>()" },
    { "returning a member function pointer", "_Z1fIFM1AKFvvEiEEvv", I, "void f<void (A::*(int))() const>()" },
    { "returning a function reference", "_Z1fIFRFicEvEEvv", I, "void f<int (&())(char)>()" },
    { "returning an array pointer", "_Z1fIFPA3_iiEEvv", I, "void f<int (*(int))[3]>()" },
    { "two deep", "_Z1fIFPFPFvvEiEcEEvv", I, "void f<void (*(*(char))(int))()>()" },
    { "a pointer to one", "_Z1fPFPFivEcE", I, "f(int (*(*)(char))())" },
    { "a pointer in a template argument", "_Z1fIFP1BIPFvvEEvEEvv", I, "void f<B<void (*)()>* ()>()" },
    { "std::_Bind's", "_ZTSSt5_BindIFM1AFPivEPS0_EE", I, "typeinfo name for std::_Bind<int* (A::*(A*))()>" },

    /* an unnamed type is an entry of its own, before any it is part of */
    { "an unnamed type's entries", "_Z1fIN1AUt_EEvS1_S2_", I,
      "void f<A::{unnamed type#1}>({unnamed type#1}, A::{unnamed type#1})" },
    { "in a nested name", "_ZN1AUt0_3fooES0_S1_", I,
      "A::{unnamed type#2}::foo({unnamed type#2}, A::{unnamed type#2})" },
    { "a local one", "_Z1fIZ1gvEUt_EvS0_S1_", I,
      "void f<g()::{unnamed type#1}>({unnamed type#1}, g()::{unnamed type#1})" },

    /* a qualifier a template argument or an entry has already is printed
     * once, after its others, as GNU c++filt prints it */
    { "const on a const argument", "_Z1fIKiEvKT_", I, "void f<int const>(int const)" },
    { "under a reference", "_Z1fIKiEvRKT_", I, "void f<int const>(int const&)" },
    { "the others go first", "_Z1fIVKiEvKT_", I, "void f<int const volatile>(int volatile const)" },
    { "both", "_Z1fIVKiEvRVKT_", I, "void f<int const volatile>(int const volatile&)" },
    { "on an entry", "_Z1gIKiEvKS0_", I, "void g<int const>(int const)" },
    { "a pointer's own", "_Z1fIPKiEvKT_", I, "void f<int const*>(int const* const)" },

    /* an Itanium qualified function type is one substitution entry */
    { "a const member function's", "_Z1fIM1AKFvvEEvS0_S1_S2_", I,
      "void f<void (A::*)() const>(A, void () const, void (A::*)() const)" },
    { "volatile const", "_Z1fIM1AKVFvvEEvS1_S2_", I,
      "void f<void (A::*)() volatile const>(void () volatile const, void (A::*)() volatile const)" },
    { "a pointer to one", "_Z1fPKFviES_S0_", I, "f(void (*)(int) const, void (int) const, void (*)(int) const)" },
    { "LLVM's", "_ZN4llvm6detail9checkedOpIiMNS_5APIntEKFS2_RKS2_RbEEENSt9enable_ifIXaa13is_integral_vIT_ElemlstS9_Li8ELi64EESt8optionalIS9_EE4typeES9_S9_T0_b", I,
      "std::enable_if<(is_integral_v<int>)&&(((sizeof (int))*(8))<=(64)), std::optional<int>>::type "
      "llvm::detail::checkedOp<int, llvm::APInt (llvm::APInt::*)(llvm::APInt const&, bool&) const>"
      "(int, int, llvm::APInt (llvm::APInt::*)(llvm::APInt const&, bool&) const, bool)" },

    /* an Itanium decltype is a substitution candidate */
    { "a later parameter's", "_Z1fIiEvDTfp_ES0_", I, "void f<int>(decltype ({parm#1}), decltype ({parm#1}))" },
    { "libstdc++'s", "_ZSt14__copy_move_a1ILb1EPjjEN9__gnu_cxx11__enable_ifIXsrSt27__is_any_random_access_iterIT0_DTcl26__iter_concept_or_categoryIS4_EEEE7__valueESt15_Deque_iteratorIT1_RS8_PS8_EE6__typeES4_S4_SB_", I,
      "__gnu_cxx::__enable_if<std::__is_any_random_access_iter<unsigned int*, decltype ((__iter_concept_or_category<unsigned int*>)())>::__value, "
      "std::_Deque_iterator<unsigned int, unsigned int&, unsigned int*>>::__type std::__copy_move_a1<true, unsigned int*, unsigned int>"
      "(unsigned int*, unsigned int*, std::_Deque_iterator<unsigned int, unsigned int&, unsigned int*>)" },

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
