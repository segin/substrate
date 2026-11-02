#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_reader_writer: %s\n", msg);
    exit(1);
}

static void test_exec_dynstr_phdr(void) {
    elfobj_t *obj;
    elfobj_t *reopen;
    elf_section_t *text;
    elf_section_t *dyn;
    elf_symbol_t *sym;
    /* Room for the 8 bytes R_X86_64_64 stores at offset 0: the library
     * refuses a relocation that would write past its section. */
    uint8_t code[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0xC3};

    obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) fail("elf_create ET_DYN");

    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) fail("add .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set .text data");
    /* An image's loaded sections each have an address of their own: the
     * reader refuses two that overlap. */
    if (elf_section_set_addr(text, 0x1000) != ELF_OK) fail("set .text addr");

    dyn = elf_add_section(obj, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
    if (!dyn) fail("add .dynamic");
    if (elf_section_set_data(dyn, code, 1) != ELF_OK) fail("set .dynamic data");
    if (elf_section_set_addr(dyn, 0x2000) != ELF_OK) fail("set .dynamic addr");

    sym = elf_add_symbol(obj, "dynfunc", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (!sym) fail("add symbol");
    if (elf_add_relocation(text, 0, sym, 1, 0) != ELF_OK) fail("add relocation");

    if (elf_write_file(obj, "tmp_dyn64.elf") != ELF_OK) fail("write ET_DYN");
    elf_close(obj);

    if (elf_open("tmp_dyn64.elf", &reopen) != ELF_OK) fail("open ET_DYN");
    if (elf_program_header_count(reopen) == 0) fail("missing program headers");
    {
        /* the .dynstr made up for it goes past the sections that were
         * placed, not at address 0 */
        elf_section_t *ds = elf_find_section(reopen, ".dynstr");
        if (!ds) fail("missing .dynstr");
        if (elf_section_addr(ds) < 0x2001) fail(".dynstr overlaps the placed sections");
    }
    elf_close(reopen);
}

static void test_rel_and_rela_emission(void) {
    elfobj_t *obj32;
    elfobj_t *obj64;
    elfobj_t *reopen;
    elf_section_t *text;
    elf_symbol_t *sym;
    /* Room for the 4 bytes R_386_32 and R_X86_64_PC32 store at offset 0. */
    uint8_t code[] = {0x90, 0x90, 0x90, 0x90, 0xC3};

    obj32 = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj32) fail("create 32");
    text = elf_add_section(obj32, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    sym = elf_add_symbol(obj32, "f32", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (!text || !sym) fail("setup 32");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set 32 code");
    if (elf_add_relocation(text, 0, sym, R_386_32, 0) != ELF_OK) fail("rel 32");
    if (elf_write_file(obj32, "tmp_rel32.o") != ELF_OK) fail("write 32");
    elf_close(obj32);

    if (elf_open("tmp_rel32.o", &reopen) != ELF_OK) fail("open 32");
    if (!elf_find_section(reopen, ".rel.text")) fail("missing .rel.text");
    elf_close(reopen);

    obj64 = elf_create(ET_REL, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj64) fail("create 64");
    text = elf_add_section(obj64, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    sym = elf_add_symbol(obj64, "f64", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (!text || !sym) fail("setup 64");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set 64 code");
    if (elf_add_relocation(text, 0, sym, 2, 4) != ELF_OK) fail("rela 64");
    if (elf_write_file(obj64, "tmp_rela64.o") != ELF_OK) fail("write 64");
    elf_close(obj64);

    if (elf_open("tmp_rela64.o", &reopen) != ELF_OK) fail("open 64");
    if (!elf_find_section(reopen, ".rela.text")) fail("missing .rela.text");
    elf_close(reopen);
}

int main(void) {
    test_exec_dynstr_phdr();
    test_rel_and_rela_emission();
    return 0;
}
