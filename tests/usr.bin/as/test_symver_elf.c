#include "as_data.h"
#include "as_elf_emit.h"
#include "as_lexer.h"
#include "as_parser.h"
#include "as_sections.h"
#include "as_symtab.h"
#include "elfobj.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static uint16_t rd16(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_LE) {
        return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
    }
    return (uint16_t)(p[1] | (uint16_t)p[0] << 8);
}

static uint32_t rd32(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_LE) {
        return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    return (uint32_t)p[3] | ((uint32_t)p[2] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[0] << 24);
}

static size_t find_symbol_index(elfobj_t *obj, const char *name) {
    size_t i;
    size_t n = elf_symbol_count(obj);

    for (i = 0; i < n; ++i) {
        elf_symbol_t *sym = elf_symbol_at(obj, i);
        const char *nm = sym != NULL ? elf_symbol_name(sym) : NULL;
        if (nm != NULL && strcmp(nm, name) == 0) {
            return i;
        }
    }
    return (size_t)-1;
}

int main(int argc, char **argv) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_elf_cfg_t ecfg;
    as_token_vec_t toks;
    as_parse_result_t parsed;
    as_symtab_t syms;
    as_section_state_t secs;
    as_data_program_t data;
    elfobj_t *obj = NULL;
    char err[256];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <input.s> <output.o>\n", argv[0]);
        return 2;
    }

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    memset(&ecfg, 0, sizeof(ecfg));
    pcfg.arch = AS_PARSER_ARCH_X86;
    ecfg.machine = EM_X86_64;
    ecfg.is_64 = 1;
    ecfg.use_rela = 1;
    ecfg.x86_64_isa_level = 3;

    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);
    as_symtab_init(&syms);
    as_section_state_init(&secs);
    as_data_program_init(&data);

    if (as_lex_file(argv[1], &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("parse failed");
    }
    if (as_symtab_build(&parsed, &syms, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("symtab failed");
    }
    if (as_sections_build(&parsed, &secs, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("sections failed");
    }
    if (as_data_build(&parsed, &data, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("data failed");
    }
    if (as_elf_emit_file(&parsed, &secs, &syms, &data, &ecfg, argv[2], err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("emit failed");
    }

    if (elf_open(argv[2], &obj) != ELF_OK || obj == NULL) {
        fail("elf_open failed");
    }

    {
        elf_section_t *versym = elf_find_section(obj, ".gnu.version");
        elf_section_t *verdef = elf_find_section(obj, ".gnu.version_d");
        elf_section_t *strtab = elf_find_section(obj, ".strtab");
        size_t versym_size = 0;
        size_t verdef_size = 0;
        size_t strtab_size = 0;
        const uint8_t *versym_data = versym ? (const uint8_t *)elf_section_data(versym, &versym_size) : NULL;
        const uint8_t *verdef_data = verdef ? (const uint8_t *)elf_section_data(verdef, &verdef_size) : NULL;
        const uint8_t *strtab_data = strtab ? (const uint8_t *)elf_section_data(strtab, &strtab_size) : NULL;
        elfobj_endian_t e = elf_endian(obj);
        size_t sym_index;
        uint16_t verndx;
        uint16_t vd_version;
        uint16_t vd_ndx;
        uint16_t vd_cnt;
        uint32_t vd_aux;
        uint32_t vda_name;
        const char *vname;

        if (versym == NULL || verdef == NULL || strtab == NULL) {
            fail("missing GNU version sections");
        }
        sym_index = find_symbol_index(obj, "gsym");
        if (sym_index == (size_t)-1) {
            fail("missing gsym symbol");
        }
        if (versym_data == NULL || versym_size < (sym_index + 1) * 2) {
            fail("invalid versym size");
        }
        verndx = rd16(versym_data + sym_index * 2, e);
        if (verndx != 2) {
            fail("unexpected versym index");
        }
        if (verdef_data == NULL || verdef_size < 28) {
            fail("invalid verdef size");
        }
        vd_version = rd16(verdef_data + 0, e);
        vd_ndx = rd16(verdef_data + 4, e);
        vd_cnt = rd16(verdef_data + 6, e);
        vd_aux = rd32(verdef_data + 12, e);
        if (vd_version != 1 || vd_cnt == 0 || vd_ndx != 2) {
            fail("verdef header mismatch");
        }
        if (vd_aux + 8 > verdef_size) {
            fail("verdef aux out of range");
        }
        vda_name = rd32(verdef_data + vd_aux + 0, e);
        if (vda_name >= strtab_size) {
            fail("verdef name offset out of range");
        }
        vname = (const char *)(strtab_data + vda_name);
        if (strcmp(vname, "VERS_1") != 0) {
            fail("verdef name mismatch");
        }
    }

    elf_close(obj);
    as_data_program_free(&data);
    as_section_state_free(&secs);
    as_symtab_free(&syms);
    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);

    puts("ok");
    return 0;
}
