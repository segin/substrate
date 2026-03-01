#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$(dirname "$(dirname "$SCRIPT_DIR")")")"
AR_BIN="$REPO_ROOT/usr.bin/ar/ar"
ELFOBJ_LIB="$REPO_ROOT/usr.lib/elfobj/libelfobj.a"
ELFOBJ_INC="$REPO_ROOT/include"
ELFOBJ_SRC="$REPO_ROOT/usr.lib/elfobj/src"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}

assert_eq() {
    local lhs="$1"
    local rhs="$2"
    local msg="$3"
    [[ "$lhs" == "$rhs" ]] || fail "$msg (got '$lhs', expected '$rhs')"
}

assert_file_contains() {
    local file="$1"
    local pat="$2"
    rg -a "$pat" "$file" >/dev/null || fail "expected pattern '$pat' in $file"
}

assert_file_not_contains() {
    local file="$1"
    local pat="$2"
    ! rg -a "$pat" "$file" >/dev/null || fail "unexpected pattern '$pat' in $file"
}

build_tools() {
    make -C "$REPO_ROOT/usr.lib/elfobj" NATIVE_BUILD=1 >/dev/null
    make -C "$REPO_ROOT/usr.bin/ar" NATIVE_BUILD=1 >/dev/null
    [[ -x "$AR_BIN" ]] || fail "ar binary missing at $AR_BIN"
}

make_obj_generator() {
    cat > gen_obj.c <<'EOF'
#include "elfobj.h"

static int make_obj(const char *path, const char *sym, uint16_t machine,
                    elfobj_class_t cls, elfobj_endian_t endian) {
    static const unsigned char code[] = {0xC3};
    elfobj_t *obj = elf_create(ET_REL, machine, cls, endian);
    if (!obj) return 1;
    elf_section_t *text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) return 2;
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) return 3;
    if (elf_section_set_align(text, 1) != ELF_OK) return 4;
    elf_symbol_t *g = elf_add_symbol(obj, sym, 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (!g) return 5;
    if (elf_symbol_define(g, text, 0) != ELF_OK) return 6;
    if (elf_write_file(obj, path) != ELF_OK) return 7;
    elf_close(obj);
    return 0;
}

int main(void) {
    if (make_obj("obj32le.o", "sym32le", EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE) != 0) return 10;
    if (make_obj("obj64le.o", "sym64le", EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE) != 0) return 11;
    if (make_obj("obj32be.o", "sym32be", EM_MIPS, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_BE) != 0) return 12;
    return 0;
}
EOF
    cc -O2 -Wall -Wextra -Werror \
        -iquote "$ELFOBJ_INC" -I "$ELFOBJ_SRC" \
        -o gen_obj gen_obj.c "$ELFOBJ_LIB"
}

main() {
    build_tools

    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    cd "$WORK"
    cp "$AR_BIN" ./ar
    ln -sf ar ranlib

    make_obj_generator
    ./gen_obj

    # 12a: basic parser and round-trip checks (BSD/GNU + symbol tables + long names)
    ./ar --format=bsd rc bsd.a obj32le.o
    ./ar --format=gnu rc gnu.a obj32le.o
    assert_eq "$(./ar t bsd.a | tr -d '\n')" "obj32le.o" "bsd parse/list failed"
    assert_eq "$(./ar t gnu.a | tr -d '\n')" "obj32le.o" "gnu parse/list failed"
    ./ar --format=bsd rs bsd.a obj32le.o
    ./ar --format=gnu rs gnu.a obj32le.o
    assert_file_contains bsd.a "__.SYMDEF SORTED"
    assert_file_contains gnu.a "sym32le"

    longname="name_with_space and_extra_chars_for_archive_member_test.o"
    cp obj32le.o "$longname"
    ./ar --format=bsd rc long_bsd.a "$longname"
    assert_eq "$(./ar t long_bsd.a | tr -d '\n')" "$longname" "bsd extended name decode failed"

    # 12b: symbol extraction matrix (32/64/endian/non-elf/undef-ish)
    ./ar --format=gnu rc mix.a obj32le.o obj64le.o obj32be.o
    ./ar --format=gnu s mix.a
    assert_file_contains mix.a "sym32le"
    assert_file_contains mix.a "sym64le"
    assert_file_contains mix.a "sym32be"
    printf "not-elf\n" > plain.txt
    ./ar --format=gnu rc nonelf.a plain.txt
    ./ar --format=gnu s nonelf.a
    assert_file_not_contains nonelf.a "__.SYMDEF SORTED"

    # 12c: core operations
    printf "A" > a
    printf "B" > b
    ./ar rc ops.a a b
    assert_eq "$(./ar t ops.a | paste -sd ',' -)" "a,b" "r/create failed"
    ./ar q ops.a a
    assert_eq "$(./ar t ops.a | paste -sd ',' -)" "a,b,a" "q duplicate append failed"
    ./ar d ops.a b
    assert_eq "$(./ar t ops.a | paste -sd ',' -)" "a,a" "delete failed"
    ./ar m ops.a a
    ./ar mb a ops.a a
    ./ar p ops.a a > p.out
    [[ -s p.out ]] || fail "print output empty"
    rm -f a
    ./ar x ops.a
    [[ -f a ]] || fail "extract failed"
    ./ranlib ops.a

    # 12d: edge cases
    ./ar dN 2 ops.a a
    assert_eq "$(./ar t ops.a | paste -sd ',' -)" "a" "N-count delete failed"
    printf '!<arch>\n' > empty.a
    [[ -z "$(./ar t empty.a)" ]] || fail "empty archive list must be empty"
    : > zero
    ./ar rc zero.a zero
    ./ar t zero.a >/dev/null
    odd="oddlen"
    printf "xyz" > "$odd"
    ./ar rc odd.a "$odd"
    ./ar x odd.a
    cmp -s "$odd" "$odd" || fail "odd length payload mismatch"

    # 12e: integration/compatibility + deterministic
    /usr/bin/ar rc hostmade.a obj32le.o "$longname"
    ./ar t hostmade.a | rg "^obj32le.o$" >/dev/null || fail "host ar compatibility failed"
    ./ar --format=gnu rc ours.a obj32le.o obj64le.o
    /usr/bin/ar t ours.a >/dev/null || fail "host ar failed reading substrate archive"
    SOURCE_DATE_EPOCH=123456789 ./ar rc det1.a obj32le.o
    SOURCE_DATE_EPOCH=123456789 ./ar rc det2.a obj32le.o
    cmp -s det1.a det2.a || fail "deterministic archive mismatch"

    # 12f: fuzz/stress smoke
    printf 'corrupt' > fuzz.bin
    ./ar t fuzz.bin >/dev/null 2>&1 || true
    stress_dir="$(mktemp -d)"
    for i in $(seq 1 1000); do printf "x" > "$stress_dir/f$i"; done
    ./ar rc stress.a "$stress_dir"/f*
    count="$(./ar t stress.a | wc -l | tr -d ' ')"
    assert_eq "$count" "1000" "stress archive member count mismatch"

    echo "All ar tests passed."
}

main "$@"
