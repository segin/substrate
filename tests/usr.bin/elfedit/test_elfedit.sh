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

build_core_fixture_maker() {
    cat >"$TMP/mk_core_fixture.c" <<'EOF'
#include "elfobj.h"

int main(int argc, char **argv) {
    elfobj_t *obj;

    if (argc != 2) {
        return 1;
    }
    obj = elf_create(ET_CORE, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return 2;
    }
    if (elf_write_file(obj, argv[1]) != ELF_OK) {
        elf_close(obj);
        return 3;
    }
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$TOP/include" "$TMP/mk_core_fixture.c" \
        "$TOP/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_core_fixture"
}

build_fixture64_maker() {
    cat >"$TMP/mk_fixture64.c" <<'EOF'
#include "elfobj.h"
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t code[] = {0xC3};
    elfobj_t *obj;
    elf_section_t *text;

    if (argc != 2) {
        return 1;
    }
    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
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
    cc -Wall -Wextra -idirafter "$TOP/include" "$TMP/mk_fixture64.c" \
        "$TOP/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_fixture64"
}

build_nosections_fixture_maker() {
    cat >"$TMP/mk_nosections_fixture.c" <<'EOF'
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void wr16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
}
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
}

int main(int argc, char **argv) {
    uint8_t img[84];
    FILE *fp;

    if (argc != 2) {
        return 1;
    }
    memset(img, 0, sizeof(img));
    img[0] = 0x7f;
    img[1] = 'E';
    img[2] = 'L';
    img[3] = 'F';
    img[4] = 1;
    img[5] = 1;
    img[6] = 1;
    wr16(img + 16, 1);
    wr16(img + 18, 3);
    wr32(img + 20, 1);
    wr32(img + 24, 0);
    wr32(img + 28, 52);
    wr32(img + 32, 0);
    wr32(img + 36, 0);
    wr16(img + 40, 52);
    wr16(img + 42, 32);
    wr16(img + 44, 1);
    wr16(img + 46, 40);
    wr16(img + 48, 0);
    wr16(img + 50, 0);

    wr32(img + 52, 1);
    wr32(img + 56, 0);
    wr32(img + 60, 0);
    wr32(img + 64, 0);
    wr32(img + 68, 0);
    wr32(img + 72, 0);
    wr32(img + 76, 5);
    wr32(img + 80, 0x1000);

    fp = fopen(argv[1], "wb");
    if (fp == NULL) {
        return 2;
    }
    if (fwrite(img, 1, sizeof(img), fp) != sizeof(img)) {
        fclose(fp);
        return 3;
    }
    fclose(fp);
    return 0;
}
EOF
    cc -Wall -Wextra "$TMP/mk_nosections_fixture.c" -o "$TMP/mk_nosections_fixture"
}

assert_grep() {
    pattern="$1"
    path="$2"
    if ! grep -Eq "$pattern" "$path"; then
        echo "assertion failed: pattern '$pattern' not found in $path" >&2
        return 1
    fi
}

