/*
 * test_link_name_hook_once.c: a link asks the section name hook once for
 * each input section, not once for each relocation.
 *
 * The merge asked it for the section of each relocation and of the
 * symbol it names, and ld's hook matches an input section against every
 * pattern of a linker script: in a link of Linux's vmlinux, whose
 * vmlinux.o has millions of relocations, nine tenths of the time (about
 * 25 minutes of each of the three links) went in it.
 *
 * Two inputs of a .text with 500 relocations against a symbol of it, and
 * a .data with 500 against one of .text: the hook may be asked a few
 * times for each of the four sections, and every answer it gave is the
 * one the output has (a renamed section, a discarded one).
 */
#include <elfobj.h>
#include <elf_private.h>
#include <stdio.h>
#include <string.h>

#define NREL 500

static unsigned long calls;

/** The section name hook: .data goes to .rodata.out, .drop is left out, the rest keep their names. */
static const char *name_hook(const char *section, const char *file, void *user) {
    (void)file;
    (void)user;
    ++calls;
    if (strcmp(section, ".data") == 0) {
        return ".rodata.out";
    }
    if (strcmp(section, ".drop") == 0) {
        return NULL;
    }
    return section;
}

/**
 * An x86-64 relocatable object: .text of NREL * 4 bytes with a local symbol at 0 and NREL R_X86_64_PC32
 * against it, .data of as many with NREL R_X86_64_32 against it, and a .drop.
 *
 * @return the object, or NULL
 */
static elfobj_t *mk(const char *sym_name) {
    static unsigned char bytes[NREL * 4];
    elfobj_t *obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    elf_section_t *text;
    elf_section_t *data;
    elf_section_t *drop;
    elf_symbol_t *sym;
    size_t i;

    if (obj == NULL) {
        return NULL;
    }
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    data = elf_add_section(obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    drop = elf_add_section(obj, ".drop", SHT_PROGBITS, SHF_ALLOC);
    if (text == NULL || data == NULL || drop == NULL || elf_section_set_data(text, bytes, sizeof(bytes)) != ELF_OK ||
        elf_section_set_data(data, bytes, sizeof(bytes)) != ELF_OK || elf_section_set_data(drop, bytes, 4) != ELF_OK) {
        return NULL;
    }
    sym = elf_add_symbol(obj, sym_name, 0, 0, STB_LOCAL, STT_FUNC);
    if (sym == NULL || elf_symbol_define(sym, text, 0) != ELF_OK) {
        return NULL;
    }
    for (i = 0; i < NREL; ++i) {
        if (elf_add_relocation(text, i * 4, sym, 2 /* R_X86_64_PC32 */, -4) != ELF_OK ||
            elf_add_relocation(data, i * 4, sym, 10 /* R_X86_64_32 */, 0) != ELF_OK) {
            return NULL;
        }
    }
    return obj;
}

int main(void) {
    elfobj_t *a = mk("fa");
    elfobj_t *b = mk("fb");
    elf_link_plan_t *plan = elf_link_plan_create();
    elfobj_t *out = NULL;
    elf_section_t *s;
    int fail = 0;

    if (a == NULL || b == NULL || plan == NULL || elf_link_plan_add_input(plan, a, "a.o") != ELF_OK ||
        elf_link_plan_add_input(plan, b, "b.o") != ELF_OK ||
        elf_link_plan_set_section_name_hook(plan, name_hook, NULL) != ELF_OK) {
        fprintf(stderr, "test_link_name_hook_once: setup failed\n");
        return 1;
    }
    if (elf_link_plan_link(plan, &out) != ELF_OK || out == NULL) {
        fprintf(stderr, "test_link_name_hook_once: link failed\n");
        return 1;
    }
    /* (six input sections: a few askings each, never one a relocation) */
    if (calls > 6 * 8) {
        fprintf(stderr, "test_link_name_hook_once: the hook was asked %lu times for 6 sections and %d relocations\n",
                calls, 4 * NREL);
        fail = 1;
    }
    s = elf_find_section(out, ".rodata.out");
    if (s == NULL || elf_section_size(s) != 2 * NREL * 4 || elf_section_reloc_count(s) != 2 * NREL) {
        fprintf(stderr, "test_link_name_hook_once: .data is not all in .rodata.out with its relocations\n");
        fail = 1;
    }
    if (elf_find_section(out, ".data") != NULL || elf_find_section(out, ".drop") != NULL) {
        fprintf(stderr, "test_link_name_hook_once: a renamed or left-out section is in the output\n");
        fail = 1;
    }
    s = elf_find_section(out, ".text");
    if (s == NULL || elf_section_reloc_count(s) != 2 * NREL) {
        fprintf(stderr, "test_link_name_hook_once: .text has not its relocations\n");
        fail = 1;
    }
    if (fail == 0) {
        printf("test_link_name_hook_once: ok (%lu askings)\n", calls);
    }
    elf_close(out);
    elf_link_plan_destroy(plan);
    elf_close(a);
    elf_close(b);
    return fail;
}
