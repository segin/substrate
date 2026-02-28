#include <demangle.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed = 0;

static void
failf(const char *label, const char *msg)
{
    fprintf(stderr, "FAIL: %s: %s\n", label, msg);
    g_failed++;
}

static void
expect_eq(const char *label, const char *sym, int opt, const char *want)
{
    char *got = demangle(sym, opt);
    if (got == NULL) {
        failf(label, "demangle returned NULL");
        return;
    }
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: %s: expected [%s], got [%s]\n", label, want, got);
        g_failed++;
    }
    free(got);
}

static void
expect_contains(const char *label, const char *sym, int opt, const char *needle)
{
    char *got = demangle(sym, opt);
    if (got == NULL) {
        failf(label, "demangle returned NULL");
        return;
    }
    if (strstr(got, needle) == NULL) {
        fprintf(stderr, "FAIL: %s: expected substring [%s], got [%s]\n", label, needle, got);
        g_failed++;
    }
    free(got);
}

static void
expect_not_null(const char *label, const char *sym, int opt)
{
    char *got = demangle(sym, opt);
    if (got == NULL) {
        failf(label, "demangle returned NULL");
        return;
    }
    free(got);
}

static void
expect_null(const char *label, const char *sym, int opt)
{
    char *got = demangle(sym, opt);
    if (got != NULL) {
        fprintf(stderr, "FAIL: %s: expected NULL, got [%s]\n", label, got);
        free(got);
        g_failed++;
    }
}

static void
test_9a(void)
{
    expect_eq("9a-1", "_Z3foov", DEMANGLE_AUTO, "foo()");
    expect_eq("9a-2", "_Z3fooi", DEMANGLE_AUTO, "foo(int)");
    expect_eq("9a-3", "_ZN3Foo3barEv", DEMANGLE_AUTO, "Foo::bar()");
    expect_eq("9a-4", "_ZNK3Foo3barEi", DEMANGLE_AUTO, "Foo::bar(int) const");
    expect_contains("9a-5", "_Z3fooIiEvT_", DEMANGLE_AUTO, "foo<int>(");
}

static void
test_9b(void)
{
    expect_eq("9b-1", "_ZN3FooplERKS_", DEMANGLE_AUTO, "Foo::operator+(Foo const&)");
    expect_eq("9b-2", "_ZN3FooclEi", DEMANGLE_AUTO, "Foo::operator()(int)");
    expect_eq("9b-3", "_ZN3FoocvfEv", DEMANGLE_AUTO, "Foo::operator float()");
    expect_eq("9b-4", "_ZN3FoodlEPv", DEMANGLE_AUTO, "Foo::operator delete(void*)");
}

static void
test_9c(void)
{
    expect_eq("9c-1", "_ZN3FooC1Ev", DEMANGLE_AUTO, "Foo::Foo()");
    expect_eq("9c-2", "_ZN3FooC2Ei", DEMANGLE_AUTO, "Foo::Foo(int)");
    expect_eq("9c-3", "_ZN3FooD0Ev", DEMANGLE_AUTO, "Foo::~Foo()");
    expect_eq("9c-4", "_ZN3FooD2Ev", DEMANGLE_AUTO, "Foo::~Foo()");
}

static void
test_9d(void)
{
    expect_eq("9d-1", "_ZTV3Foo", DEMANGLE_AUTO, "vtable for Foo");
    expect_eq("9d-2", "_ZTI3Foo", DEMANGLE_AUTO, "typeinfo for Foo");
    expect_eq("9d-3", "_ZTS3Foo", DEMANGLE_AUTO, "typeinfo name for Foo");
    expect_eq("9d-4", "_ZTT3Foo", DEMANGLE_AUTO, "VTT for Foo");
    expect_eq("9d-5", "_ZGVN3Foo3barE", DEMANGLE_AUTO, "guard variable for Foo::bar");
}

static void
test_9e(void)
{
    expect_contains("9e-1", "_ZN1AIiE1BIS_IjEE3fooEv", DEMANGLE_AUTO, "A<int>::B<");
    expect_contains("9e-2", "_Z3fooILi42EEvv", DEMANGLE_AUTO, "foo<42>");
    expect_null("9e-3", "_Z3fooIJiiEEvDpT_", DEMANGLE_AUTO);
}

