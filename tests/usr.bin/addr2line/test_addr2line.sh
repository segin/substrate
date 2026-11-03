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

build_fixture_many_cus() {
    out=$1
    count=$2
    objs=
    i=0
    while [ "$i" -lt "$count" ]; do
        src="$TMP/unit_$i.c"
        obj="$TMP/unit_$i.o"
        cat > "$src" <<SRC
int unit_func_$i(void) { return $i; }
SRC
        cc -g -gdwarf-4 -c -o "$obj" "$src"
        objs="$objs $obj"
        i=$((i + 1))
    done
    last=$((count - 1))
    cat > "$TMP/unit_main.c" <<SRC
extern int unit_func_0(void);
extern int unit_func_$last(void);
int main(void) { return unit_func_0() + unit_func_$last(); }
SRC
    cc -g -gdwarf-4 -c -o "$TMP/unit_main.o" "$TMP/unit_main.c"
    # shellcheck disable=SC2086
    cc -g -gdwarf-4 -no-pie -o "$out" $objs "$TMP/unit_main.o"
}

build_fixture_a_out() {
    dir=$1
    mkdir -p "$dir"
    cat > "$dir/main_integ.c" <<'SRC'
int integ(int x) { return x + 21; }
int main(void) { return integ(2); }
SRC
    cc -g -gdwarf-4 -no-pie -o "$dir/a.out" "$dir/main_integ.c"
}

build_fixture_headers() {
    dir=$1
    mkdir -p "$dir/inc"
    cat > "$dir/inc/helper.h" <<'SRC'
static int helper_one(int x)
{
    return x * 3;
}
static int helper_two(int x)
{
    return helper_one(x) + 1;
}
SRC
    cat > "$dir/other.h" <<'SRC'
static int other_fn(int x)
{
    return x - 2;
}
SRC
    cat > "$dir/hdr_main.c" <<'SRC'
#include "helper.h"
#include "other.h"
int main(void)
{
    return helper_two(4) + other_fn(5);
}
SRC
}

build_fixture_file_zero() {
    dir=$1
    mkdir -p "$dir"
    cat > "$dir/fzero.s" <<'SRC'
	.file 0 "/srcroot/zero" "zero.c"
	.file 1 "/srcroot/zero" "zero.c"
	.file 2 "/srcroot/inc" "part.h"
	.text
	.globl zfunc
	.type zfunc, @function
zfunc:
	.loc 0 7 0
	nop
	.loc 2 3 0
	nop
	.loc 0 9 0
	ret
	.size zfunc, .-zfunc
SRC
    cat > "$dir/fzero_main.c" <<'SRC'
int zfunc(void);
int main(void) { zfunc(); return 0; }
SRC
    as --gdwarf-5 -o "$dir/fzero.o" "$dir/fzero.s"
    cc -no-pie -o "$dir/fzero" "$dir/fzero_main.c" "$dir/fzero.o"
}

build_fixture_second_cu_inline() {
    dir=$1
    mkdir -p "$dir"
    cat > "$dir/cu_a.c" <<'SRC'
static inline __attribute__((always_inline)) int a_leaf(volatile int *p) { return *p * 5 + 2; }
int a_one(int x) { volatile int v = x; return a_leaf(&v) + 1; }
int a_two(int x) { volatile int v = x; return a_leaf(&v) - 1; }
int a_three(int x) { volatile int v = x; return a_leaf(&v) ^ 3; }
SRC
    cat > "$dir/cu_b.c" <<'SRC'
static inline __attribute__((always_inline)) int b_leaf(volatile int *p) { return *p * 7 + 1; }
static inline __attribute__((always_inline)) int b_mid(volatile int *p) { return b_leaf(p) + b_leaf(p); }
int b_outer(int x) { volatile int v = x; return b_mid(&v) + 3; }
SRC
    cat > "$dir/cu_main.c" <<'SRC'
int a_one(int), a_two(int), a_three(int), b_outer(int);
int main(int c, char **v) { (void)v; return a_one(c) + a_two(c) + a_three(c) + b_outer(c); }
SRC
}

