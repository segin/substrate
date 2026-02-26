#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_matrix: %s\n", msg);
    exit(1);
}

static void build_and_check(uint16_t type, uint16_t machine, elfobj_class_t cls,
                            elfobj_endian_t endian, const char *path) {
    elfobj_t *obj;
    elfobj_t *reopen = NULL;
    elf_section_t *text;
    elf_symbol_t *sym;
    char *diag = NULL;
    uint8_t code[] = {0x90, 0x90, 0x90, 0x90, 0xC3};

    obj = elf_create(type, machine, cls, endian);
    if (obj == NULL) fail("elf_create");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("add .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set text");

    sym = elf_add_symbol(obj, "entry", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (sym == NULL) fail("add symbol");
    if (elf_symbol_define(sym, text, 0) != ELF_OK) fail("define symbol");
    if (type == ET_REL && elf_add_relocation(text, 0, sym,
            cls == ELFOBJ_CLASS_64 ? R_X86_64_PC32 : R_386_PC32, -4) != ELF_OK) {
        fail("add relocation");
    }
    if (type == ET_EXEC || type == ET_DYN) {
        elf_segment_t *load = elf_add_load_segment(obj, 5, 0x1000);
        if (load == NULL) fail("add load segment");
        if (elf_segment_add_section(load, text) != ELF_OK) fail("map text to load segment");
    }

    if (elf_write_file(obj, path) != ELF_OK) fail("write file");
    elf_close(obj);

    if (elf_open(path, &reopen) != ELF_OK) fail("reopen file");
    if (elf_type(reopen) != type) fail("type mismatch");
    if (elf_machine(reopen) != machine) fail("machine mismatch");
    if (elf_class(reopen) != cls) fail("class mismatch");
    if (elf_endian(reopen) != endian) fail("endian mismatch");
    if (type == ET_EXEC || type == ET_DYN) {
        if (elf_set_validation_mode(reopen, ELF_VALIDATE_PERMISSIVE) != ELF_OK)
            fail("set permissive validation mode");
    }
    if (elf_validate(reopen, &diag) != ELF_OK) {
        fprintf(stderr, "%s\n", diag ? diag : "");
        fail("validate reopened");
    }
    free(diag);
    elf_close(reopen);
}

static void test_core_readonly(void) {
    uint8_t core64[64];
    elfobj_t *obj = NULL;
    memset(core64, 0, sizeof(core64));

    core64[0] = 0x7f;
    core64[1] = 'E';
    core64[2] = 'L';
    core64[3] = 'F';
    core64[4] = 2;  /* ELFCLASS64 */
    core64[5] = 1;  /* little-endian */
    core64[6] = 1;  /* EV_CURRENT */
    core64[16] = ET_CORE & 0xff;
    core64[17] = (ET_CORE >> 8) & 0xff;
    core64[18] = EM_X86_64 & 0xff;
    core64[19] = (EM_X86_64 >> 8) & 0xff;
    core64[20] = 1; /* e_version */
    core64[52] = 64; /* e_ehsize */

    if (elf_open_memory(core64, sizeof(core64), &obj) != ELF_OK) fail("open core image");
    if (elf_type(obj) != ET_CORE) fail("core type mismatch");
    if (elf_add_section(obj, ".text", SHT_PROGBITS, 0) != NULL) fail("core object must be read-only");
    elf_close(obj);
}

static void test_abi_conformance(void) {
    elfobj_t *obj32;
    elfobj_t *obj64;
    uint64_t out = 0;

    if (elf_reloc_size_for_machine(EM_386, R_386_PC32) != 4) fail("i386 reloc size");
    if (elf_reloc_is_pc_relative_for_machine(EM_386, R_386_PC32) != 1) fail("i386 pc-relative");
    if (elf_reloc_size_for_machine(EM_X86_64, R_X86_64_64) != 8) fail("x86_64 reloc size");
    if (elf_reloc_is_pc_relative_for_machine(EM_X86_64, R_X86_64_PC32) != 1) fail("x86_64 pc-relative");

    obj32 = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    obj64 = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj32 == NULL || obj64 == NULL) fail("create abi objects");

    if (elf_apply_relocation_value(obj32, R_386_PC32, 0x1000, 0x1010, -4, &out) != ELF_OK)
        fail("apply i386 pc32");
    if (out != 0x0c) fail("i386 PC32 formula mismatch");

    if (elf_apply_relocation_value(obj64, R_X86_64_64, 0x2000, 0x100000, 0x20, &out) != ELF_OK)
        fail("apply x86_64_64");
    if (out != 0x100020) fail("x86_64_64 formula mismatch");

    if (elf_apply_relocation_value(obj64, R_X86_64_PC32, 0x2000, 0x2010, -4, &out) != ELF_OK)
        fail("apply x86_64_pc32");
    if (out != 0x0c) fail("x86_64 PC32 formula mismatch");

    elf_close(obj32);
    elf_close(obj64);
}

int main(void) {
    /* ELF32/64 and little/big-endian coverage */
    build_and_check(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE, "tmp_matrix_rel32le.o");
    build_and_check(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_BE, "tmp_matrix_rel32be.o");
    build_and_check(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE, "tmp_matrix_rel64le.o");
    build_and_check(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE, "tmp_matrix_rel64be.o");

    /* ET_EXEC and ET_DYN read/write coverage */
    build_and_check(ET_EXEC, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE, "tmp_matrix_exec32.elf");
    build_and_check(ET_EXEC, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE, "tmp_matrix_exec64.elf");
    build_and_check(ET_DYN, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE, "tmp_matrix_dyn32.elf");
    build_and_check(ET_DYN, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE, "tmp_matrix_dyn64.elf");

    test_core_readonly();
    test_abi_conformance();
    return 0;
}
