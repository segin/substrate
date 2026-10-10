/*
 * What the three tests of a relocation table share (test_x86_reloc_core,
 * test_arm_reloc_core, test_a64_reloc_core): each table names a
 * relocation by an enumerator of its own and gives the number the ABI
 * gives it.
 *
 * A table is held to two things that do not come from the header it was
 * written from.  The numbers here are written out, from the processor
 * supplements, and not as the R_* macros the table itself uses -- with
 * the macros a table and its test agree whatever the header says.  And
 * every relocation is then written into an object, which the script
 * gives to readelf: the names printed there are binutils' reading of the
 * numbers, and must be the names the enumerators have.
 */
#ifndef SUBSTRATE_TESTS_AS_RELOC_TABLE_CHECK_H
#define SUBSTRATE_TESTS_AS_RELOC_TABLE_CHECK_H

#include "elfobj.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A kind, the name readelf gives its number, and the number. */
struct reloc_case {
    int kind;
    const char *name;
    uint32_t number;
};

typedef int (*reloc_type_fn)(unsigned machine, int kind, uint32_t *type_out);
typedef int (*reloc_emit_fn)(elf_section_t *section, unsigned machine, int kind, uint64_t offset,
                             elf_symbol_t *symbol, int64_t addend);

#define RELOC_CASE_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define RELOC_UNTOUCHED 0xdeadbeefu
#define RELOC_SLOT 16u

static int reloc_failures;

static void reloc_fail(const char *what, const char *name) {
    fprintf(stderr, "FAIL: %s: %s\n", what, name);
    reloc_failures++;
}

/* Each kind has its number, for its machine. */
static void reloc_check_numbers(unsigned machine, const struct reloc_case *cases, size_t count,
                                reloc_type_fn type_of) {
    size_t i;

    for (i = 0; i < count; i++) {
        uint32_t t = RELOC_UNTOUCHED;

        if (type_of(machine, cases[i].kind, &t) != 0) {
            reloc_fail("a kind of this machine is refused", cases[i].name);
        } else if (t != cases[i].number) {
            fprintf(stderr, "FAIL: %s is %u, and the ABI's number is %u\n", cases[i].name, (unsigned)t,
                    (unsigned)cases[i].number);
            reloc_failures++;
        }
    }
}

/*
 * Each kind is refused for a machine that is not its own, and a kind
 * that is none of them is refused for every machine; a refusal leaves
 * the result as it was.  Nowhere to put the result is refused too.
 */
static void reloc_check_refusals(const unsigned *machines, size_t machine_count, unsigned own,
                                 const struct reloc_case *cases, size_t count, int kind_past_end,
                                 reloc_type_fn type_of) {
    size_t i, m;
    uint32_t t;

    for (m = 0; m < machine_count; m++) {
        if (machines[m] == own) {
            continue;
        }
        for (i = 0; i < count; i++) {
            t = RELOC_UNTOUCHED;
            if (type_of(machines[m], cases[i].kind, &t) == 0) {
                reloc_fail("a kind is given a number for a machine that is not its own", cases[i].name);
            } else if (t != RELOC_UNTOUCHED) {
                reloc_fail("a refusal wrote a number all the same", cases[i].name);
            }
        }
    }
    for (m = 0; m < machine_count; m++) {
        t = RELOC_UNTOUCHED;
        if (type_of(machines[m], kind_past_end, &t) == 0 || t != RELOC_UNTOUCHED) {
            reloc_fail("a kind past the last is given a number", "past the end");
        }
        t = RELOC_UNTOUCHED;
        if (type_of(machines[m], -1, &t) == 0 || t != RELOC_UNTOUCHED) {
            reloc_fail("a kind before the first is given a number", "-1");
        }
    }
    if (count > 0 && type_of(own, cases[0].kind, NULL) == 0) {
        reloc_fail("a number is given with nowhere to put it", cases[0].name);
    }
}