header_fingerprint() {
    in="$1"
    out="$2"
    readelf -h "$in" | awk '
        /Type:/ ||
        /Machine:/ ||
        /OS\/ABI:/ ||
        /ABI Version:/ ||
        /Entry point address:/ ||
        /Flags:/ { print }' >"$out"
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

test_9e_dry_run() {
    before_sum=""
    after_sum=""

    "$TMP/mk_segment_fixture" "$TMP/dry_base.elf"
    before_sum="$(cksum "$TMP/dry_base.elf" | awk '{print $1":"$2}')"
    "$TOP/usr.bin/elfedit/elfedit" --dry-run --output-entry 0x333 \
        "$TMP/dry_base.elf" >"$TMP/dry_ok.out" 2>"$TMP/dry_ok.err"
    after_sum="$(cksum "$TMP/dry_base.elf" | awk '{print $1":"$2}')"
    if [ "$before_sum" != "$after_sum" ]; then
        echo "dry-run unexpectedly modified file" >&2
        return 1
    fi
    assert_grep "dry-run: validation passed" "$TMP/dry_ok.out"

    "$TMP/mk_invalid_fixture" "$TMP/dry_invalid.elf"
    if "$TOP/usr.bin/elfedit/elfedit" --dry-run --output-entry 0x444 \
        "$TMP/dry_invalid.elf" >"$TMP/dry_bad.out" 2>"$TMP/dry_bad.err"; then
        echo "dry-run expected validation failure" >&2
        return 1
    fi
    assert_grep "dry-run: validation failed" "$TMP/dry_bad.out"
}

test_9f_safety() {
    mode_before=""
    mode_after=""
    sum_before=""
    sum_after=""

    "$TMP/mk_segment_fixture" "$TMP/safe_perm.elf"
    chmod 640 "$TMP/safe_perm.elf"
    mode_before="$(stat -c %a "$TMP/safe_perm.elf")"
    "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x555 "$TMP/safe_perm.elf"
    mode_after="$(stat -c %a "$TMP/safe_perm.elf")"
    if [ "$mode_before" != "$mode_after" ]; then
        echo "in-place edit changed file permissions" >&2
        return 1
    fi

    mkdir "$TMP/ro"
    cp "$TMP/safe_perm.elf" "$TMP/ro/fail.elf"
    sum_before="$(cksum "$TMP/ro/fail.elf" | awk '{print $1":"$2}')"
    chmod 555 "$TMP/ro"
    if "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x666 \
        "$TMP/ro/fail.elf" >"$TMP/write_fail.out" 2>"$TMP/write_fail.err"; then
        echo "expected write failure in read-only directory" >&2
        return 1
    fi
    chmod 755 "$TMP/ro"
    sum_after="$(cksum "$TMP/ro/fail.elf" | awk '{print $1":"$2}')"
    if [ "$sum_before" != "$sum_after" ]; then
        echo "write failure modified original file" >&2
        return 1
    fi
    if ls "$TMP/ro/fail.elf.elfedit.tmp."* >/dev/null 2>&1; then
        echo "temporary file was not cleaned up after write failure" >&2
        return 1
    fi

    "$TMP/mk_core_fixture" "$TMP/core.elf"
    if "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x777 \
        "$TMP/core.elf" >"$TMP/core_fail.out" 2>"$TMP/core_fail.err"; then
        echo "core file edit should fail without --force" >&2
        return 1
    fi
    assert_grep "refusing to edit core files without --force" "$TMP/core_fail.err"
}

test_9g_edge_cases() {
    sum_before=""
    sum_after=""

    "$TMP/mk_fixture" "$TMP/edge32.o"
    "$TMP/mk_fixture64" "$TMP/edge64.o"
    "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x888 -o "$TMP/edge32_out.o" "$TMP/edge32.o"
    "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x999 -o "$TMP/edge64_out.o" "$TMP/edge64.o"
    readelf -h "$TMP/edge32_out.o" >"$TMP/edge32.hdr"
    readelf -h "$TMP/edge64_out.o" >"$TMP/edge64.hdr"
    assert_grep "Class:[[:space:]]+ELF32" "$TMP/edge32.hdr"
    assert_grep "Class:[[:space:]]+ELF64" "$TMP/edge64.hdr"

    "$TMP/mk_nosections_fixture" "$TMP/nosections.elf"
    "$TOP/usr.bin/elfedit/elfedit" --output-entry 0x1234 -o "$TMP/nosections_out.elf" \
        "$TMP/nosections.elf"
    readelf -h "$TMP/nosections_out.elf" >"$TMP/nosections.hdr"
    assert_grep "Entry point address:[[:space:]]+0x1234" "$TMP/nosections.hdr"

    "$TMP/mk_segment_fixture" "$TMP/multi.elf"
    "$TOP/usr.bin/elfedit/elfedit" --output-type dyn --output-machine x86_64 \
        --output-osabi linux --output-entry 0xabc -o "$TMP/multi_out.elf" "$TMP/multi.elf"
    readelf -h "$TMP/multi_out.elf" >"$TMP/multi.hdr"
    assert_grep "Type:[[:space:]]+DYN" "$TMP/multi.hdr"
    assert_grep "Machine:[[:space:]]+Advanced Micro Devices X86-64" "$TMP/multi.hdr"
    assert_grep "OS/ABI:[[:space:]]+UNIX - GNU" "$TMP/multi.hdr"
    assert_grep "Entry point address:[[:space:]]+0xabc" "$TMP/multi.hdr"

    cp "$TMP/multi.elf" "$TMP/noop.elf"
    sum_before="$(cksum "$TMP/noop.elf" | awk '{print $1":"$2}')"
    "$TOP/usr.bin/elfedit/elfedit" "$TMP/noop.elf" >"$TMP/noop.out" 2>"$TMP/noop.err"
    sum_after="$(cksum "$TMP/noop.elf" | awk '{print $1":"$2}')"
    if [ "$sum_before" != "$sum_after" ]; then
        echo "no-op invocation modified file" >&2
        return 1
    fi
}

test_9h_roundtrip() {
    "$TMP/mk_fixture" "$TMP/round_base.o"
    "$TOP/usr.bin/elfedit/elfedit" --output-type dyn -o "$TMP/round_dyn.o" "$TMP/round_base.o"
    "$TOP/usr.bin/elfedit/elfedit" --output-type rel -o "$TMP/round_back.o" "$TMP/round_dyn.o"
    header_fingerprint "$TMP/round_base.o" "$TMP/round_base.fp"
    header_fingerprint "$TMP/round_back.o" "$TMP/round_back.fp"
    cmp "$TMP/round_base.fp" "$TMP/round_back.fp"

    if command -v elfedit >/dev/null 2>&1; then
        host_elfedit="$(command -v elfedit)"
        host_ok=0
        if "$host_elfedit" --output-type dyn --output "$TMP/host_dyn.o" "$TMP/round_base.o" \
            >/dev/null 2>&1; then
            host_ok=1
        elif "$host_elfedit" --output-type dyn -o "$TMP/host_dyn.o" "$TMP/round_base.o" \
            >/dev/null 2>&1; then
            host_ok=1
        fi
        if [ "$host_ok" -eq 1 ]; then
            header_fingerprint "$TMP/round_dyn.o" "$TMP/ours_dyn.fp"
            header_fingerprint "$TMP/host_dyn.o" "$TMP/host_dyn.fp"
            cmp "$TMP/ours_dyn.fp" "$TMP/host_dyn.fp"
        fi
    fi
}

build_tools
build_fixture_maker
build_segment_fixture_maker
build_invalid_fixture_maker
build_core_fixture_maker
build_fixture64_maker
build_nosections_fixture_maker
test_9a_headers
test_9b_sections
test_9c_program_headers
test_9d_validation
test_9e_dry_run
test_9f_safety
test_9g_edge_cases
test_9h_roundtrip
echo "ok: elfedit 9a/9h tests"
