#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SIZE_BIN="$ROOT/usr.bin/size/size"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/size-9d.XXXXXX")

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

build_large_bss_fixture() {
    cat > "$TMP/mk_large_bss.c" <<'EOF'
#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv) {
    static const uint8_t dummy = 0;
    const uint64_t big_bss = (1ULL << 32) + 0x1234ULL;
    elfobj_t *obj;
    elf_section_t *bss;

    if (argc != 2) {
        return 1;
    }
    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return 2;
    }
    bss = elf_add_section(obj, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
    if (bss == NULL) {
        elf_close(obj);
        return 3;
    }
    if (elf_section_set_data(bss, &dummy, (size_t)big_bss) != ELF_OK) {
        elf_close(obj);
        return 4;
    }
    if (elf_write_file(obj, argv[1]) != ELF_OK) {
        elf_close(obj);
        return 5;
    }
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$ROOT/include" "$TMP/mk_large_bss.c" \
        "$ROOT/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_large_bss"
    "$TMP/mk_large_bss" "$TMP/large_bss.o"
}

test_no_wrap() {
    expected=$(( (1<<32) + 0x1234 ))
    line=$("$SIZE_BIN" "$TMP/large_bss.o" | sed -n '2p')
    bss=$(echo "$line" | awk '{print $3}')
    dec=$(echo "$line" | awk '{print $4}')

    [ "$bss" = "$expected" ] || fail "9d bss wrapped (got $bss, want $expected)"
    [ "$dec" = "$expected" ] || fail "9d total wrapped (got $dec, want $expected)"
    pass "9d large bss no wrap"
}

main() {
    build_tools
    build_large_bss_fixture
    test_no_wrap
}

main "$@"
