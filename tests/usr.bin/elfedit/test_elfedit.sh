#!/bin/sh
set -eu

TOP="${TOP:-$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)}"
TMP="${TMPDIR:-/tmp}/elfedit-tests.$$"

cleanup() {
    rm -rf "$TMP"
}
trap cleanup EXIT INT TERM
mkdir -p "$TMP"

build_tools() {
    make -C "$TOP/usr.lib/elfobj" NATIVE_BUILD=1 >/dev/null
    make -C "$TOP/usr.bin/elfedit" NATIVE_BUILD=1 >/dev/null
}

build_fixture_maker() {
    cat >"$TMP/mk_fixture.c" <<'EOF'
#include "elfobj.h"
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t code[] = {0xC3};
    elfobj_t *obj;
    elf_section_t *text;

    if (argc != 2) {
        return 1;
    }

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return 2;
    }
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) {
        elf_close(obj);
        return 3;
    }
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
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
    cc -Wall -Wextra -idirafter "$TOP/include" "$TMP/mk_fixture.c" \
        "$TOP/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_fixture"
}

assert_grep() {
    pattern="$1"
    path="$2"
    if ! grep -Eq "$pattern" "$path"; then
        echo "assertion failed: pattern '$pattern' not found in $path" >&2
        return 1
    fi
}

test_9a_headers() {
    "$TMP/mk_fixture" "$TMP/base.o"

    "$TOP/usr.bin/elfedit/elfedit" --output-type dyn -o "$TMP/type.o" "$TMP/base.o"
    readelf -h "$TMP/type.o" >"$TMP/type.hdr"
    assert_grep "Type:[[:space:]]+DYN" "$TMP/type.hdr"

    "$TOP/usr.bin/elfedit/elfedit" --output-machine x86_64 -o "$TMP/machine.o" "$TMP/base.o"
    readelf -h "$TMP/machine.o" >"$TMP/machine.hdr"
    assert_grep "Machine:[[:space:]]+Advanced Micro Devices X86-64" "$TMP/machine.hdr"

    "$TOP/usr.bin/elfedit/elfedit" --output-osabi linux -o "$TMP/osabi.o" "$TMP/base.o"
    readelf -h "$TMP/osabi.o" >"$TMP/osabi.hdr"
    assert_grep "OS/ABI:[[:space:]]+UNIX - GNU" "$TMP/osabi.hdr"

    "$TOP/usr.bin/elfedit/elfedit" --output-entry 0xdead -o "$TMP/entry.o" "$TMP/base.o"
    readelf -h "$TMP/entry.o" >"$TMP/entry.hdr"
    assert_grep "Entry point address:[[:space:]]+0xdead" "$TMP/entry.hdr"

    "$TOP/usr.bin/elfedit/elfedit" --output-flags 0x1234 -o "$TMP/flags.o" "$TMP/base.o"
    readelf -h "$TMP/flags.o" >"$TMP/flags.hdr"
    assert_grep "Flags:[[:space:]]+0x1234" "$TMP/flags.hdr"
}

build_tools
build_fixture_maker
test_9a_headers
echo "ok: elfedit 9a header tests"
