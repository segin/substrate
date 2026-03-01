#include "as_data.h"
#include "as_elf_emit.h"
#include "as_lexer.h"
#include "as_parser.h"
#include "as_sections.h"
#include "as_symtab.h"
#include "elfobj.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
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

    if (elf_type(obj) != ET_REL || elf_machine(obj) != EM_X86_64 || elf_class(obj) != ELFOBJ_CLASS_64) {
        fail("ELF class/type/machine mismatch");
    }

    if (elf_find_section(obj, ".symtab") == NULL || elf_find_section(obj, ".strtab") == NULL ||
        elf_find_section(obj, ".shstrtab") == NULL) {
        fail("missing core ELF tables");
    }

    if (elf_find_section(obj, ".note.GNU-stack") == NULL || elf_find_section(obj, ".note.gnu.property") == NULL) {
        fail("missing GNU note sections");
    }

    if (elf_find_section(obj, ".debug_custom") == NULL || elf_find_section(obj, ".debug_line") == NULL) {
        fail("missing debug sections");
    }

    if (elf_find_section(obj, ".eh_frame") == NULL || elf_find_section(obj, ".eh_frame_hdr") == NULL) {
        fail("missing CFI sections");
    }

    if (elf_reloc_count(obj) == 0) {
        fail("expected relocations");
    }

    {
        elf_symbol_t *main_sym = elf_find_symbol(obj, "main");
        if (main_sym == NULL || elf_symbol_bind(main_sym) != STB_GLOBAL || elf_symbol_type(main_sym) != STT_FUNC) {
            fail("main symbol mismatch");
        }
    }

    {
        size_t i;
        int found_ext = 0;
        for (i = 0; i < elf_reloc_count(obj); ++i) {
            elf_reloc_t *r = elf_reloc_at(obj, i);
            elf_symbol_t *s = elf_reloc_symbol(r);
            const char *nm = s != NULL ? elf_symbol_name(s) : NULL;
            if (nm != NULL && strcmp(nm, "extsym") == 0) {
                found_ext = 1;
                break;
            }
        }
        if (!found_ext) {
            fail("missing extsym relocation");
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