static void
test_9f(void)
{
    expect_not_null("9f-1", "_ZN3Foo3barES0_", DEMANGLE_AUTO);
    expect_not_null("9f-2", "_ZNSt6vectorIiSaIiEEC1Ev", DEMANGLE_AUTO);
    expect_eq("9f-3", "_ZNSsC1Ev", DEMANGLE_AUTO, "std::string::std::string()");
}

static void
test_9g(void)
{
    int opt = DEMANGLE_AUTO | DEMANGLE_TYPES;
    expect_eq("9g-1", "i", opt, "int");
    expect_eq("9g-2", "PKc", opt, "char const*");
    expect_eq("9g-3", "FvvE", opt, "void ()");
}

static void
test_9h(void)
{
    expect_eq("9h-1", "_Z3fooi", DEMANGLE_AUTO | DEMANGLE_NO_PARAMS, "foo");
    expect_eq("9h-2", "_ZNK3Foo3barEv", DEMANGLE_AUTO | DEMANGLE_NO_VERBOSE, "Foo::bar()");
}

static void
test_9i(void)
{
    char *deep;
    char *huge;
    size_t i;

    expect_null("9i-1", "main", DEMANGLE_AUTO);
    expect_null("9i-2", "", DEMANGLE_AUTO);
    if (demangle(NULL, DEMANGLE_AUTO) != NULL) {
        failf("9i-3", "NULL input should return NULL");
    }
    expect_null("9i-4", "_ZN3Foo", DEMANGLE_AUTO);

    deep = (char *)malloc(512u);
    if (deep == NULL) {
        failf("9i-5", "alloc failed");
    } else {
        char *out;
        for (i = 0u; i < 300u; i++) {
            deep[i] = 'P';
        }
        deep[300] = 'i';
        deep[301] = '\0';
        out = demangle(deep, DEMANGLE_AUTO | DEMANGLE_TYPES);
        free(out);
        free(deep);
    }

    huge = (char *)malloc(65537u);
    if (huge == NULL) {
        failf("9i-6", "alloc failed");
    } else {
        huge[0] = '_';
        huge[1] = 'Z';
        for (i = 2u; i < 65535u; i++) {
            huge[i] = 'a';
        }
        huge[65535] = '\0';
        {
            char *out = demangle(huge, DEMANGLE_AUTO);
            free(out);
        }
        free(huge);
    }
}

static void
test_9j(void)
{
    expect_contains("9j-1", "_RNvCshgxSpmajvKg_7mycrate3foo", DEMANGLE_AUTO, "mycrate");
    expect_contains("9j-2", "_RNvNtCshgxSpmajvKg_7mycrate4mod13bar", DEMANGLE_AUTO, "mod1");
    expect_contains("9j-3", "_RNvMCshgxSpmajvKg_7mycrateNtB2_3Foo3baz", DEMANGLE_AUTO, "<");
    expect_contains("9j-4", "_RNvXCshgxSpmajvKg_7mycrateNtB2_3FooNtB2_5Trait3qux", DEMANGLE_AUTO, " as ");
    expect_contains("9j-5", "_RINvCshgxSpmajvKg_7mycrate3foolE", DEMANGLE_AUTO, "::<i32>");
}

static void
test_9k(void)
{
    expect_contains("9k-1a", "_RINvCshgxSpmajvKg_7mycrate3foolE", DEMANGLE_AUTO, "i32");
    expect_contains("9k-1b", "_RINvCshgxSpmajvKg_7mycrate3foobE", DEMANGLE_AUTO, "bool");
    expect_contains("9k-1c", "_RINvCshgxSpmajvKg_7mycrate3foocE", DEMANGLE_AUTO, "char");
    expect_contains("9k-1d", "_RINvCshgxSpmajvKg_7mycrate3fooeE", DEMANGLE_AUTO, "str");
    expect_contains("9k-2", "_RINvCshgxSpmajvKg_7mycrate3fooRlE", DEMANGLE_AUTO, "<&i32>");
    expect_contains("9k-3", "_RINvCshgxSpmajvKg_7mycrate3fooQlE", DEMANGLE_AUTO, "<&mut i32>");
    expect_contains("9k-4a", "_RINvCshgxSpmajvKg_7mycrate3fooPlE", DEMANGLE_AUTO, "<*const i32>");
    expect_contains("9k-4b", "_RINvCshgxSpmajvKg_7mycrate3fooOlE", DEMANGLE_AUTO, "<*mut i32>");
    expect_contains("9k-5", "_RINvCshgxSpmajvKg_7mycrate3fooAljA_E", DEMANGLE_AUTO, "[i32; ");
    expect_contains("9k-6", "_RINvCshgxSpmajvKg_7mycrate3fooSlE", DEMANGLE_AUTO, "<[i32]>");
    expect_contains("9k-7", "_RINvCshgxSpmajvKg_7mycrate3fooTlbEE", DEMANGLE_AUTO, "<(i32, bool)>");
    expect_contains("9k-8", "_RINvCshgxSpmajvKg_7mycrate3fooFUKClElE", DEMANGLE_AUTO, "extern \"C\" fn(i32) -> i32");
    expect_contains("9k-9", "_RINvCshgxSpmajvKg_7mycrate3fooDNtCshgxSpmajvKg_7mycrate5TraitEL_E", DEMANGLE_AUTO, "<dyn ");
}

