#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
SIZE_BIN="$ROOT/usr.bin/size/size"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/size-9e.XXXXXX")

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

build_rel_fixtures() {
    cat > "$TMP/mk_rel.c" <<'EOF'
#include <elfobj.h>
#include <stdint.h>

static int make_one(const char *path, elfobj_class_t cls, uint16_t machine) {
    static const uint8_t text[] = {0xC3};
    static const uint8_t data[] = {1, 2, 3, 4};
    uint8_t bss_dummy[8];
    elfobj_t *obj;
    elf_section_t *sec;

    obj = elf_create(ET_REL, machine, cls, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) return -1;
    sec = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (sec == NULL || elf_section_set_data(sec, text, sizeof(text)) != ELF_OK) return -1;
    sec = elf_add_section(obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    if (sec == NULL || elf_section_set_data(sec, data, sizeof(data)) != ELF_OK) return -1;
    sec = elf_add_section(obj, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
    if (sec == NULL || elf_section_set_data(sec, bss_dummy, sizeof(bss_dummy)) != ELF_OK) return -1;
    if (elf_write_file(obj, path) != ELF_OK) return -1;
    elf_close(obj);
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) return 1;
    if (make_one(argv[1], ELFOBJ_CLASS_32, EM_386) != 0) return 2;
    if (make_one(argv[2], ELFOBJ_CLASS_64, EM_X86_64) != 0) return 3;
    return 0;
}
EOF
    cc -Wall -Wextra -idirafter "$ROOT/include" "$TMP/mk_rel.c" \
        "$ROOT/usr.lib/elfobj/libelfobj.a" -o "$TMP/mk_rel"
    "$TMP/mk_rel" "$TMP/rel32.o" "$TMP/rel64.o"
}

triplet_from_size() {
    "$1" "$2" | sed -n '2p' | awk '{print $1" "$2" "$3}'
}

test_rel32_rel64() {
    t32=$(triplet_from_size "$SIZE_BIN" "$TMP/rel32.o")
    t64=$(triplet_from_size "$SIZE_BIN" "$TMP/rel64.o")
    [ "$t32" = "1 4 8" ] || fail "9e ELF32 ET_REL mismatch ($t32)"
    [ "$t64" = "1 4 8" ] || fail "9e ELF64 ET_REL mismatch ($t64)"
    pass "9e ELF32/ELF64 ET_REL"
}

build_exec_dyn() {
    cat > "$TMP/main.c" <<'EOF'
int x = 7;
int y;
int f(void) { return x + y; }
int main(void) { return f(); }
EOF
    cc -g -O0 -no-pie -o "$TMP/exec.bin" "$TMP/main.c"
    cc -g -O0 -fPIC -shared -o "$TMP/libdyn.so" "$TMP/main.c"
}

test_exec_dyn_match_host() {
    host_exec=$(triplet_from_size size "$TMP/exec.bin")
    ours_exec=$(triplet_from_size "$SIZE_BIN" "$TMP/exec.bin")
    host_dyn=$(triplet_from_size size "$TMP/libdyn.so")
    ours_dyn=$(triplet_from_size "$SIZE_BIN" "$TMP/libdyn.so")
    [ "$ours_exec" = "$host_exec" ] || fail "9e ET_EXEC mismatch (ours '$ours_exec' host '$host_exec')"
    [ "$ours_dyn" = "$host_dyn" ] || fail "9e ET_DYN mismatch (ours '$ours_dyn' host '$host_dyn')"
    pass "9e ET_EXEC/ET_DYN host parity"
}

test_stripped_no_symtab() {
    objcopy --strip-all "$TMP/exec.bin" "$TMP/exec.stripped"
    before=$(triplet_from_size "$SIZE_BIN" "$TMP/exec.bin")
    after=$(triplet_from_size "$SIZE_BIN" "$TMP/exec.stripped")
    [ "$before" = "$after" ] || fail "9e stripped changed section accounting"
    pass "9e stripped ELF still countable"
}

main() {
    build_tools
    build_rel_fixtures
    test_rel32_rel64
    build_exec_dyn
    test_exec_dyn_match_host
    test_stripped_no_symtab
}

main "$@"
