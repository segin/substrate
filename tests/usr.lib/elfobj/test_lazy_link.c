/*
 * An object opened with ELFOBJ_OPEN_LAZY_PARSE has its symbols and
 * relocations read on first use.  Linking it, resolving a symbol in it,
 * and removing or moving one of its sections are such uses: each must see
 * the same symbols and relocations an eagerly parsed object has.
 */
#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_private.h"

static int failures;

/**
 * @brief Report a failed check and count it.
 * @param msg What was expected.
 */
static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    failures++;
}

/**
 * @brief Serialize an object built in memory.
 * @param obj The object; it is closed.
 * @param size_out Receives the size of the image.
 * @return The image, to be freed by the caller.
 */
static uint8_t *serialize(elfobj_t *obj, size_t *size_out) {
    uint8_t *buf = NULL;

    if (obj == NULL || elf__write_to_buffer(obj, &buf, size_out) != ELF_OK) {
        fprintf(stderr, "FAIL: cannot build an input object\n");
        exit(1);
    }
    elf_close(obj);
    return buf;
}

/**
 * @brief Build the first input: a section ahead of .text, a global foo in
 *        .text, and a call from .text to the undefined bar.
 * @param size_out Receives the size of the image.
 * @return The image, to be freed by the caller.
 */
static uint8_t *build_caller(size_t *size_out) {
    static const uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    uint8_t code[16];
    elfobj_t *obj;
    elf_section_t *pad;
    elf_section_t *text;
    elf_symbol_t *foo;
    elf_symbol_t *bar;

    memset(code, 0x90, sizeof(code));
    obj = elf_create(ET_REL, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
    if (obj == NULL) {
        return serialize(NULL, size_out);
    }
    pad = elf_add_section(obj, ".junk", SHT_PROGBITS, SHF_ALLOC);
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (pad == NULL || text == NULL || elf_section_set_data(pad, junk, sizeof(junk)) != ELF_OK ||
        elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
        return serialize(NULL, size_out);
    }
    foo = elf_add_symbol(obj, "foo", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    bar = elf_add_symbol(obj, "bar", 0, 0, STB_GLOBAL, STT_NOTYPE);
    if (foo == NULL || bar == NULL || elf_symbol_define(foo, text, 0) != ELF_OK ||
        elf_add_relocation(text, 4, bar, R_X86_64_PLT32, -4) != ELF_OK) {
        return serialize(NULL, size_out);
    }
    return serialize(obj, size_out);
}

/**
 * @brief Build the second input: a global bar in .text.
 * @param size_out Receives the size of the image.
 * @return The image, to be freed by the caller.
 */
static uint8_t *build_callee(size_t *size_out) {
    static const uint8_t code[4] = {0xc3, 0x90, 0x90, 0x90};
    elfobj_t *obj;
    elf_section_t *text;
    elf_symbol_t *bar;

    obj = elf_create(ET_REL, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
    if (obj == NULL) {
        return serialize(NULL, size_out);
    }
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    bar = elf_add_symbol(obj, "bar", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (text == NULL || bar == NULL || elf_section_set_data(text, code, sizeof(code)) != ELF_OK ||
        elf_symbol_define(bar, text, 0) != ELF_OK) {
        return serialize(NULL, size_out);
    }
    return serialize(obj, size_out);
}

/**
 * @brief Open an image eagerly or lazily.
 * @param buf The image.
 * @param size Its size.
 * @param lazy Nonzero to open it with ELFOBJ_OPEN_LAZY_PARSE.
 * @return The object.
 */
static elfobj_t *open_image(const uint8_t *buf, size_t size, int lazy) {
    elfobj_t *obj = NULL;

    if (elf_open_memory_with_options(buf, size, lazy ? ELFOBJ_OPEN_LAZY_PARSE : 0, &obj) != ELF_OK) {
        fprintf(stderr, "FAIL: cannot open an input object\n");
        exit(1);
    }
    return obj;
}

/**
 * @brief Name the section a symbol is defined in.
 * @param obj The object holding the symbol.
 * @param name The symbol.
 * @return The section's name, or "(none)".
 */
static const char *defining_section(elfobj_t *obj, const char *name) {
    elf_symbol_t *sym = elf_find_symbol(obj, name);
    elf_section_t *sec;
    uint32_t shndx;

    if (sym == NULL) {
        return "(none)";
    }
    shndx = elf_symbol_shndx(sym);
    if (shndx == SHN_UNDEF || ELF_SHNDX_RESERVED(shndx)) {
        return "(none)";
    }
    sec = elf_section_get(obj, shndx - 1);
    return sec == NULL || elf_section_name(sec) == NULL ? "(none)" : elf_section_name(sec);
}

/**
 * @brief Link the two inputs, opened eagerly or lazily, and describe the output.
 * @param a The caller's image.
 * @param asz Its size.
 * @param b The callee's image.
 * @param bsz Its size.
 * @param lazy Nonzero to open the inputs lazily.
 * @param syms Receives the output's symbol count.
 * @param relocs Receives the output's relocation count.
 * @param foo_sec Receives the section foo is defined in (a static buffer).
 * @param bar_sec Receives the section bar is defined in (a static buffer).
 */
static void link_pair(const uint8_t *a, size_t asz, const uint8_t *b, size_t bsz, int lazy, size_t *syms,
                      size_t *relocs, char *foo_sec, char *bar_sec) {
    elfobj_t *in[2];
    elfobj_t *out = NULL;

    in[0] = open_image(a, asz, lazy);
    in[1] = open_image(b, bsz, lazy);
    if (elf_link(in, 2, &out) != ELF_OK || out == NULL) {
        fail(lazy ? "linking lazily opened inputs fails" : "linking eagerly opened inputs fails");
        *syms = 0;
        *relocs = 0;
        foo_sec[0] = '\0';
        bar_sec[0] = '\0';
    } else {
        *syms = elf_symbol_count(out);
        *relocs = elf_reloc_count(out);
        snprintf(foo_sec, 32, "%s", defining_section(out, "foo"));
        snprintf(bar_sec, 32, "%s", defining_section(out, "bar"));
        elf_close(out);
    }
    elf_close(in[0]);
    elf_close(in[1]);
}

/**
 * @brief elf_link() merges the symbols and relocations of lazily opened
 *        inputs exactly as it merges those of eagerly opened ones.
 */
static void test_link(const uint8_t *a, size_t asz, const uint8_t *b, size_t bsz) {
    size_t esyms, erelocs, lsyms, lrelocs;
    char efoo[32], ebar[32], lfoo[32], lbar[32];

    link_pair(a, asz, b, bsz, 0, &esyms, &erelocs, efoo, ebar);
    link_pair(a, asz, b, bsz, 1, &lsyms, &lrelocs, lfoo, lbar);
    if (erelocs != 1 || strcmp(efoo, ".text") != 0 || strcmp(ebar, ".text") != 0) {
        fail("the eager link does not give foo and bar in .text and one relocation");
    }
    if (lsyms != esyms) {
        fprintf(stderr, "  eager link: %zu symbols, lazy link: %zu\n", esyms, lsyms);
        fail("a link of lazily opened inputs merges a different number of symbols");
    }
    if (lrelocs != erelocs) {
        fprintf(stderr, "  eager link: %zu relocations, lazy link: %zu\n", erelocs, lrelocs);
        fail("a link of lazily opened inputs merges a different number of relocations");
    }
    if (strcmp(lfoo, efoo) != 0 || strcmp(lbar, ebar) != 0) {
        fail("a link of lazily opened inputs does not define foo and bar where the eager link does");
    }
}

/**
 * @brief elf_link_resolve_symbol() finds a definition in lazily opened inputs.
 */
static void test_resolve(const uint8_t *a, size_t asz, const uint8_t *b, size_t bsz) {
    elfobj_t *in[2];
    elf_symbol_t *sym;
    size_t idx = 99;

    in[0] = open_image(a, asz, 1);
    in[1] = open_image(b, bsz, 1);
    sym = elf_link_resolve_symbol(in, 2, "bar", &idx);
    if (sym == NULL || idx != 1 || elf_symbol_shndx(sym) == SHN_UNDEF) {
        fail("elf_link_resolve_symbol() does not find bar's definition among lazily opened inputs");
    }
    elf_close(in[0]);
    elf_close(in[1]);
}

/**
 * @brief Find a section by name.
 * @param obj The object.
 * @param name The section's name.
 * @return The section, or NULL.
 */
static elf_section_t *section_named(elfobj_t *obj, const char *name) {
    size_t i;

    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);

        if (sec != NULL && elf_section_name(sec) != NULL && strcmp(elf_section_name(sec), name) == 0) {
            return sec;
        }
    }
    return NULL;
}

/** What a rearranged caller says of foo and of its one relocation. */
struct caller_state {
    int changed;          /* the change succeeded */
    uint32_t foo_shndx;   /* foo's section index, or SHN_UNDEF when foo is missing */
    size_t relocs;        /* the object's relocation count */
    char rsec[32];        /* the first relocation's section, or "" */
    char rsym[32];        /* the first relocation's symbol, or "" */
};

/**
 * @brief Open the caller, remove .junk from it or move .text last, and
 *        describe the result.
 * @param a The caller's image.
 * @param asz Its size.
 * @param lazy Nonzero to open it with ELFOBJ_OPEN_LAZY_PARSE.
 * @param reorder Nonzero to move .text last, zero to remove .junk.
 * @param st Receives the description.
 */
static void rearrange_caller(const uint8_t *a, size_t asz, int lazy, int reorder, struct caller_state *st) {
    elfobj_t *obj = open_image(a, asz, lazy);
    elf_symbol_t *foo;
    elf_reloc_t *r;
    elf_section_t *sec;

    memset(st, 0, sizeof(*st));
    if (reorder) {
        sec = section_named(obj, ".text");
        st->changed = sec != NULL && elf_reorder_section(obj, sec, elf_section_count(obj) - 1) == ELF_OK;
    } else {
        sec = section_named(obj, ".junk");
        st->changed = sec != NULL && elf_remove_section(obj, sec) == ELF_OK;
    }
    foo = elf_find_symbol(obj, "foo");
    st->foo_shndx = foo == NULL ? SHN_UNDEF : elf_symbol_shndx(foo);
    st->relocs = elf_reloc_count(obj);
    if (st->relocs > 0) {
        r = elf_reloc_at(obj, 0);
        sec = elf_reloc_section(r);
        snprintf(st->rsec, sizeof(st->rsec), "%s",
                 sec == NULL || elf_section_name(sec) == NULL ? "" : elf_section_name(sec));
        snprintf(st->rsym, sizeof(st->rsym), "%s",
                 elf_reloc_symbol(r) == NULL || elf_symbol_name(elf_reloc_symbol(r)) == NULL
                     ? ""
                     : elf_symbol_name(elf_reloc_symbol(r)));
    }
    elf_close(obj);
}

/**
 * @brief elf_remove_section() and elf_reorder_section() leave a lazily
 *        opened object's symbols and relocations as they leave an eagerly
 *        opened one's.
 */
static void test_section_changes(const uint8_t *a, size_t asz) {
    static const char *const what[2] = {"removing .junk", "moving .text last"};
    struct caller_state eager;
    struct caller_state lazy;
    int reorder;

    for (reorder = 0; reorder < 2; ++reorder) {
        rearrange_caller(a, asz, 0, reorder, &eager);
        rearrange_caller(a, asz, 1, reorder, &lazy);
        if (!eager.changed || !lazy.changed) {
            fprintf(stderr, "  %s fails\n", what[reorder]);
            fail("a section change is refused");
            continue;
        }
        if (eager.relocs != 1 || strcmp(eager.rsec, ".text") != 0 || strcmp(eager.rsym, "bar") != 0) {
            fprintf(stderr, "  after %s the eager object has no call to bar in .text\n", what[reorder]);
            fail("a section change on an eagerly opened object misplaces its relocation");
        }
        if (lazy.foo_shndx != eager.foo_shndx) {
            fprintf(stderr, "  after %s foo has section index %u eagerly, %u lazily\n", what[reorder],
                    (unsigned)eager.foo_shndx, (unsigned)lazy.foo_shndx);
            fail("a section change on a lazily opened object leaves foo in another section");
        }
        if (lazy.relocs != eager.relocs || strcmp(lazy.rsec, eager.rsec) != 0 ||
            strcmp(lazy.rsym, eager.rsym) != 0) {
            fprintf(stderr, "  after %s: eagerly %zu relocations (%s against %s), lazily %zu (%s against %s)\n",
                    what[reorder], eager.relocs, eager.rsec, eager.rsym, lazy.relocs, lazy.rsec, lazy.rsym);
            fail("a section change on a lazily opened object loses or misplaces its relocation");
        }
    }
}

int main(void) {
    size_t asz = 0;
    size_t bsz = 0;
    uint8_t *a = build_caller(&asz);
    uint8_t *b = build_callee(&bsz);

    test_link(a, asz, b, bsz);
    test_resolve(a, asz, b, bsz);
    test_section_changes(a, asz);
    free(a);
    free(b);
    if (failures != 0) {
        return 1;
    }
    printf("PASS: lazily parsed objects link and rearrange as eagerly parsed ones\n");
    return 0;
}