static void
test_9l(void)
{
    expect_contains("9l-1", "_RINvCshgxSpmajvKg_7mycrate3fooAljA_E", DEMANGLE_AUTO, "0xA");
    expect_contains("9l-2", "_RINvCshgxSpmajvKg_7mycrate3fooAljnA_E", DEMANGLE_AUTO, "-0xA");
    expect_contains("9l-3a", "_RINvCshgxSpmajvKg_7mycrate3fooAlb0_E", DEMANGLE_AUTO, "false");
    expect_contains("9l-3b", "_RINvCshgxSpmajvKg_7mycrate3fooAlb1_E", DEMANGLE_AUTO, "true");
    expect_contains("9l-4", "_RINvCshgxSpmajvKg_7mycrate3fooAlc41_E", DEMANGLE_AUTO, "'A'");
}

static void
test_9m(void)
{
    expect_contains("9m-1", "_RINvCshgxSpmajvKg_7mycrate3fooL_E", DEMANGLE_AUTO, "'_>");
    expect_contains("9m-2a", "_RINvCshgxSpmajvKg_7mycrate3fooL0_E", DEMANGLE_AUTO, "'a>");
    expect_contains("9m-2b", "_RINvCshgxSpmajvKg_7mycrate3fooL1_E", DEMANGLE_AUTO, "'b>");
    expect_contains("9m-3", "_RINvCshgxSpmajvKg_7mycrate3fooFG_UKClElE", DEMANGLE_AUTO, "fn(i32) -> i32");
}

static void
test_9n(void)
{
    expect_not_null("9n-1", "_RINvCshgxSpmajvKg_7mycrate3foolB3_E", DEMANGLE_AUTO);
    expect_null("9n-2", "_RIB0_E", DEMANGLE_AUTO);
    expect_not_null("9n-3", "_RINvCshgxSpmajvKg_7mycrate3fooB1_B1_E", DEMANGLE_AUTO);
}

static void
test_9o(void)
{
    expect_contains("9o-1", "_RNvCshgxSpmajvKg_7mycrate7closure", DEMANGLE_AUTO, "{closure#0}");
    expect_contains("9o-2", "_RNvCshgxSpmajvKg_7mycrate4shim", DEMANGLE_AUTO, "{shim:vtable#0}");
    expect_contains("9o-3", "_RNvCshgxSpmajvKg_7mycrates1_7closure", DEMANGLE_AUTO, "{closure#2}");
}

static void
test_9p(void)
{
    expect_contains("9p-1", "_RNvCshgxSpmajvKg_7mycrateu3abc", DEMANGLE_AUTO, "{{");
    expect_contains("9p-2", "_RNvCshgxSpmajvKg_7mycrate3abc", DEMANGLE_AUTO, "{abc}");
}

static void
test_9q(void)
{
    expect_eq("9q-1", "_ZN7mycrate3foo17h1234567890abcdefE", DEMANGLE_AUTO, "mycrate::foo");
    expect_contains("9q-2", "_ZN47_$LT$mycrate..Foo$u20$as$u20$mycrate..Trait$GT$3qux17hfa7523f5fb2df040E", DEMANGLE_AUTO, "<mycrate..Foo as mycrate..Trait>");
    expect_contains("9q-3", "_ZN3foo3bar17h1234567890abcdefE", DEMANGLE_AUTO, "foo::bar");
    expect_eq("9q-4", "_ZN3Foo3barEv", DEMANGLE_AUTO, "Foo::bar()");
}

