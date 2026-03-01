#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SIZE_BIN="$ROOT/usr.bin/size/size"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/size-9c.XXXXXX")

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

pass() {
    echo "ok: $*"
}

build_tools() {
    make -C "$ROOT/usr.lib/elfobj" NATIVE_BUILD=1 >/dev/null
    make -C "$ROOT/usr.bin/size" NATIVE_BUILD=1 >/dev/null
}

build_fixtures() {
    cat > "$TMP/a.c" <<'EOF'
int a(void) { return 1; }
EOF
    cat > "$TMP/b.c" <<'EOF'
int b = 2;
EOF
    cat > "$TMP/c.c" <<'EOF'
int c = 3;
EOF
    cc -c -o "$TMP/a.o" "$TMP/a.c"
    cc -c -o "$TMP/b.o" "$TMP/b.c"
    cc -c -o "$TMP/c.o" "$TMP/c.c"
    ar rc "$TMP/libabc.a" "$TMP/a.o" "$TMP/b.o" "$TMP/c.o"
}

test_two_files_total_row() {
    out=$("$SIZE_BIN" "$TMP/a.o" "$TMP/b.o")
    lines=$(echo "$out" | wc -l | tr -d ' ')
    [ "$lines" = "4" ] || fail "9c two files expected 4 lines, got $lines"
    echo "$out" | sed -n '4p' | grep -Eq '[[:space:]]total$' || fail "9c missing total row"
    pass "9c two file total row"
}

test_archive_member_rows() {
    out=$("$SIZE_BIN" "$TMP/libabc.a")
    rows=$(echo "$out" | tail -n +2 | grep -c 'libabc\.a(' | tr -d ' ')
    [ "$rows" = "3" ] || fail "9c archive expected 3 member rows, got $rows"
    echo "$out" | grep -F "$TMP/libabc.a(a.o)" >/dev/null 2>&1 || fail "9c missing a.o row"
    echo "$out" | grep -F "$TMP/libabc.a(b.o)" >/dev/null 2>&1 || fail "9c missing b.o row"
    echo "$out" | grep -F "$TMP/libabc.a(c.o)" >/dev/null 2>&1 || fail "9c missing c.o row"
    pass "9c archive member names"
}

main() {
    build_tools
    build_fixtures
    test_two_files_total_row
    test_archive_member_rows
}

main "$@"
