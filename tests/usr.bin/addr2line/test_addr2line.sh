#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
BIN="$ROOT/usr.bin/addr2line/addr2line"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/addr2line-tests.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

expect_nonempty_match() {
    label=$1
    got=$2
    pat=$3
    echo "$got" | grep -E "$pat" >/dev/null 2>&1 || {
        printf 'FAIL: %s\n  got : [%s]\n  want pattern: [%s]\n' "$label" "$got" "$pat" >&2
        exit 1
    }
    pass "$label"
}

expect_eq() {
    label=$1
    got=$2
    want=$3
    [ "$got" = "$want" ] || {
        printf 'FAIL: %s\n  got : [%s]\n  want: [%s]\n' "$label" "$got" "$want" >&2
        exit 1
    }
    pass "$label"
}

build_fixture_c() {
    out=$1
    dwarf=$2
    cflags=$3
    cat > "$TMP/sample.c" <<'SRC'
int f0(void) { return 10; }
int f1(int x) { return f0() + x; }
int main(void) { return f1(7); }
SRC
    cc $cflags -g $dwarf -no-pie -o "$out" "$TMP/sample.c"
}

build_fixture_cpp() {
    out=$1
    dwarf=$2
    cat > "$TMP/sample.cpp" <<'SRC'
namespace Ns {
int FooBar(int x) { return x + 3; }
}
int main() { return Ns::FooBar(7); }
SRC
    c++ -g $dwarf -no-pie -o "$out" "$TMP/sample.cpp"
}

first_line() {
    printf '%s\n' "$1" | sed -n '1p'
}

addr_of() {
    obj=$1
    sym=$2
    nm -n "$obj" | awk -v s="$sym" '$3==s {print $1; exit}'
}

main() {
    require_cmd cc
    require_cmd nm

    [ -x "$BIN" ] || fail "addr2line binary not found: $BIN"

    # 10a-1: ET_EXEC ELF64 with DWARF v4
    build_fixture_c "$TMP/exec64_v4" "-gdwarf-4" ""
    A=$(addr_of "$TMP/exec64_v4" f1)
    [ -n "$A" ] || fail "10a-1 symbol f1 not found"
    OUT=$($BIN -e "$TMP/exec64_v4" 0x$A)
    expect_nonempty_match "10a-1" "$OUT" 'sample\.c:[0-9]+'

    # 10a-2: ET_EXEC ELF32 with DWARF v4 (skip if host lacks -m32)
    if cc -m32 -x c - -o "$TMP/cc32_probe" >/dev/null 2>&1 <<'SRC'
int main(void) { return 0; }
SRC
    then
        build_fixture_c "$TMP/exec32_v4" "-gdwarf-4" "-m32"
        A32=$(addr_of "$TMP/exec32_v4" f1)
        [ -n "$A32" ] || fail "10a-2 symbol f1 not found"
        OUT32=$($BIN -e "$TMP/exec32_v4" 0x$A32)
        expect_nonempty_match "10a-2" "$OUT32" 'sample\.c:[0-9]+'
    else
        pass "10a-2 skipped (no -m32 toolchain)"
    fi

    # 10a-3: DWARF v5
    build_fixture_c "$TMP/exec64_v5" "-gdwarf-5" ""
    A5=$(addr_of "$TMP/exec64_v5" f1)
    [ -n "$A5" ] || fail "10a-3 symbol f1 not found"
    OUT5=$($BIN -e "$TMP/exec64_v5" 0x$A5)
    expect_nonempty_match "10a-3" "$OUT5" 'sample\.c:[0-9]+'

    # 10a-4: multiple addresses resolved independently
    A0=$(addr_of "$TMP/exec64_v5" f0)
    AM=$(addr_of "$TMP/exec64_v5" main)
    [ -n "$A0" ] || fail "10a-4 symbol f0 not found"
    [ -n "$AM" ] || fail "10a-4 symbol main not found"
    OUTM=$($BIN -e "$TMP/exec64_v5" 0x$A0 0x$A5 0x$AM)
    LINES=$(printf '%s\n' "$OUTM" | wc -l | tr -d ' ')
    expect_eq "10a-4" "$LINES" "3"

    # 10b-1: -f prints function before file:line
    OUTF=$($BIN -f -e "$TMP/exec64_v5" 0x$A5)
    FN_LINE=$(first_line "$OUTF")
    expect_eq "10b-1" "$FN_LINE" "f1"

    # 10b-2: DW_AT_linkage_name shown without -C
    require_cmd c++
    build_fixture_cpp "$TMP/cpp_v4" "-gdwarf-4"
    CPP_MANGLED=$(nm -n "$TMP/cpp_v4" | awk '$3=="_ZN2Ns6FooBarEi" {print $1; exit}')
    [ -n "$CPP_MANGLED" ] || fail "10b-2 mangled symbol not found"
    OUT_CPP_RAW=$($BIN -f -e "$TMP/cpp_v4" 0x$CPP_MANGLED)
    CPP_RAW_FN=$(first_line "$OUT_CPP_RAW")
    expect_eq "10b-2" "$CPP_RAW_FN" "_ZN2Ns6FooBarEi"

    # 10b-3: -f -C prints demangled C++ function
    OUT_CPP_DEM=$($BIN -f -C -e "$TMP/cpp_v4" 0x$CPP_MANGLED)
    CPP_DEM_FN=$(first_line "$OUT_CPP_DEM")
    expect_nonempty_match "10b-3" "$CPP_DEM_FN" '^Ns::FooBar\(int\)$'

    # 10b-4: fallback to .symtab when .debug_info absent
    objcopy --remove-section .debug_info \
            --remove-section .debug_abbrev \
            --remove-section .debug_str \
            --remove-section .debug_rnglists \
            --remove-section .debug_ranges \
            "$TMP/exec64_v5" "$TMP/exec64_symtab_only"
    OUT_SYM=$($BIN -f -e "$TMP/exec64_symtab_only" 0x$A5)
    SYM_FN=$(first_line "$OUT_SYM")
    expect_eq "10b-4" "$SYM_FN" "f1"

    pass "all"
}

main "$@"