build_fixture_named_by_ref() {
    dir=$1
    mkdir -p "$dir"
    # Out-of-line member definitions: their DW_TAG_subprogram carries
    # only DW_AT_specification, pointing at the declaration in the class.
    cat > "$dir/ref_cls.cpp" <<'SRC'
namespace Geo {
struct Shape {
    int scale(int x);
    static int twice(int x);
};
int Shape::scale(int x) { return x * 3; }
int Shape::twice(int x) { return x * 2; }
}
int run_shape(int x) { Geo::Shape s; return s.scale(x) + Geo::Shape::twice(x); }
SRC
    # An inline function that is also emitted out of line (its address
    # escapes): the concrete instance carries only DW_AT_abstract_origin.
    cat > "$dir/ref_inl.c" <<'SRC'
static inline int ref_body(volatile int *p) { return *p * 9 + 4; }
int (*volatile ref_ptr)(volatile int *) = ref_body;
int ref_user(int x) { volatile int v = x; return ref_body(&v) + ref_ptr(&v); }
SRC
    cat > "$dir/ref_main.cpp" <<'SRC'
int run_shape(int);
extern "C" int ref_user(int);
int main(int c, char **) { return run_shape(c) + ref_user(c); }
SRC
}

build_fixture_two_level_inline() {
    dir=$1
    mkdir -p "$dir"
    # Every function body sits on its own lines, so each frame of the
    # inline chain has a distinct source line to be paired with.
    cat > "$dir/two_level.c" <<'SRC'
static inline __attribute__((always_inline)) int tl_inner(volatile int *p)
{
    return *p * 7 + 1;
}
static inline __attribute__((always_inline)) int tl_mid(volatile int *p)
{
    return tl_inner(p) + 5;
}
int tl_outer(int x)
{
    volatile int v = x;
    return tl_mid(&v) * 3;
}
int main(int c, char **v) { (void)v; return tl_outer(c); }
SRC
}

# Hand-written x86-64 line programs in two units.  overlap.c has two
# sequences, one nested inside the other: [ofunc, ofunc+0x40) at line
# 20 and [ofunc+8, ofunc+0xc) at line 30.  unterm.c has one sequence
# for ufunc (line 10 at +0, line 11 at +4) that the program never ends
# with DW_LNE_end_sequence.  ufunc is the last code with line rows, so
# everything above it (main among it) is past the end of every sequence.
build_fixture_sequences() {
    dir=$1
    mkdir -p "$dir"
    cat > "$dir/seq.s" <<'SRC'
	.text
	.globl ofunc
	.type ofunc, @function
ofunc:
	.fill 0x3f, 1, 0x90
	ret
	.size ofunc, .-ofunc
	.globl ufunc
	.type ufunc, @function
ufunc:
	.fill 0xf, 1, 0x90
	ret
	.size ufunc, .-ufunc

	.section .debug_abbrev,"",@progbits
.Labbrev:
	.uleb128 1
	.uleb128 0x11
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 0x10
	.uleb128 0x17
	.byte 0, 0
	.byte 0

	.section .debug_info,"",@progbits
	.long .Lcu1_end - .Lcu1_ver
.Lcu1_ver:
	.short 4
	.long .Labbrev
	.byte 8
	.uleb128 1
	.asciz "unterm.c"
	.long .Lline1
.Lcu1_end:
	.long .Lcu2_end - .Lcu2_ver
.Lcu2_ver:
	.short 4
	.long .Labbrev
	.byte 8
	.uleb128 1
	.asciz "overlap.c"
	.long .Lline2
.Lcu2_end:

	.section .debug_line,"",@progbits
.Lline1:
	.long .Lline1_end - .Lline1_ver
.Lline1_ver:
	.short 4
	.long .Lline1_prog - .Lline1_hdr
.Lline1_hdr:
	.byte 1, 1, 1, -5, 14, 13
	.byte 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1
	.asciz "/srcroot/seq"
	.byte 0
	.asciz "unterm.c"
	.uleb128 1, 0, 0
	.byte 0
.Lline1_prog:
	.byte 0, 9, 2
	.quad ufunc
	.byte 3
	.sleb128 9
	.byte 1
	.byte 2
	.uleb128 4
	.byte 3
	.sleb128 1
	.byte 1
.Lline1_end:
.Lline2:
	.long .Lline2_end - .Lline2_ver
.Lline2_ver:
	.short 4
	.long .Lline2_prog - .Lline2_hdr
.Lline2_hdr:
	.byte 1, 1, 1, -5, 14, 13
	.byte 0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1
	.asciz "/srcroot/seq"
	.byte 0
	.asciz "overlap.c"
	.uleb128 1, 0, 0
	.byte 0
.Lline2_prog:
	.byte 0, 9, 2
	.quad ofunc
	.byte 3
	.sleb128 19
	.byte 1
	.byte 2
	.uleb128 0x40
	.byte 0, 1, 1
	.byte 0, 9, 2
	.quad ofunc + 8
	.byte 3
	.sleb128 29
	.byte 1
	.byte 2
	.uleb128 4
	.byte 0, 1, 1
.Lline2_end:
SRC
    cat > "$dir/seq_main.c" <<'SRC'
int ofunc(void), ufunc(void);
int main(void) { ofunc(); ufunc(); return 0; }
SRC
}

