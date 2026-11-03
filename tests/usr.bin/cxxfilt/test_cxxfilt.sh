#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
BIN="$ROOT/usr.bin/cxxfilt/c++filt"

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }
expect_eq() {
    label=$1
    got=$2
    want=$3
    if [ "$got" != "$want" ]; then
        printf 'FAIL: %s\n  got : [%s]\n  want: [%s]\n' "$label" "$got" "$want" >&2
        exit 1
    fi
    pass "$label"
}

"$BIN" --version >/dev/null || fail "version callable"

# 8a CLI smoke
expect_eq "8a-1" "$(printf '_ZN3Foo3barEv\n' | "$BIN")" "Foo::bar()"
expect_eq "8a-2" "$(printf '_Z3fooi\n' | "$BIN")" "foo(int)"
expect_eq "8a-3" "$("$BIN" _ZN9Wikipedia7articleE)" "Wikipedia::article"
expect_eq "8a-4" "$("$BIN" _Z1fv)" "f()"
expect_eq "8a-5" "$(printf '_ZNK3Foo3barEi\n' | "$BIN")" "Foo::bar(int) const"

# 8b stdin streaming
expect_eq "8b-1" "$(printf 'X _Z3foov Y\n' | "$BIN")" "X foo() Y"
expect_eq "8b-2" "$(printf '_Z3foov _ZN3Foo3barEv\n' | "$BIN")" "foo() Foo::bar()"
expect_eq "8b-3" "$(printf '' | "$BIN")" ""
expect_eq "8b-4" "$(printf 'plain text\n' | "$BIN")" "plain text"
# A "_Z" in the middle of a word does not start a symbol: each word
# (a run of alphanumerics, '_', '$' and '.') is demangled whole or
# left alone, as GNU c++filt does.
expect_eq "8b-5" "$(printf 'FOO_Z3bar __Z3foov a._Z3foov x$_Z1fv _Z3foov,_Z1fv (_Z3foov)\n' | "$BIN" -n)" \
    'FOO_Z3bar __Z3foov a._Z3foov x$_Z1fv foo(),f() (foo())'
expect_eq "8b-6" "$(printf 'FOO_Z3bar __Z3foov\n' | "$BIN")" "FOO_Z3bar __Z3foov"
expect_eq "8b-7" "$(printf 'FOO_Z3bar __Z3foov\n' | "$BIN" -_)" "FOO_Z3bar foo()"
# One leading '.' or '$' (assembler dot-symbols) is skipped before the
# word is demangled, and a '.' is printed again, as GNU c++filt does.
# Outputs taken from GNU c++filt.
expect_eq "8b-8" "$(printf '._Z3foov $_Z3foov\n' | "$BIN")" '.foo() foo()'
expect_eq "8b-9" "$(printf '._Z3foov $_Z3foov\n' | "$BIN" -n)" '.foo() foo()'
expect_eq "8b-10" "$(printf '._Z3foov .__Z3foov $__Z1fv\n' | "$BIN" -_)" '._Z3foov .foo() f()'
expect_eq "8b-11" "$(printf '. $ .x $x .._Z3foov\n' | "$BIN")" '. $ .x $x .._Z3foov'

# 8c argv mode
expect_eq "8c-1" "$("$BIN" _Z3fooi)" "foo(int)"
expect_eq "8c-2" "$("$BIN" _Z3fooi _ZN3Foo3barEv)" "foo(int)
Foo::bar()"
expect_eq "8c-3" "$("$BIN" ._Z3foov '$_Z3foov')" ".foo()
foo()"

