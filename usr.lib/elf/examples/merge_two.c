#include <elfobj.h>

static elfobj_t *mk(const char *name) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *text;
    unsigned char code[] = {0xC3};
    if (!obj) return 0;
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) return 0;
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) return 0;
    if (!elf_add_symbol(obj, name, 0, 1, STB_GLOBAL, STT_FUNC)) return 0;
    return obj;
}

int main(void) {
    elfobj_t *a = mk("f1");
    elfobj_t *b = mk("f2");
    elfobj_t *out;
    elfobj_t *in[2];
    if (!a || !b) return 1;
    in[0] = a;
    in[1] = b;
    if (elf_link(in, 2, &out) != ELF_OK) return 1;
    if (elf_write_file(out, "example_merge.o") != ELF_OK) return 1;
    elf_close(out);
    elf_close(a);
    elf_close(b);
    return 0;
}