# A compile unit whose DW_AT_name is followed by one attribute that runs
# past the end of the unit: $2 is the attribute code, $3 its form.  The
# unit holds a single byte of that attribute's value, too few for any of
# the forms used, so the value can neither be read nor skipped.
build_fixture_truncated_attr() {
    out=$1
    attr=$2
    form=$3
    cat > "$out" <<SRC
	.text
	.globl tfunc
	.type tfunc, @function
tfunc:
	ret
	.size tfunc, .-tfunc

	.section .debug_abbrev,"",@progbits
.Labbrev:
	.uleb128 1
	.uleb128 0x11
	.byte 0
	.uleb128 0x03
	.uleb128 0x08
	.uleb128 $attr
	.uleb128 $form
	.byte 0, 0
	.byte 0

	.section .debug_info,"",@progbits
	.long .Lcu_end - .Lcu_ver
.Lcu_ver:
	.short 4
	.long .Labbrev
	.byte 8
	.uleb128 1
	.asciz "truncated.c"
	.byte 0
.Lcu_end:
SRC
}

# Normalise addr2line output for an exact comparison: GNU zero-pads
# -a addresses to the target's width and appends "(discriminator N)",
# neither of which is what the inline-frame checks are about.
norm_a2l() {
    printf '%s\n' "$1" | sed -e 's/^0x0*\([0-9a-f]\)/0x\1/' \
                             -e 's/ (discriminator [0-9]*)$//'
}

# Function names only (the odd lines) of "-f -i" output.
inline_names() {
    printf '%s\n' "$1" | awk 'NR % 2 == 1' | tr '\n' ' '
}

first_line() {
    printf '%s\n' "$1" | sed -n '1p'
}

addr_of() {
    obj=$1
    sym=$2
    nm -n "$obj" | awk -v s="$sym" '$3==s {print $1; exit}'
}

# Write VAL as N little-endian bytes at byte offset OFF of FILE.
put_le() {
    f=$1
    off=$2
    val=$3
    n=$4
    i=0
    while [ "$i" -lt "$n" ]; do
        # shellcheck disable=SC2059
        printf "\\$(printf '%03o' $(( (val >> (8 * i)) & 255 )))"
        i=$((i + 1))
    done | dd of="$f" bs=1 seek="$off" conv=notrunc 2>/dev/null
}

