#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_private.h"

int elfobj_section_is_gnu_extension(const elf_section_t *section);

#define fail(msg) do { fprintf(stderr, "FAIL: %s at %s:%d\n", msg, __FILE__, __LINE__); exit(1); } while (0)

int main(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fail("elf_create");
    }

    /* Test null section */
    if (elfobj_section_is_gnu_extension(NULL)) {
        fail("null section returned true");
    }

    /* Test section with null name */
    elf_section_t *sec_null = elf_add_section(obj, NULL, SHT_PROGBITS, 0);
    if (elfobj_section_is_gnu_extension(sec_null)) {
        fail("section with null name returned true");
    }

    /* Test .gnu prefixed sections */
    elf_section_t *sec_gnu1 = elf_add_section(obj, ".gnu.version", SHT_PROGBITS, 0);
    if (!elfobj_section_is_gnu_extension(sec_gnu1)) {
        fail(".gnu.version returned false");
    }

    elf_section_t *sec_gnu2 = elf_add_section(obj, ".gnu.hash", SHT_PROGBITS, 0);
    if (!elfobj_section_is_gnu_extension(sec_gnu2)) {
        fail(".gnu.hash returned false");
    }

    /* Test specific non-.gnu prefix sections */
    elf_section_t *sec_build_id = elf_add_section(obj, ".note.gnu.build-id", SHT_PROGBITS, 0);
    if (!elfobj_section_is_gnu_extension(sec_build_id)) {
        fail(".note.gnu.build-id returned false");
    }

    /* Test false positives */
    elf_section_t *sec_gn = elf_add_section(obj, ".gn", SHT_PROGBITS, 0);
    if (elfobj_section_is_gnu_extension(sec_gn)) {
        fail(".gn returned true");
    }

    elf_section_t *sec_gnux = elf_add_section(obj, ".gnux", SHT_PROGBITS, 0);
    if (!elfobj_section_is_gnu_extension(sec_gnux)) {
        fail(".gnux returned false");
    }

    elf_section_t *sec_text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (elfobj_section_is_gnu_extension(sec_text)) {
        fail(".text returned true");
    }

    elf_close(obj);

    return 0;
}
