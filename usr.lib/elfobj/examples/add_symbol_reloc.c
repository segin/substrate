#include <elfobj.h>

int main(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *text;
    elf_symbol_t *sym;
    /* `call target; ret`: the call's 4-byte displacement, at offset 1, is what the relocation fills (a
     * relocation wider than what is left of the section is refused) */
    unsigned char code[] = {0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3};

    if (!obj) return 1;
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) return 1;
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) return 1;
    sym = elf_add_symbol(obj, "target", 0, 0, STB_GLOBAL, STT_FUNC);
    if (!sym) return 1;
    if (elf_add_relocation(text, 1, sym, R_386_PC32, -4) != ELF_OK) return 1;
    if (elf_write_file(obj, "example_reloc.o") != ELF_OK) return 1;
    elf_close(obj);
    return 0;
}
