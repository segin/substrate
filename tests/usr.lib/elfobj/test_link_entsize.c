/*
 * test_link_entsize.c: an output section keeps an entry size only where
 * every input section gathered into it gives the same one.  One input's
 * 8 (as a .rodata.cst8-like table among Linux's .rodata) was taken for a
 * whole section whose size is no multiple of it; libelf then refused to
 * update the linked vmlinux ("invalid section entry size") and Linux's
 * resolve_btfids failed.
 */
#include <elfobj.h>
#include <elf_private.h>
#include <stdio.h>
#include <string.h>

/**
 * An x86-64 relocatable object with one .rodata section of @p size bytes,
 * of entry size @p entsize (0 for none) but not mergeable.
 *
 * @return the object, or NULL
 */
static elfobj_t *mk(size_t size, uint64_t entsize) {
    unsigned char bytes[16];
    elfobj_t *obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    elf_section_t *s;

    if (obj == NULL) {
        return NULL;
    }
    s = elf_add_section(obj, ".rodata", SHT_PROGBITS, SHF_ALLOC);
    memset(bytes, 0x5a, sizeof(bytes));
    if (s == NULL || elf_section_set_data(s, bytes, size) != ELF_OK) {
        return NULL;
    }
    if (entsize != 0 && (elf_section_set_merge(s, entsize, 0) != ELF_OK ||
                         elf_section_set_flags(s, SHF_ALLOC) != ELF_OK)) {
        return NULL;
    }
    return obj;
}

/**
 * Link @p a then @p b and check the output .rodata's entry size.
 *
 * @return 0 if it is @p want, else 1
 */
static int check(elfobj_t *a, elfobj_t *b, uint64_t want, const char *what) {
    elfobj_t *ins[2];
    elfobj_t *out = NULL;
    elf_section_t *s;

    ins[0] = a;
    ins[1] = b;
    if (elf_link(ins, 2, &out) != ELF_OK || (s = elf_find_section(out, ".rodata")) == NULL) {
        fprintf(stderr, "test_link_entsize: %s: link failed\n", what);
        return 1;
    }
    if (s->entsize != want || (s->entsize != 0 && elf_section_size(s) % s->entsize != 0)) {
        fprintf(stderr, "test_link_entsize: %s: entsize %llu (size %llu), want %llu\n", what,
                (unsigned long long)s->entsize, (unsigned long long)elf_section_size(s), (unsigned long long)want);
        elf_close(out);
        return 1;
    }
    elf_close(out);
    return 0;
}

int main(void) {
    elfobj_t *plain = mk(5, 0);
    elfobj_t *table = mk(8, 8);
    elfobj_t *table2 = mk(16, 8);
    int fail = 0;

    if (plain == NULL || table == NULL || table2 == NULL) {
        fprintf(stderr, "test_link_entsize: cannot make the inputs\n");
        return 1;
    }
    fail |= check(plain, table, 0, "plain then table");
    fail |= check(table, plain, 0, "table then plain");
    fail |= check(table, table2, 8, "two tables");
    elf_close(plain);
    elf_close(table);
    elf_close(table2);
    return fail;
}
