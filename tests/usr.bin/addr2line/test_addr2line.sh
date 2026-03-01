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

build_fixture_rust_inline() {
    out=$1
    cat > "$TMP/inline.rs" <<'SRC'
#[inline(always)]
fn leaf(x: i32) -> i32 { x + 1 }
#[inline(always)]
fn mid(x: i32) -> i32 { leaf(x) + 2 }
fn top(x: i32) -> i32 { mid(x) + 3 }
fn main() { let _ = top(5); }
SRC
    rustc -C debuginfo=2 -C opt-level=2 -o "$out" "$TMP/inline.rs"
}

build_fixture_pie() {
    out=$1
    cat > "$TMP/pie.c" <<'SRC'
int pie_func(int x) { return x + 9; }
int main(void) { return pie_func(2); }
SRC
    cc -g -gdwarf-4 -fPIE -pie -o "$out" "$TMP/pie.c"
}

build_fixture_so() {
    out=$1
    cat > "$TMP/so.c" <<'SRC'
int so_func(int x) { return x + 11; }
SRC
    cc -g -gdwarf-4 -fPIC -shared -Wl,-soname,libfixture.so -o "$out" "$TMP/so.c"
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

    # 10c: inline frames
    require_cmd rustc
    require_cmd readelf
    require_cmd addr2line
    build_fixture_rust_inline "$TMP/rust_inline"

    INLINE_ADDR=
    OUT_INLINE=
    HOST_INLINE=
    for CAND in $(readelf --debug-dump=info "$TMP/rust_inline" | \
        awk '/DW_TAG_inlined_subroutine/ {want=1; next}
             want && /DW_AT_low_pc/ {gsub("0x", "", $NF); print $NF; want=0}' | \
        head -n 64); do
        OUT_CAND=$($BIN -f -i -e "$TMP/rust_inline" 0x$CAND 2>/dev/null || true)
        HOST_CAND=$(addr2line -f -i -e "$TMP/rust_inline" 0x$CAND 2>/dev/null || true)
        OLINES=$(printf '%s\n' "$OUT_CAND" | wc -l | tr -d ' ')
        HLINES=$(printf '%s\n' "$HOST_CAND" | wc -l | tr -d ' ')
        if [ "$OLINES" -ge 4 ] && [ "$HLINES" -ge 4 ]; then
            INLINE_ADDR=$CAND
            OUT_INLINE=$OUT_CAND
            HOST_INLINE=$HOST_CAND
            break
        fi
    done
    [ -n "$INLINE_ADDR" ] || fail "10c no inline-frame probe address found"

    # 10c-1: -i returns multiple frames
    INLINE_LINES=$(printf '%s\n' "$OUT_INLINE" | wc -l | tr -d ' ')
    [ "$INLINE_LINES" -ge 4 ] || fail "10c-1 expected >=4 lines, got $INLINE_LINES"
    pass "10c-1"

    # 10c-2: ordering matches host innermost frame first
    expect_eq "10c-2" "$(first_line "$OUT_INLINE")" "$(first_line "$HOST_INLINE")"

    # 10c-3: call_file/call_line locations are carried through output
    HOST_LOC1=$(printf '%s\n' "$HOST_INLINE" | sed -n '2p')
    HOST_LOC2=$(printf '%s\n' "$HOST_INLINE" | sed -n '4p')
    [ -n "$HOST_LOC1" ] || fail "10c-3 host location #1 missing"
    [ -n "$HOST_LOC2" ] || fail "10c-3 host location #2 missing"
    printf '%s\n' "$OUT_INLINE" | grep -F "$HOST_LOC1" >/dev/null 2>&1 || \
        fail "10c-3 missing call location: $HOST_LOC1"
    printf '%s\n' "$OUT_INLINE" | grep -F "$HOST_LOC2" >/dev/null 2>&1 || \
        fail "10c-3 missing call location: $HOST_LOC2"
    pass "10c-3"

    # 10d-1: -j .text interprets address as section offset
    TEXT_ADDR=$(readelf -SW "$TMP/exec64_v5" | \
        awk '$2==".text" {print $4; exit} $3==".text" {print $5; exit}')
    [ -n "$TEXT_ADDR" ] || fail "10d-1 .text address not found"
    OFF_HEX=$(printf '%x' $((0x$A5 - 0x$TEXT_ADDR)))
    OUT_ABS=$($BIN -e "$TMP/exec64_v5" 0x$A5)
    OUT_OFF=$($BIN -j .text -e "$TMP/exec64_v5" 0x$OFF_HEX)
    expect_eq "10d-1" "$OUT_OFF" "$OUT_ABS"

    # 10d-2: invalid section name returns error
    if "$BIN" -j .not_a_real_section -e "$TMP/exec64_v5" 0x$A5 >/dev/null 2>"$TMP/err_10d2"; then
        fail "10d-2 expected non-zero status for invalid section"
    fi
    grep -E 'unknown section: \.not_a_real_section' "$TMP/err_10d2" >/dev/null 2>&1 || \
        fail "10d-2 missing unknown section diagnostic"
    pass "10d-2"

    # 10e-1: ET_DYN PIE lookup with load bias style runtime address
    build_fixture_pie "$TMP/pie_v4"
    PIE_SYM=$(addr_of "$TMP/pie_v4" pie_func)
    [ -n "$PIE_SYM" ] || fail "10e-1 pie_func symbol not found"
    OUT_PIE_STATIC=$($BIN -e "$TMP/pie_v4" 0x$PIE_SYM)
    PIE_RUNTIME=$(printf '%x' $((0x$PIE_SYM + 0x55555000)))
    OUT_PIE_RUNTIME=$($BIN -e "$TMP/pie_v4" 0x$PIE_RUNTIME)
    expect_eq "10e-1" "$OUT_PIE_RUNTIME" "$OUT_PIE_STATIC"

    # 10e-2: ET_DYN shared object function lookup
    build_fixture_so "$TMP/libfixture.so"
    SO_SYM=$(addr_of "$TMP/libfixture.so" so_func)
    [ -n "$SO_SYM" ] || fail "10e-2 so_func symbol not found"
    OUT_SO=$($BIN -f -e "$TMP/libfixture.so" 0x$SO_SYM)
    expect_eq "10e-2" "$(first_line "$OUT_SO")" "so_func"

    pass "all"
}

main "$@"
