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
    static const uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
    static const uint8_t comment[] = {'o', 'k', '\0'};
    elfobj_t *obj;
    elf_section_t *text;
    elf_section_t *datasec;
    elf_section_t *commentsec;

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
    datasec = elf_add_section(obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    if (datasec == NULL) {
        elf_close(obj);
        return 5;
    }
    if (elf_section_set_data(datasec, data, sizeof(data)) != ELF_OK) {
        elf_close(obj);
        return 6;
    }
    commentsec = elf_add_section(obj, ".comment", SHT_PROGBITS, 0);
    if (commentsec == NULL) {
        elf_close(obj);
        return 7;
    }
    if (elf_section_set_data(commentsec, comment, sizeof(comment)) != ELF_OK) {
        elf_close(obj);
        return 8;
    }
    if (elf_write_file(obj, argv[1]) != ELF_OK) {
        elf_close(obj);
        return 9;
    }
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$TOP/include" "$TMP/mk_fixture.c" \
        "$TOP/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_fixture"
}

build_segment_fixture_maker() {
    cat >"$TMP/mk_segment_fixture.c" <<'EOF'
#include "elfobj.h"
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t code[] = {0xC3};
    elfobj_t *obj;
    elf_section_t *text;
    elf_segment_t *seg;

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
    seg = elf_add_load_segment(obj, 0x5, 0x1000);
    if (seg == NULL) {
        elf_close(obj);
        return 5;
    }
    if (elf_segment_add_section(seg, text) != ELF_OK) {
        elf_close(obj);
        return 6;
    }
    if (elf_write_file(obj, argv[1]) != ELF_OK) {
        elf_close(obj);
        return 7;
    }
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$TOP/include" "$TMP/mk_segment_fixture.c" \
        "$TOP/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_segment_fixture"
}

build_invalid_fixture_maker() {
    cat >"$TMP/mk_invalid_fixture.c" <<'EOF'
#include "elfobj.h"
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t code[] = {0xC3};
    elfobj_t *obj;
    elf_section_t *text;
    elf_segment_t *seg;

    if (argc != 2) {
        return 1;
    }

    obj = elf_create(ET_DYN, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
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
    seg = elf_add_load_segment(obj, 0x5, 0x1000);
    if (seg == NULL) {
        elf_close(obj);
        return 5;
    }
    if (elf_segment_add_section(seg, text) != ELF_OK) {
        elf_close(obj);
        return 6;
    }
    if (elf_write_file(obj, argv[1]) != ELF_OK) {
        elf_close(obj);
        return 7;
    }
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$TOP/include" "$TMP/mk_invalid_fixture.c" \
        "$TOP/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_invalid_fixture"
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

test_9b_sections() {
    "$TMP/mk_fixture" "$TMP/sections_base.o"

    "$TOP/usr.bin/elfedit/elfedit" --set-section-flags .data=alloc,write,execinstr \
        -o "$TMP/sections_flags.o" "$TMP/sections_base.o"
    readelf -S "$TMP/sections_flags.o" >"$TMP/sections_flags.txt"
    assert_grep "\\.data[[:space:]]+PROGBITS.*WAX" "$TMP/sections_flags.txt"

    "$TOP/usr.bin/elfedit/elfedit" --set-section-type .comment=note \
        -o "$TMP/sections_type.o" "$TMP/sections_base.o"
    readelf -S "$TMP/sections_type.o" >"$TMP/sections_type.txt"
    assert_grep "\\.comment[[:space:]]+NOTE" "$TMP/sections_type.txt"

    "$TOP/usr.bin/elfedit/elfedit" --set-section-align .text=16 \
        -o "$TMP/sections_align.o" "$TMP/sections_base.o"
    readelf -S "$TMP/sections_align.o" >"$TMP/sections_align.txt"
    assert_grep "\\.text[[:space:]]+PROGBITS.*[[:space:]]16$" "$TMP/sections_align.txt"

    "$TOP/usr.bin/elfedit/elfedit" --rename-section .text=.code \
        -o "$TMP/sections_rename.o" "$TMP/sections_base.o"
    readelf -S "$TMP/sections_rename.o" >"$TMP/sections_rename.txt"
    assert_grep "\\.code[[:space:]]+PROGBITS" "$TMP/sections_rename.txt"
}

test_9c_program_headers() {
    "$TMP/mk_segment_fixture" "$TMP/segments_base.elf"

    "$TOP/usr.bin/elfedit/elfedit" --set-segment-flags 0=rwx \
        -o "$TMP/segments_flags.elf" "$TMP/segments_base.elf"
    readelf -l "$TMP/segments_flags.elf" >"$TMP/segments_flags.txt"
    assert_grep "LOAD.*RWE" "$TMP/segments_flags.txt"

    "$TOP/usr.bin/elfedit/elfedit" --set-segment-align 0=0x2000 \
        -o "$TMP/segments_align.elf" "$TMP/segments_base.elf"
    readelf -l "$TMP/segments_align.elf" >"$TMP/segments_align.txt"
    assert_grep "0x2000" "$TMP/segments_align.txt"
}

test_9d_validation() {
    before_sum=""
    after_sum=""

    "$TMP/mk_segment_fixture" "$TMP/valid_base.elf"

    "$TOP/usr.bin/elfedit/elfedit" --set-segment-align 0=0x2000 \
        -o "$TMP/valid_edit.elf" "$TMP/valid_base.elf"
    readelf -l "$TMP/valid_edit.elf" >"$TMP/valid_check.txt"
    assert_grep "0x2000" "$TMP/valid_check.txt"

    "$TMP/mk_invalid_fixture" "$TMP/invalid_base.elf"
    before_sum="$(cksum "$TMP/invalid_base.elf" | awk '{print $1":"$2}')"
    if "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x222 \
        "$TMP/invalid_base.elf" >"$TMP/illegal_fail.out" 2>"$TMP/illegal_fail.err"; then
        echo "expected illegal edit to fail without --force" >&2
        return 1
    fi
    after_sum="$(cksum "$TMP/invalid_base.elf" | awk '{print $1":"$2}')"
    if [ "$before_sum" != "$after_sum" ]; then
        echo "illegal edit modified original file" >&2
        return 1
    fi

    "$TOP/usr.bin/elfedit/elfedit" --force --output-entry 0x222 \
        -o "$TMP/illegal_force.elf" "$TMP/invalid_base.elf" >"$TMP/illegal_force.out" \
        2>"$TMP/illegal_force.err"
    test -f "$TMP/illegal_force.elf"
    assert_grep "WARNING: writing structurally invalid ELF" "$TMP/illegal_force.err"
}

build_tools
build_fixture_maker
build_segment_fixture_maker
build_invalid_fixture_maker
test_9a_headers
test_9b_sections
test_9c_program_headers
test_9d_validation
echo "ok: elfedit 9a/9d tests"
