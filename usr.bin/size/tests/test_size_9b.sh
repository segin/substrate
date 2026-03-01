#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SIZE_BIN="$ROOT/usr.bin/size/size"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/size-9b.XXXXXX")

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

is_hex() {
    echo "$1" | grep -Eq '^[0-9a-f]+$'
}

is_octal() {
    echo "$1" | grep -Eq '^[0-7]+$'
}

build_tools() {
    make -C "$ROOT/usr.lib/elfobj" NATIVE_BUILD=1 >/dev/null
    make -C "$ROOT/usr.bin/size" NATIVE_BUILD=1 >/dev/null
}

build_fixture() {
    cat > "$TMP/f.c" <<'EOF'
int g = 7;
int h;
int f(void) { return g + h; }
EOF
    cc -c -o "$TMP/f.o" "$TMP/f.c"
}

test_berkeley_layout() {
    out=$("$SIZE_BIN" "$TMP/f.o")
    header=$(echo "$out" | sed -n '1p')
    row=$(echo "$out" | sed -n '2p')

    [ "$header" = "   text    data     bss     dec     hex filename" ] || \
        fail "9b berkeley header mismatch"
    echo "$row" | awk 'NF==6 {exit 0} {exit 1}' || fail "9b berkeley row columns"
    pass "9b berkeley layout"
}

test_sysv_layout() {
    out=$("$SIZE_BIN" -A "$TMP/f.o")
    line1=$(echo "$out" | sed -n '1p')
    line2=$(echo "$out" | sed -n '2p')

    [ "$line1" = "$TMP/f.o  :" ] || fail "9b sysv file header"
    [ "$line2" = "section              size             addr" ] || fail "9b sysv table header"
    echo "$out" | grep -Eq '^Total[[:space:]]+[0-9]+' || fail "9b sysv total row"
    pass "9b sysv layout"
}

test_hex_radix() {
    row=$("$SIZE_BIN" -x "$TMP/f.o" | sed -n '2p')
    set -- $row
    is_hex "$1" || fail "9b -x text not hex"
    is_hex "$2" || fail "9b -x data not hex"
    is_hex "$3" || fail "9b -x bss not hex"
    is_hex "$4" || fail "9b -x total not hex"
    is_hex "$5" || fail "9b -x final numeric not hex"
    pass "9b -x radix"
}

test_octal_radix() {
    row=$("$SIZE_BIN" -o "$TMP/f.o" | sed -n '2p')
    set -- $row
    is_octal "$1" || fail "9b -o text not octal"
    is_octal "$2" || fail "9b -o data not octal"
    is_octal "$3" || fail "9b -o bss not octal"
    is_octal "$4" || fail "9b -o total not octal"
    is_octal "$5" || fail "9b -o final numeric not octal"
    pass "9b -o radix"
}

main() {
    build_tools
    build_fixture
    test_berkeley_layout
    test_sysv_layout
    test_hex_radix
    test_octal_radix
}

main "$@"