# Rewrite a little-endian ELF (32- or 64-bit) to use ELF's extended
# section numbering: e_shnum 0 with the real count in section 0's
# sh_size, and e_shstrndx SHN_XINDEX with the real index in its sh_link.
make_extended_numbering() {
    src=$1
    out=$2
    cls=$3
    shoff=$(readelf -hW "$src" | awk '/Start of section headers/ {print $5}')
    shnum=$(readelf -hW "$src" | awk '/Number of section headers/ {print $5}')
    strndx=$(readelf -hW "$src" | awk '/Section header string table index/ {print $6}')
    cp "$src" "$out"
    if [ "$cls" = 64 ]; then
        put_le "$out" 60 0 2
        put_le "$out" 62 65535 2
        put_le "$out" $((shoff + 32)) "$shnum" 8
        put_le "$out" $((shoff + 40)) "$strndx" 4
    else
        put_le "$out" 48 0 2
        put_le "$out" 50 65535 2
        put_le "$out" $((shoff + 20)) "$shnum" 4
        put_le "$out" $((shoff + 24)) "$strndx" 4
    fi
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

    # 10f-1: stripped binary with no DWARF/symbols -> unresolved output
    require_cmd strip
    cp "$TMP/exec64_v5" "$TMP/exec64_stripped"
    strip --strip-all "$TMP/exec64_stripped"
    OUT_STRIP=$($BIN -f -e "$TMP/exec64_stripped" 0x$A5)
    [ "$(printf '%s\n' "$OUT_STRIP" | sed -n '1p')" = "??" ] || \
        fail "10f-1 expected unresolved function"
    [ "$(printf '%s\n' "$OUT_STRIP" | sed -n '2p')" = "??:0" ] || \
        fail "10f-1 expected unresolved file:line"
    pass "10f-1"

    # 10f-2: address 0x0 is accepted as a valid query
    OUT_ZERO=$($BIN -e "$TMP/exec64_v5" 0x0 2>"$TMP/err_10f2")
    [ -n "$OUT_ZERO" ] || fail "10f-2 expected output for address 0x0"
    if grep -E 'invalid address' "$TMP/err_10f2" >/dev/null 2>&1; then
        fail "10f-2 0x0 should not be treated as invalid input"
    fi
    pass "10f-2"

    # 10f-3: address beyond .text end -> unresolved
    TEXT_FIELDS=$(readelf -SW "$TMP/exec64_v5" | \
        awk '$2==".text" {print $4" "$6; exit} $3==".text" {print $5" "$7; exit}')
    TEXT_BASE=$(printf '%s\n' "$TEXT_FIELDS" | awk '{print $1}')
    TEXT_SIZE=$(printf '%s\n' "$TEXT_FIELDS" | awk '{print $2}')
    [ -n "$TEXT_BASE" ] || fail "10f-3 missing .text base"
    [ -n "$TEXT_SIZE" ] || fail "10f-3 missing .text size"
    BEYOND_HEX=$(printf '%x' $((0x$TEXT_BASE + 0x$TEXT_SIZE + 0x200)))
    OUT_BEYOND=$($BIN -e "$TMP/exec64_v5" 0x$BEYOND_HEX)
    expect_eq "10f-3" "$OUT_BEYOND" "??:0"

    # 10f-4: empty stdin input produces no output
    OUT_EMPTY=$(printf '' | "$BIN" -e "$TMP/exec64_v5")
    [ -z "$OUT_EMPTY" ] || fail "10f-4 expected no output for empty stdin"
    pass "10f-4"

    # 10f-5: large DWARF with many CUs resolves without crashing
    build_fixture_many_cus "$TMP/many_cu" 48
    MANY_ADDR=$(addr_of "$TMP/many_cu" unit_func_37)
    [ -n "$MANY_ADDR" ] || fail "10f-5 unit_func_37 symbol not found"
    OUT_MANY=$($BIN -e "$TMP/many_cu" 0x$MANY_ADDR)
    expect_nonempty_match "10f-5" "$OUT_MANY" 'unit_37\.c:[0-9]+'

    # 10g-1: -f -s prints function and basename-only path
    OUT_FS=$($BIN -f -s -e "$TMP/exec64_v5" 0x$A5)
    expect_eq "10g-1-func" "$(printf '%s\n' "$OUT_FS" | sed -n '1p')" "f1"
    FS_LOC=$(printf '%s\n' "$OUT_FS" | sed -n '2p')
    expect_nonempty_match "10g-1-loc" "$FS_LOC" '^sample\.c:[0-9]+$'

    # 10g-2: -f -C -i -p yields one-line pretty inline frames
    OUT_FCIP=$($BIN -f -C -i -p -e "$TMP/rust_inline" 0x$INLINE_ADDR)
    FCIP_LINES=$(printf '%s\n' "$OUT_FCIP" | wc -l | tr -d ' ')
    [ "$FCIP_LINES" -ge 2 ] || fail "10g-2 expected multiple pretty inline lines"
    expect_nonempty_match "10g-2" "$(first_line "$OUT_FCIP")" '^.+ at .+:[0-9]+$'

    # 10g-3: -a prepends address column
    OUT_A=$($BIN -a -e "$TMP/exec64_v5" 0x$A5)
    A5_CANON=$(printf '%x' $((0x$A5)))
    expect_eq "10g-3" "$(first_line "$OUT_A")" "0x$A5_CANON"

    # 10g-4: stdin mode resolves one output per input line
    OUT_STDIN=$(printf '0x%s\n0x%s\n0x%s\n' "$A0" "$A5" "$AM" | \
        "$BIN" -e "$TMP/exec64_v5")
    STDIN_LINES=$(printf '%s\n' "$OUT_STDIN" | wc -l | tr -d ' ')
    expect_eq "10g-4" "$STDIN_LINES" "3"

    # 10h-1: .zdebug_line (zlib-gnu) decompression
    OUT_BASE_COMP=$($BIN -e "$TMP/exec64_v5" 0x$A5)
    objcopy --compress-debug-sections=zlib-gnu "$TMP/exec64_v5" "$TMP/exec64_zgnu"
    OUT_ZGNU=$($BIN -e "$TMP/exec64_zgnu" 0x$A5)
    expect_eq "10h-1" "$OUT_ZGNU" "$OUT_BASE_COMP"

    # 10h-2: SHF_COMPRESSED .debug_line decompression
    objcopy --compress-debug-sections=zlib "$TMP/exec64_v5" "$TMP/exec64_shfcomp"
    OUT_SHFC=$($BIN -e "$TMP/exec64_shfcomp" 0x$A5)
    expect_eq "10h-2" "$OUT_SHFC" "$OUT_BASE_COMP"

    # 10i-1: compile to a.out and resolve a known function address
    build_fixture_a_out "$TMP/integration"
    INTEG_ADDR=$(addr_of "$TMP/integration/a.out" integ)
    [ -n "$INTEG_ADDR" ] || fail "10i-1 integ symbol not found"
    OUT_10I=$(
        cd "$TMP/integration"
        "$BIN" 0x$INTEG_ADDR
    )
    expect_nonempty_match "10i-1" "$OUT_10I" 'main_integ\.c:[0-9]+'

    # 10i-2: compare output with host addr2line on same binary
    HOST_10I=$(
        cd "$TMP/integration"
        addr2line 0x$INTEG_ADDR
    )
    expect_eq "10i-2" "$OUT_10I" "$HOST_10I"

    # 10j: DWARF 5 numbers the line-table file and directory entries
    # from 0 (file 0 = primary source, directory 0 = compilation
    # directory); DWARF 4 numbers them from 1.  Every path here is
    # absolute so host addr2line's output can be compared verbatim.
    build_fixture_headers "$TMP/hdr"
    for V in 4 5; do
        cc -O0 -g -gdwarf-$V -no-pie -I"$TMP/hdr/inc" \
            -o "$TMP/hdr/hdr_v$V" "$TMP/hdr/hdr_main.c"
        for S in helper_one helper_two other_fn main; do
            HA=$(addr_of "$TMP/hdr/hdr_v$V" "$S")
            [ -n "$HA" ] || fail "10j-1 v$V symbol $S not found"
            expect_eq "10j-1 v$V $S" \
                "$($BIN -e "$TMP/hdr/hdr_v$V" 0x$HA)" \
                "$(addr2line -e "$TMP/hdr/hdr_v$V" 0x$HA)"
        done
    done

    # 10j-2: a DWARF 5 line program that names file 0 directly (clang
    # does this for the primary file), and a file under directory 1.
    require_cmd as
    build_fixture_file_zero "$TMP/fzero"
    ZA=$(addr_of "$TMP/fzero/fzero" zfunc)
    [ -n "$ZA" ] || fail "10j-2 symbol zfunc not found"
    for OFF in 0 1 2; do
        ZQ=$(printf '%x' $((0x$ZA + OFF)))
        expect_eq "10j-2 +$OFF" \
            "$($BIN -e "$TMP/fzero/fzero" 0x$ZQ)" \
            "$(addr2line -e "$TMP/fzero/fzero" 0x$ZQ)"
    done

    # 10k: DW_AT_abstract_origin in DW_FORM_ref4 (and the other
    # ref1/2/8/ref_udata forms) is an offset from the start of its own
    # unit.  Inlined frames in any unit after the first must still be
    # named, so every address of b_outer (second unit, two levels of
    # inlining) must give host addr2line's chain of function names.
    build_fixture_second_cu_inline "$TMP/cu2"
    for V in 4 5; do
        cc -O2 -g -gdwarf-$V -no-pie -o "$TMP/cu2/cu2_v$V" \
            "$TMP/cu2/cu_a.c" "$TMP/cu2/cu_b.c" "$TMP/cu2/cu_main.c"
        BO=$(addr_of "$TMP/cu2/cu2_v$V" b_outer)
        BS=$(nm -S "$TMP/cu2/cu2_v$V" | awk '$4=="b_outer" {print $2; exit}')
        [ -n "$BO" ] && [ -n "$BS" ] || fail "10k v$V symbol b_outer not found"
        SEEN_LEAF=0
        OFF=0
        while [ "$OFF" -lt $((0x$BS)) ]; do
            Q=$(printf '%x' $((0x$BO + OFF)))
            HOST_N=$(inline_names "$(addr2line -f -i -e "$TMP/cu2/cu2_v$V" 0x$Q)")
            OUR_N=$(inline_names "$($BIN -f -i -e "$TMP/cu2/cu2_v$V" 0x$Q)")
            [ "$OUR_N" = "$HOST_N" ] || {
                printf 'FAIL: 10k v%s +%s\n  got : [%s]\n  want: [%s]\n' \
                    "$V" "$OFF" "$OUR_N" "$HOST_N" >&2
                exit 1
            }
            case $HOST_N in
            "b_leaf b_mid b_outer "*) SEEN_LEAF=1 ;;
            esac
            OFF=$((OFF + 1))
        done
        [ "$SEEN_LEAF" -eq 1 ] || fail "10k v$V no address inside inlined b_leaf"
        pass "10k v$V"
    done

    # 10l: a subprogram with code but no name of its own (an out-of-line
    # C++ member definition via DW_AT_specification, an out-of-line copy
    # of an inline function via DW_AT_abstract_origin) is named through
    # that reference.  The symbol table is removed so nothing but the
    # DWARF can supply the name; GNU addr2line is the judge.
    require_cmd c++
    require_cmd objcopy
    require_cmd readelf
    build_fixture_named_by_ref "$TMP/nref"
    for V in 4 5; do
        cc -O1 -g -gdwarf-$V -c -o "$TMP/nref/ref_inl_v$V.o" \
            "$TMP/nref/ref_inl.c"
        c++ -O0 -g -gdwarf-$V -no-pie -o "$TMP/nref/nref_v$V" \
            "$TMP/nref/ref_cls.cpp" "$TMP/nref/ref_main.cpp" \
            "$TMP/nref/ref_inl_v$V.o"
        objcopy --strip-all --keep-section='.debug_*' "$TMP/nref/nref_v$V" \
            "$TMP/nref/nref_v${V}_nosym"
        if readelf -S "$TMP/nref/nref_v${V}_nosym" | grep -F '.symtab' >/dev/null; then
            fail "10l v$V symbol table survived the strip"
        fi
        for S in _ZN3Geo5Shape5scaleEi _ZN3Geo5Shape5twiceEi ref_body; do
            RA=$(addr_of "$TMP/nref/nref_v$V" "$S")
            [ -n "$RA" ] || fail "10l v$V symbol $S not found"
            HOST_R=$(addr2line -f -C -e "$TMP/nref/nref_v${V}_nosym" 0x$RA)
            case $HOST_R in
            '??'*) fail "10l v$V host addr2line cannot name $S" ;;
            esac
            expect_eq "10l v$V $S" \
                "$($BIN -f -C -e "$TMP/nref/nref_v${V}_nosym" 0x$RA)" \
                "$HOST_R"
        done
    done

    # 10m: an address inside a two-level inline chain (tl_inner inlined
    # into tl_mid inlined into tl_outer).  Each function name must be
    # paired with its own source line: the innermost frame with the
    # .debug_line row, every outer frame with the call site recorded on
    # the frame it inlined.  Without -i the innermost function is named.
    # With -a the address is printed once, and in -p mode every later
    # frame starts with " (inlined by) ".  GNU addr2line is the judge,
    # over every address of tl_outer.
    build_fixture_two_level_inline "$TMP/tl"
    for V in 4 5; do
        cc -O2 -g -gdwarf-$V -no-pie -o "$TMP/tl/tl_v$V" "$TMP/tl/two_level.c"
        TO=$(addr_of "$TMP/tl/tl_v$V" tl_outer)
        TS=$(nm -S "$TMP/tl/tl_v$V" | awk '$4=="tl_outer" {print $2; exit}')
        [ -n "$TO" ] && [ -n "$TS" ] || fail "10m v$V symbol tl_outer not found"
        SEEN_INNER=0
        OFF=0
        while [ "$OFF" -lt $((0x$TS)) ]; do
            Q=$(printf '%x' $((0x$TO + OFF)))
            for FLAGS in "-f" "-f -i" "-i" "-a -f -i" "-f -i -p" "-f -i -p -a" \
                         "-f -p -a"; do
                HOST_O=$(norm_a2l "$(addr2line $FLAGS -e "$TMP/tl/tl_v$V" 0x$Q)")
                OUR_O=$(norm_a2l "$($BIN $FLAGS -e "$TMP/tl/tl_v$V" 0x$Q)")
                [ "$OUR_O" = "$HOST_O" ] || {
                    printf 'FAIL: 10m v%s +%s %s\n  got :\n%s\n  want:\n%s\n' \
                        "$V" "$OFF" "$FLAGS" "$OUR_O" "$HOST_O" >&2
                    exit 1
                }
            done
            case $(inline_names "$(addr2line -f -i -e "$TMP/tl/tl_v$V" 0x$Q)") in
            "tl_inner tl_mid tl_outer ") SEEN_INNER=1 ;;
            esac
            OFF=$((OFF + 1))
        done
        [ "$SEEN_INNER" -eq 1 ] || fail "10m v$V no address inside inlined tl_inner"
        pass "10m v$V"
    done

    # 10n: line-number sequences bound the addresses they resolve.  A
    # sequence the program never ends ends at its last row, so its last
    # row and every address above it (main here) resolve to nothing, not
    # to that row.  Where one sequence is nested inside another, an
    # address the outer one covers resolves through it, even when the
    # nearest lower row belongs to the inner one.  GNU addr2line is the
    # judge for every byte of ofunc and ufunc and for main; it prints an
    # unresolved line in a unit with no address ranges as "??:?", which
    # is mapped to the "??:0" addr2line prints.
    require_cmd as
    build_fixture_sequences "$TMP/seq"
    as -o "$TMP/seq/seq.o" "$TMP/seq/seq.s"
    cc -no-pie -o "$TMP/seq/seq" "$TMP/seq/seq.o" "$TMP/seq/seq_main.c"
    for S in ofunc ufunc main; do
        SA=$(addr_of "$TMP/seq/seq" "$S")
        [ -n "$SA" ] || fail "10n symbol $S not found"
        case $S in
        ofunc) SN=64 ;;
        ufunc) SN=16 ;;
        main) SN=1 ;;
        esac
        OFF=0
        while [ "$OFF" -lt "$SN" ]; do
            Q=$(printf '%x' $((0x$SA + OFF)))
            HOST_Q=$(addr2line -e "$TMP/seq/seq" 0x$Q | sed 's/^??:?$/??:0/')
            expect_eq "10n $S+$OFF" "$($BIN -e "$TMP/seq/seq" 0x$Q)" "$HOST_Q"
            OFF=$((OFF + 1))
        done
    done
    # The cases the GNU comparison above must have covered.
    UA=$(addr_of "$TMP/seq/seq" ufunc)
    OA=$(addr_of "$TMP/seq/seq" ofunc)
    expect_eq "10n unterminated last row" \
        "$($BIN -e "$TMP/seq/seq" 0x$(printf '%x' $((0x$UA + 4))))" "??:0"
    expect_eq "10n past every sequence" \
        "$($BIN -e "$TMP/seq/seq" 0x$(addr_of "$TMP/seq/seq" main))" "??:0"
    expect_eq "10n outer sequence beyond the nested one" \
        "$($BIN -e "$TMP/seq/seq" 0x$(printf '%x' $((0x$OA + 0x20))))" \
        "/srcroot/seq/overlap.c:20"

    # 10o: a DIE whose DW_AT_name is followed by a truncated low_pc,
    # high_pc, ranges, stmt_list, call_file or abstract_origin makes
    # .debug_info malformed.  addr2line warns, drops what it parsed and
    # goes on resolving, so the name it had already read must be freed,
    # not leaked.  A copy of addr2line built with AddressSanitizer runs
    # each case and LeakSanitizer must find nothing.
    if cc -fsanitize=address -x c - -o "$TMP/asan_probe" >/dev/null 2>&1 <<'SRC'
