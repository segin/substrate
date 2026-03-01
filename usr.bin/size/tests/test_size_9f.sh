#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SIZE_BIN="$ROOT/usr.bin/size/size"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/size-9f.XXXXXX")

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

build_object() {
    cat > "$TMP/foo.c" <<'EOF'
int foo_data = 7;
int foo_bss;
int foo(int x) { return x + foo_data + foo_bss; }
EOF
    cc -c -o "$TMP/foo.o" "$TMP/foo.c"
}

triplet() {
    "$1" "$2" | sed -n '2p' | awk '{print $1" "$2" "$3}'
}

text_value() {
    "$1" "$2" | sed -n '2p' | awk '{print $1}'
}

text_section_size() {
    hex=$(objdump -h "$1" | awk '$2==".text" {print $3; exit}')
    [ -n "$hex" ] || fail "9f missing .text section in fixture"
    printf "%d\n" "0x$hex"
}

test_compiled_code_bound() {
    text_reported=$(text_value "$SIZE_BIN" "$TMP/foo.o")
    text_section=$(text_section_size "$TMP/foo.o")
    [ "$text_reported" -ge "$text_section" ] || \
        fail "9f text lower than .text section ($text_reported < $text_section)"
    pass "9f text >= compiled .text size"
}

test_host_parity() {
    ours=$(triplet "$SIZE_BIN" "$TMP/foo.o")
    host=$(triplet size "$TMP/foo.o")
    [ "$ours" = "$host" ] || fail "9f host parity mismatch (ours '$ours', host '$host')"
    pass "9f host size parity"
}

main() {
    build_tools
    build_object
    test_compiled_code_bound
    test_host_parity
}

main "$@"
