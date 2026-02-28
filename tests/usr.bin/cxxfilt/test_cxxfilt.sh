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

# 8c argv mode
expect_eq "8c-1" "$("$BIN" _Z3fooi)" "foo(int)"
expect_eq "8c-2" "$("$BIN" _Z3fooi _ZN3Foo3barEv)" "foo(int)
Foo::bar()"

# 8d flags
expect_eq "8d-1" "$("$BIN" -p _Z3fooi)" "foo"
expect_eq "8d-2" "$("$BIN" -n __Z3fooi)" "foo(int)"
expect_eq "8d-3" "$("$BIN" -s none _Z3fooi)" "_Z3fooi"
expect_eq "8d-4" "$("$BIN" -s rust _RNvCshgxSpmajvKg_7mycrate3foo)" "{s14492079435488262969}mycrate::{foo}"
expect_eq "8d-5" "$("$BIN" -s dlang _D3foo3barFiZi)" "foo.bar: int function(int)"
expect_eq "8d-6" "$("$BIN" -t i)" "int"
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
