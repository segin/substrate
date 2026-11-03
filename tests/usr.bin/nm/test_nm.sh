#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
NM="$ROOT/usr.bin/nm/nm"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/nm-tests.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

pass() { printf 'ok: %s\n' "$1"; }
fail() { printf 'FAIL: %s\n' "$1" >&2; exit 1; }

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || fail "missing command: $1"
}

expect_match() {
    label=$1
    text=$2
    pattern=$3
    printf '%s\n' "$text" | grep -E "$pattern" >/dev/null 2>&1 || {
        printf 'FAIL: %s\npattern: %s\ntext:\n%s\n' "$label" "$pattern" "$text" >&2
        exit 1
    }
    pass "$label"
}

expect_nomatch() {
    label=$1
    text=$2
    pattern=$3
    printf '%s\n' "$text" | grep -E "$pattern" >/dev/null 2>&1 && {
        printf 'FAIL: %s\nunexpected pattern: %s\ntext:\n%s\n' "$label" "$pattern" "$text" >&2
        exit 1
    }
    pass "$label"
}

main() {
    require_cmd cc
    require_cmd ar

    [ -x "$NM" ] || fail "nm binary not found: $NM"

    cat > "$TMP/sample.c" <<'SRC'
int g_data = 7;
int g_bss;
static int s_data = 3;
static int s_bss;
extern int ext_ref;

int use_ext(void) {
    return ext_ref + g_data + g_bss + s_data + s_bss;
}
SRC

    cc -c -o "$TMP/sample.o" "$TMP/sample.c"

    OUT=$($NM "$TMP/sample.o")
    expect_match "object has global text" "$OUT" '[[:space:]]T[[:space:]]+use_ext$'
    expect_match "object has global data" "$OUT" '[[:space:]]D[[:space:]]+g_data$'
    expect_match "object has global bss" "$OUT" '[[:space:]]B[[:space:]]+g_bss$'
    expect_match "object has local data" "$OUT" '[[:space:]]d[[:space:]]+s_data$'
    expect_match "object has local bss" "$OUT" '[[:space:]]b[[:space:]]+s_bss$'
    expect_match "object has undefined" "$OUT" '[[:space:]]U[[:space:]]+ext_ref$'

    OUT_U=$($NM -u "$TMP/sample.o")
    expect_match "-u keeps undefined" "$OUT_U" '[[:space:]]U[[:space:]]+ext_ref$'
    expect_nomatch "-u drops defined text" "$OUT_U" '[[:space:]]T[[:space:]]+use_ext$'

    OUT_G=$($NM -g "$TMP/sample.o")
    expect_match "-g keeps external" "$OUT_G" '[[:space:]]D[[:space:]]+g_data$'
    expect_nomatch "-g drops local" "$OUT_G" '[[:space:]]d[[:space:]]+s_data$'

    OUT_P=$($NM -P "$TMP/sample.o")
    expect_match "-P format includes name/type/value/size" "$OUT_P" '^use_ext[[:space:]]+T[[:space:]]+[0-9a-fA-F]+[[:space:]]+[0-9a-fA-F]+$'

    cat > "$TMP/sample.cpp" <<'SRC'
namespace Ns {
int FooBar(int x) { return x + 1; }
}
SRC
    c++ -c -o "$TMP/sample_cpp.o" "$TMP/sample.cpp"

    OUT_RAW=$($NM "$TMP/sample_cpp.o")
    expect_match "mangled C++ visible without -C" "$OUT_RAW" '_ZN2Ns6FooBarEi'

    OUT_DEM=$($NM -C "$TMP/sample_cpp.o")
    expect_match "demangled with -C" "$OUT_DEM" 'Ns::FooBar\(int\)'

    cat > "$TMP/a.c" <<'SRC'
int alpha(void) { return 1; }
SRC
    cat > "$TMP/b.c" <<'SRC'
int beta(void) { return 2; }
SRC
    cc -c -o "$TMP/a.o" "$TMP/a.c"
    cc -c -o "$TMP/b.o" "$TMP/b.c"
    ar rcs "$TMP/libab.a" "$TMP/a.o" "$TMP/b.o"

    OUT_AR=$($NM "$TMP/libab.a")
    expect_match "archive shows first member" "$OUT_AR" 'libab\.a\[a\.o\]:'
    expect_match "archive shows second member" "$OUT_AR" 'libab\.a\[b\.o\]:'
    expect_match "archive member symbol alpha" "$OUT_AR" '[[:space:]]T[[:space:]]+alpha$'
    expect_match "archive member symbol beta" "$OUT_AR" '[[:space:]]T[[:space:]]+beta$'

    # --size-sort: ascending by size, undefined and size-0 symbols are
    # dropped, and without -S the BSD value column carries the size.
    cat > "$TMP/sizes.c" <<'SRC'
extern int ext_sz;
char big_arr[300] = { 1 };
short mid_arr[40] = { 2 };
int small_var = 3;
__asm__(".globl zero_sym\n.data\nzero_sym:\n.text\n");
int sz_fn(void) { return ext_sz + big_arr[0] + mid_arr[0] + small_var; }
SRC
    cc -c -o "$TMP/sizes.o" "$TMP/sizes.c"

    OUT_SS=$($NM --size-sort "$TMP/sizes.o")
    expect_nomatch "--size-sort drops undefined" "$OUT_SS" 'ext_sz'
    expect_nomatch "--size-sort drops size-0 symbols" "$OUT_SS" 'zero_sym'
    expect_match "--size-sort prints size of small_var" "$OUT_SS" '^0+4 D small_var$'
    expect_match "--size-sort prints size of mid_arr" "$OUT_SS" '^0+50 D mid_arr$'
    expect_match "--size-sort prints size of big_arr" "$OUT_SS" '^0+12c D big_arr$'
    ORDER=$(printf '%s\n' "$OUT_SS" | awk '{print $3}' | grep -E '^(small_var|mid_arr|big_arr)$' | tr '\n' ' ')
    [ "$ORDER" = "small_var mid_arr big_arr " ] ||
        fail "--size-sort order is ascending (got: $ORDER)"
    pass "--size-sort order is ascending"

    OUT_SSS=$($NM --size-sort -S "$TMP/sizes.o")
    # Both the value and the size (4) are shown; the -S column order
    # itself is not what this checks.
    NF_SV=$(printf '%s\n' "$OUT_SSS" | awk '$NF == "small_var" { n = 0; for (i = 1; i < NF; i++) if ($i ~ /^0+4$/) n++; print NF ":" n }')
    [ "$NF_SV" = "4:1" ] ||
        fail "--size-sort -S prints value and size (got: $OUT_SSS)"
    pass "--size-sort -S prints value and size"
    expect_nomatch "--size-sort -S drops undefined" "$OUT_SSS" 'ext_sz'

    if nm --version 2>/dev/null | grep -q 'GNU nm'; then
        for flags in "--size-sort" "--size-sort -r" "--size-sort -g" "--size-sort -u"; do
            # shellcheck disable=SC2086
            GOT=$($NM $flags "$TMP/sizes.o")
            # shellcheck disable=SC2086
            WANT=$(nm $flags "$TMP/sizes.o")
            [ "$GOT" = "$WANT" ] || {
                printf 'FAIL: nm %s differs from GNU nm\n--- got:\n%s\n--- want:\n%s\n' \
                    "$flags" "$GOT" "$WANT" >&2
                exit 1
            }
            pass "nm $flags matches GNU nm"
        done
    fi

    if [ -x /bin/ls ]; then
        OUT_DYN=$($NM -D /bin/ls)
        expect_match "-D dynamic symbols produce output" "$OUT_DYN" '^[[:space:]]*[0-9a-fA-F]*[[:space:]]+[A-Za-z][[:space:]]+'
    fi

    pass "nm smoke tests passed"
}

main "$@"