# 8d flags
expect_eq "8d-1" "$("$BIN" -p _Z3fooi)" "foo"
# Only -_ strips a leading underscore; -n and the default leave
# "__Z..." alone, as GNU c++filt does.
expect_eq "8d-2" "$("$BIN" -n __Z3fooi)" "__Z3fooi"
expect_eq "8d-2b" "$("$BIN" __Z3fooi)" "__Z3fooi"
expect_eq "8d-2c" "$("$BIN" -_ __Z3fooi)" "foo(int)"
expect_eq "8d-2d" "$("$BIN" --no-strip-underscore __Z3fooi)" "__Z3fooi"
# -_ tries only the stripped name: "_Z3foov" becomes "Z3foov", which
# does not demangle, so the word is echoed, operand or stdin alike.
expect_eq "8d-2e" "$("$BIN" -_ _Z3foov)" "_Z3foov"
expect_eq "8d-2f" "$(printf '_Z3foov __Z3foov\n' | "$BIN" -_)" "_Z3foov foo()"
expect_eq "8d-3" "$("$BIN" -s none _Z3fooi)" "_Z3fooi"
expect_eq "8d-4" "$("$BIN" -s rust _RNvCshgxSpmajvKg_7mycrate3foo)" "{s14492079435488262969}mycrate::{foo}"
expect_eq "8d-5" "$("$BIN" -s dlang _D3foo3barFiZi)" "foo.bar: int function(int)"
expect_eq "8d-6" "$("$BIN" -t i)" "int"
# -t demangles bare type words read from stdin too, not only operands;
# words that are not types, and _Z names, behave as without -t.
expect_eq "8d-6b" "$(printf 'i\n' | "$BIN" -t)" "int"
expect_eq "8d-6c" "$(printf 'PKc\n' | "$BIN" -t)" "char const*"
expect_eq "8d-6d" "$(printf 'this is a test i PKc _Z3foov x.i foo$i\n' | "$BIN" -t)" \
    'this is signed char test int char const* foo() x.i foo$i'
expect_eq "8d-6e" "$(printf '_i\n' | "$BIN" -t)" "_i"
expect_eq "8d-6f" "$(printf '_i\n' | "$BIN" -t -_)" "int"
expect_eq "8d-6g" "$(printf 'a i\n' | "$BIN" -t -s rust)" "a i"
expect_eq "8d-6h" "$(printf '.i $i x.i\n' | "$BIN" -t)" '.int int x.i'
expect_eq "8d-6i" "$(printf '. $ .\n' | "$BIN" -t)" '. $ .'
expect_eq "8d-7" "$("$BIN" -V)" "$("$BIN" --version)"

# 8e failures/edge
expect_eq "8e-1" "$("$BIN" main)" "main"
expect_eq "8e-2" "$("$BIN" _ZN3Foo)" "_ZN3Foo"
expect_eq "8e-3" "$(printf '\001\002\003\n' | "$BIN" | od -An -tx1 | tr -d ' \n')" "0102030a"
if "$BIN" -s not-a-style >/dev/null 2>&1; then
    fail "8e-4 invalid style should fail"
fi
pass "8e-4"

# 8f integration
TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT INT TERM
cat > "$TMPDIR/sample.cpp" <<'CPP'
namespace Foo { struct Bar { int baz(int) const; }; }
int Foo::Bar::baz(int x) const { return x + 1; }
CPP
c++ -c -O2 "$TMPDIR/sample.cpp" -o "$TMPDIR/sample.o"
RAW=$(nm "$TMPDIR/sample.o" | awk '{print $3}' | grep '^_Z' | head -n 1)
[ -n "$RAW" ] || fail "8f-raw symbol"
expect_eq "8f-1" "$(echo "$RAW" | "$BIN")" "$(echo "$RAW" | c++filt)"
expect_eq "8f-2" "$(objdump -d "$TMPDIR/sample.o" | awk '/_Z/{print $NF; exit}' | "$BIN")" "$(objdump -d "$TMPDIR/sample.o" | awk '/_Z/{print $NF; exit}' | c++filt)"

# 8g large corpus
SYMS="$TMPDIR/syms.txt"
nm -g "$(cc -print-file-name=libstdc++.a)" 2>/dev/null | awk '{print $3}' | grep '^_Z' | head -n 2000 > "$SYMS"
[ -s "$SYMS" ] || fail "8g symbols extracted"
"$BIN" < "$SYMS" >/dev/null || fail "8g run"
pass "8g"

pass "all"
