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

    if [ -x /bin/ls ]; then
        OUT_DYN=$($NM -D /bin/ls)
        expect_match "-D dynamic symbols produce output" "$OUT_DYN" '^[[:space:]]*[0-9a-fA-F]*[[:space:]]+[A-Za-z][[:space:]]+'
    fi

    pass "nm smoke tests passed"
}

main "$@"