int main(void) { return 0; }
SRC
    then
        mkdir -p "$TMP/trunc"
        cc -fsanitize=address -fno-omit-frame-pointer -O1 -g \
            -idirafter "$ROOT/include" -DADDR2LINE_HAVE_ZLIB \
            -o "$TMP/trunc/addr2line_asan" "$ROOT/usr.bin/addr2line/addr2line.c" \
            "$ROOT/usr.lib/elfobj/libelfobj.a" \
            "$ROOT/usr.lib/demangle/libdemangle.a" -lz
        printf 'int tfunc(void);\nint main(void) { return tfunc(); }\n' \
            > "$TMP/trunc/main.c"
        for CASE in low_pc:0x11:0x01 high_pc:0x12:0x01 ranges:0x55:0x17 \
                    stmt_list:0x10:0x17 call_file:0x58:0x06 \
                    abstract_origin:0x31:0x13; do
            AN=${CASE%%:*}
            AF=${CASE#*:}
            build_fixture_truncated_attr "$TMP/trunc/$AN.s" "${AF%%:*}" "${AF#*:}"
            as -o "$TMP/trunc/$AN.o" "$TMP/trunc/$AN.s"
            cc -no-pie -o "$TMP/trunc/$AN" "$TMP/trunc/$AN.o" "$TMP/trunc/main.c"
            TA=$(addr_of "$TMP/trunc/$AN" tfunc)
            [ -n "$TA" ] || fail "10o $AN symbol tfunc not found"
            RC=0
            ASAN_OPTIONS=detect_leaks=1:exitcode=99 \
                "$TMP/trunc/addr2line_asan" -f -e "$TMP/trunc/$AN" 0x$TA \
                > "$TMP/trunc/$AN.out" 2> "$TMP/trunc/$AN.err" || RC=$?
            grep -F 'malformed .debug_info' "$TMP/trunc/$AN.err" >/dev/null ||
                fail "10o $AN fixture did not make .debug_info malformed"
            if [ "$RC" -ne 0 ] || grep -F 'LeakSanitizer' "$TMP/trunc/$AN.err" >/dev/null; then
                cat "$TMP/trunc/$AN.err" >&2
                fail "10o $AN leaked (exit $RC)"
            fi
            pass "10o $AN"
        done
    else
        printf 'skip: 10o (cc cannot build with -fsanitize=address)\n'
    fi

    # 10p: section addresses come from the object elf_open() parsed, so
    # an image using ELF's extended section numbering (e_shnum 0,
    # e_shstrndx SHN_XINDEX) still has its sections, and -j finds them.
    # GNU addr2line is the judge.
    require_cmd readelf
    require_cmd dd
    mkdir -p "$TMP/xnum"
    for XC in 64 32; do
        if [ "$XC" = 64 ]; then
            XSRC="$TMP/exec64_v5"
        else
            XSRC="$TMP/exec32_v4"
            if [ ! -f "$XSRC" ]; then
                pass "10p-1 elf32 skipped (no -m32 toolchain)"
                continue
            fi
        fi
        XA=$(addr_of "$XSRC" f1)
        [ -n "$XA" ] || fail "10p-1 elf$XC symbol f1 not found"
        XT=$(readelf -SW "$XSRC" | \
            awk '$2==".text" {print $4; exit} $3==".text" {print $5; exit}')
        [ -n "$XT" ] || fail "10p-1 elf$XC .text address not found"
        XOFF=$(printf '%x' $((0x$XA - 0x$XT)))
        make_extended_numbering "$XSRC" "$TMP/xnum/x$XC" "$XC"
        readelf -hW "$TMP/xnum/x$XC" | grep -E 'Number of section headers: +0 \(' >/dev/null ||
            fail "10p-1 elf$XC fixture does not use extended numbering"
        HOST_X=$(addr2line -j .text -e "$TMP/xnum/x$XC" 0x$XOFF)
        OUT_X=$($BIN -j .text -e "$TMP/xnum/x$XC" 0x$XOFF 2>"$TMP/xnum/err$XC") ||
            { cat "$TMP/xnum/err$XC" >&2; fail "10p-1 elf$XC -j .text failed"; }
        expect_eq "10p-1 elf$XC -j .text" "$OUT_X" "$HOST_X"
        expect_eq "10p-1 elf$XC stderr" "$(cat "$TMP/xnum/err$XC")" ""
    done

    # 10p-2: an ELF32 executable that is only its 52-byte header (no
    # program or section headers) is a valid, empty image: GNU prints
    # "??:0" and nothing on stderr, and so must addr2line.
    H32="$TMP/xnum/hdr32"
    printf '\177ELF\001\001\001' > "$H32"
    dd if=/dev/zero bs=1 count=45 2>/dev/null >> "$H32"
    put_le "$H32" 16 2 2        # e_type ET_EXEC
    put_le "$H32" 18 3 2        # e_machine EM_386
    put_le "$H32" 20 1 4        # e_version
    put_le "$H32" 40 52 2       # e_ehsize
    [ "$(wc -c < "$H32" | tr -d ' ')" = 52 ] || fail "10p-2 fixture is not 52 bytes"
    HOST_H=$(addr2line -e "$H32" 0x10 2>"$TMP/xnum/hgnu.err")
    OUT_H=$($BIN -e "$H32" 0x10 2>"$TMP/xnum/h.err")
    expect_eq "10p-2 header-only elf32" "$OUT_H" "$HOST_H"
    expect_eq "10p-2 header-only elf32 stderr" "$(cat "$TMP/xnum/h.err")" \
        "$(cat "$TMP/xnum/hgnu.err")"

    pass "all"
}

main "$@"
