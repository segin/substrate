#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_roundtrip: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj;
    elfobj_t *reopen;
    elf_section_t *text;
    elf_symbol_t *sym;
    unsigned char code[] = {0x90, 0xC3};
    char *diag = NULL;

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) fail("elf_create");

    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) fail("elf_add_section .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("elf_section_set_data");

    sym = elf_add_symbol(obj, "func", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (!sym) fail("elf_add_symbol");

    if (elf_add_relocation(text, 0, sym, R_386_32, 0) != ELF_OK) fail("elf_add_relocation");

    if (elf_write_file(obj, "tmp_roundtrip.o") != ELF_OK) fail("elf_write_file");
    elf_close(obj);

    if (elf_open("tmp_roundtrip.o", &reopen) != ELF_OK) fail("elf_open");
    if (elf_validate(reopen, &diag) != ELF_OK) fail(diag ? diag : "elf_validate");

    if (!elf_find_section(reopen, ".text")) fail("missing .text after reopen");
    if (!elf_find_symbol(reopen, "func")) fail("missing symbol after reopen");

    free(diag);
    elf_close(reopen);
    return 0;
}
