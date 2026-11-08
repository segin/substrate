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
