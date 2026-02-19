#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>

static elfobj_t *mk(const char *secname, const char *symname) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s;
    unsigned char b = 0x90;
    if (!obj) return NULL;
    s = elf_add_section(obj, secname, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!s) return NULL;
    if (elf_section_set_data(s, &b, 1) != ELF_OK) return NULL;
    if (!elf_add_symbol(obj, symname, 0, 1, STB_GLOBAL, STT_FUNC)) return NULL;
    return obj;
}

int main(void) {
    elfobj_t *a = mk(".text", "a");
    elfobj_t *b = mk(".text", "b");
    elfobj_t *out = NULL;
    elfobj_t *ins[2];

    if (!a || !b) return 1;
    ins[0] = a;
    ins[1] = b;

    if (elf_link(ins, 2, &out) != ELF_OK) return 1;
    if (!elf_find_symbol(out, "a") || !elf_find_symbol(out, "b")) return 1;
    if (!elf_find_section(out, ".text")) return 1;

    elf_close(out);
    elf_close(a);
    elf_close(b);
    return 0;
}