/*
 * Write an object with one relocation of each kind, RELOC_SLOT bytes
 * apart, against two symbols in turn, and print the line readelf -rW
 * should have for each: offset, name, symbol and, where the machine
 * keeps addends in the relocation, the addend with its sign.
 *
 * Before that: a relocation with no section, with no symbol, of a kind
 * there is not, or past the end of the section is refused, and adds
 * nothing.
 */
static void reloc_write_object(const char *path, unsigned machine, elfobj_class_t cls, int with_addends,
                               const struct reloc_case *cases, size_t count, int kind_past_end,
                               reloc_emit_fn emit) {
    elfobj_t *o;
    elf_section_t *text;
    elf_symbol_t *sym[2];
    static const char *const sym_name[2] = { "alpha", "beta" };
    unsigned char *data;
    size_t size = (count + 1) * RELOC_SLOT;
    size_t i;

    data = calloc(size, 1);
    o = elf_create(ET_REL, (uint16_t)machine, cls, ELFOBJ_ENDIAN_LE);
    if (data == NULL || o == NULL) {
        reloc_fail("no object to write relocations to", path);
        return;
    }
    text = elf_add_section(o, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL || elf_section_set_data(text, data, size) != ELF_OK) {
        reloc_fail("no section to write relocations to", path);
        return;
    }
    sym[0] = elf_add_symbol(o, sym_name[0], 0, 0, STB_GLOBAL, STT_FUNC);
    sym[1] = elf_add_symbol(o, sym_name[1], 0, 0, STB_GLOBAL, STT_NOTYPE);
    if (sym[0] == NULL || sym[1] == NULL || elf_symbol_define(sym[0], text, 0) != ELF_OK) {
        reloc_fail("no symbols to relocate against", path);
        return;
    }

    if (count > 0) {
        if (emit(NULL, machine, cases[0].kind, 0, sym[0], 0) == 0) {
            reloc_fail("a relocation with no section is accepted", cases[0].name);
        }
        if (emit(text, machine, cases[0].kind, 0, NULL, 0) == 0) {
            reloc_fail("a relocation with no symbol is accepted", cases[0].name);
        }
        if (emit(text, machine, kind_past_end, 0, sym[0], 0) == 0) {
            reloc_fail("a relocation of a kind there is not is accepted", "past the end");
        }
        if (emit(text, machine, cases[0].kind, size, sym[0], 0) == 0) {
            reloc_fail("a relocation past the end of its section is accepted", cases[0].name);
        }
        if (elf_reloc_count(o) != 0) {
            reloc_fail("a refused relocation was added", cases[0].name);
        }
    }

    for (i = 0; i < count; i++) {
        uint64_t offset = i * RELOC_SLOT + (i % 3);
        /* Positive, negative and none, and no two alike in size. */
        int64_t addend = !with_addends ? 0 : (i % 3 == 0) ? (int64_t)(0x100 + i) : (i % 3 == 1) ? -(int64_t)(4 + i) : 0;

        if (emit(text, machine, cases[i].kind, offset, sym[i % 2], addend) != 0) {
            reloc_fail("a relocation is refused", cases[i].name);
            continue;
        }
        if (cls == ELFOBJ_CLASS_64) {
            printf("%016" PRIx64 " %s %s", offset, cases[i].name, sym_name[i % 2]);
        } else {
            printf("%08" PRIx64 " %s %s", offset, cases[i].name, sym_name[i % 2]);
        }
        if (with_addends) {
            printf(" %c %" PRIx64, addend < 0 ? '-' : '+', (uint64_t)(addend < 0 ? -addend : addend));
        }
        putchar('\n');
    }
    if (elf_reloc_count(o) != count) {
        reloc_fail("the object does not have one relocation for each kind", path);
    }
    if (elf_write_file(o, path) != ELF_OK) {
        reloc_fail("the object cannot be written", path);
    }
    elf_close(o);
    free(data);
}

#endif