static void
test_9r(void)
{
    expect_contains("9r-1", "_D3std5stdio7writelnFAaZv", DEMANGLE_AUTO, "std.stdio.writeln");
    expect_contains("9r-2", "_D3foo3barFiZi", DEMANGLE_AUTO, "foo.bar: int function(int)");
    expect_not_null("9r-3", "_D3std5range10primitives3popFiZi", DEMANGLE_AUTO);
}

static void
test_9s(void)
{
    expect_contains("9s-1", "_D3pkg3mod__T3FooTiTAaZi", DEMANGLE_AUTO, "Foo!(int, char[])");
    expect_contains("9s-2", "_D3pkg3mod__T3FooVi42_TiZi", DEMANGLE_AUTO, "int(42)");
    expect_contains("9s-3", "_D3pkg3mod__T3FooS3pkg3mod3barZi", DEMANGLE_AUTO, "pkg.mod.bar");
}

static void
test_9t(void)
{
    expect_contains("9t-1", "_D3foo3barFiZi", DEMANGLE_AUTO, "int function(int)");
    expect_contains("9t-2", "_D3foo3barAi", DEMANGLE_AUTO, "int[]");
    expect_contains("9t-3", "_D3foo3barG3i", DEMANGLE_AUTO, "int[3]");
    expect_contains("9t-4", "_D3foo3barHia", DEMANGLE_AUTO, "char[int]");
    expect_contains("9t-5", "_D3foo3barPi", DEMANGLE_AUTO, "int*");
    expect_contains("9t-6", "_D3foo3barDFiZi", DEMANGLE_AUTO, "int delegate(int)");
    expect_contains("9t-7a", "_D3foo3barxi", DEMANGLE_AUTO, "const(int)");
    expect_contains("9t-7b", "_D3foo3baryi", DEMANGLE_AUTO, "immutable(int)");
}

static void
test_9u(void)
{
    expect_null("9u-1a", "_Dmain", DEMANGLE_DLANG);
    expect_null("9u-1b", "_Dmain", DEMANGLE_AUTO);
    expect_eq("9u-2", "_d_allocmemory", DEMANGLE_AUTO, "_d_allocmemory");
    expect_contains("9u-3a", "_D3foo9__lambda1FiZi", DEMANGLE_AUTO, "{lambda#1}");
    expect_contains("9u-3b", "_D3foo11__unittest1FiZi", DEMANGLE_AUTO, "{unittest#1}");
}

static void
test_9v(void)
{
    expect_eq("9v-1", "_Z3foov", DEMANGLE_AUTO, "foo()");
    expect_contains("9v-2", "_RNvCshgxSpmajvKg_7mycrate3foo", DEMANGLE_AUTO, "mycrate");
    expect_contains("9v-3", "_D3foo3barFiZi", DEMANGLE_AUTO, "foo.bar");
    expect_eq("9v-4", "_ZN7mycrate3foo17h1234567890abcdefE", DEMANGLE_AUTO, "mycrate::foo");
    expect_null("9v-5", "main", DEMANGLE_AUTO);
}

static void
test_9y(void)
{
    char buf[64];
    char tiny[1];
    int rc;

    rc = demangle_buf("_Z3foov", buf, sizeof(buf), DEMANGLE_AUTO);
    if (rc != 0 || strcmp(buf, "foo()") != 0) {
        failf("9y-1", "exact-fit demangle_buf failed");
    }

    rc = demangle_buf("_ZNK3Foo3barEi", buf, 8u, DEMANGLE_AUTO);
    if (rc != -2 || buf[7] != '\0') {
        failf("9y-2", "too-small demangle_buf behavior mismatch");
    }

    tiny[0] = 'X';
    rc = demangle_buf("_Z3foov", tiny, sizeof(tiny), DEMANGLE_AUTO);
    if (rc != -2 || tiny[0] != '\0') {
        failf("9y-3", "1-byte demangle_buf behavior mismatch");
    }
}

int
main(void)
{
    test_9a();
    test_9b();
    test_9c();
    test_9d();
    test_9e();
    test_9f();
    test_9g();
    test_9h();
    test_9i();
    test_9j();
    test_9k();
    test_9l();
    test_9m();
    test_9n();
    test_9o();
    test_9p();
    test_9q();
    test_9r();
    test_9s();
    test_9t();
    test_9u();
    test_9v();
    test_9y();

    if (g_failed != 0) {
        fprintf(stderr, "demangle tests failed: %d\n", g_failed);
        return 1;
    }

    puts("demangle tests: ok");
    return 0;
}
