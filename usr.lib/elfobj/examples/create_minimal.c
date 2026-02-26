#include <elfobj.h>

int main(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    unsigned char code[] = {0xC3};
    elf_section_t *text;

    if (!obj) return 1;
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) return 1;
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) return 1;

    if (elf_write_file(obj, "example_minimal.o") != ELF_OK) return 1;
    elf_close(obj);
    return 0;
}
