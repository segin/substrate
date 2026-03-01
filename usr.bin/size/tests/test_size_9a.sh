#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SIZE_BIN="$ROOT/usr.bin/size/size"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/size-9a.XXXXXX")

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

expect_eq() {
    label=$1
    got=$2
    want=$3
    [ "$got" = "$want" ] || fail "$label (got '$got', want '$want')"
    pass "$label"
}

parse_col() {
    line=$1
    col=$2
    echo "$line" | awk -v n="$col" '{print $n}'
}

build_tools() {
    make -C "$ROOT/usr.lib/elfobj" NATIVE_BUILD=1 >/dev/null
    make -C "$ROOT/usr.bin/size" NATIVE_BUILD=1 >/dev/null
}

test_text_data_bss_rodata() {
    cat > "$TMP/mk_classify.c" <<'EOF'
#include <elfobj.h>
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t text[] = {0xC3};
    static const uint8_t ro[] = {1, 2, 3, 4};
    static const uint8_t data[] = {5, 6, 7, 8};
    elfobj_t *obj;
    elf_section_t *sec;

    if (argc != 2) {
        return 1;
    }
    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) return 2;

    sec = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (sec == NULL || elf_section_set_data(sec, text, sizeof(text)) != ELF_OK) return 3;
    sec = elf_add_section(obj, ".rodata", SHT_PROGBITS, SHF_ALLOC);
    if (sec == NULL || elf_section_set_data(sec, ro, sizeof(ro)) != ELF_OK) return 4;
    sec = elf_add_section(obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    if (sec == NULL || elf_section_set_data(sec, data, sizeof(data)) != ELF_OK) return 5;
    sec = elf_add_section(obj, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
    if (sec == NULL || elf_section_set_data(sec, data, sizeof(data)) != ELF_OK) return 6;

    if (elf_write_file(obj, argv[1]) != ELF_OK) return 7;
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$ROOT/include" "$TMP/mk_classify.c" \
        "$ROOT/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_classify"
    "$TMP/mk_classify" "$TMP/classify.o"
    line=$("$SIZE_BIN" "$TMP/classify.o" | sed -n '2p')
    text=$(parse_col "$line" 1)
    data=$(parse_col "$line" 2)
    bss=$(parse_col "$line" 3)
    expect_eq "9a text bucket" "$text" "5"
    expect_eq "9a data bucket" "$data" "4"
    expect_eq "9a bss bucket" "$bss" "4"
}

test_tls_classification() {
    cat > "$TMP/mk_tls.c" <<'EOF'
#include <elfobj.h>
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t text[] = {0xC3};
    static const uint8_t tdata[] = {9, 8, 7, 6};
    uint8_t tbss[8];
    elfobj_t *obj;
    elf_section_t *sec;

    if (argc != 2) {
        return 1;
    }
    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) return 2;

    sec = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (sec == NULL || elf_section_set_data(sec, text, sizeof(text)) != ELF_OK) return 3;
    sec = elf_add_section(obj, ".tdata", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
    if (sec == NULL || elf_section_set_data(sec, tdata, sizeof(tdata)) != ELF_OK) return 4;
    sec = elf_add_section(obj, ".tbss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
    if (sec == NULL || elf_section_set_data(sec, tbss, sizeof(tbss)) != ELF_OK) return 5;

    if (elf_write_file(obj, argv[1]) != ELF_OK) return 6;
    elf_close(obj);
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$ROOT/include" "$TMP/mk_tls.c" \
        "$ROOT/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_tls"
    "$TMP/mk_tls" "$TMP/tls.o"
    line=$("$SIZE_BIN" "$TMP/tls.o" | sed -n '2p')
    data=$(parse_col "$line" 2)
    bss=$(parse_col "$line" 3)
    expect_eq "9a tls data" "$data" "4"
    expect_eq "9a tls bss" "$bss" "8"
}

test_no_alloc_sections() {
    cat > "$TMP/mk_noalloc.c" <<'EOF'
#include <elfobj.h>
#include <stdint.h>

int main(int argc, char **argv) {
    static const uint8_t blob[] = {'n', 'o', 'a', '\0'};
    elfobj_t *obj;
    elf_section_t *sec;

    if (argc != 2) {
        return 1;
    }
    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return 2;
    }
    sec = elf_add_section(obj, ".comment", SHT_PROGBITS, 0);
    if (sec == NULL) {
        elf_close(obj);
        return 3;
    }
    if (elf_section_set_data(sec, blob, sizeof(blob)) != ELF_OK) {
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
    cc -Wall -Wextra -idirafter "$ROOT/include" "$TMP/mk_noalloc.c" \
        "$ROOT/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_noalloc"
    "$TMP/mk_noalloc" "$TMP/noalloc.o"
    line=$("$SIZE_BIN" "$TMP/noalloc.o" | sed -n '2p')
    text=$(parse_col "$line" 1)
    data=$(parse_col "$line" 2)
    bss=$(parse_col "$line" 3)
    expect_eq "9a noalloc text" "$text" "0"
    expect_eq "9a noalloc data" "$data" "0"
    expect_eq "9a noalloc bss" "$bss" "0"
}

main() {
    build_tools
    test_text_data_bss_rodata
    test_tls_classification
    test_no_alloc_sections
}

main "$@"
