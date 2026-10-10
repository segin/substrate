#include "as_lexer.h"
#include "as_parser.h"
#include "as_symtab.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static as_symbol_t *find_sym(as_symtab_t *tab, const char *name) {
    size_t i;

    for (i = 0; i < tab->count; ++i) {
        if (strcmp(tab->items[i].name, name) == 0) {
            return &tab->items[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    as_parse_result_t parsed;
    as_symtab_t tab;
    char err[256];
    as_symbol_t *s;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <input.s>\n", argv[0]);
        return 2;
    }

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.arch = AS_PARSER_ARCH_X86;

    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);
    as_symtab_init(&tab);

    if (as_lex_file(argv[1], &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("parse failed");
    }
    if (as_symtab_build(&parsed, &tab, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("symtab build failed");
    }

    s = find_sym(&tab, "gsym");
    if (s == NULL || s->bind != AS_SYM_BIND_GLOBAL || s->type != AS_SYM_TYPE_FUNCTION || s->size != 64 ||
        s->visibility != AS_SYM_VIS_HIDDEN || s->version == NULL || strcmp(s->version, "gsym@@VERS_1") != 0) {
        fail("gsym attributes mismatch");
    }

    s = find_sym(&tab, "wsym");
    if (s == NULL || s->bind != AS_SYM_BIND_WEAK || s->visibility != AS_SYM_VIS_PROTECTED || !s->defined) {
        fail("wsym attributes mismatch");
    }

    s = find_sym(&tab, "lsym");
    if (s == NULL || s->bind != AS_SYM_BIND_LOCAL || !s->defined) {
        fail("lsym attributes mismatch");
    }

    s = find_sym(&tab, "isym");
    if (s == NULL || s->visibility != AS_SYM_VIS_INTERNAL || !s->defined) {
        fail("isym attributes mismatch");
    }

    s = find_sym(&tab, "csym");
    if (s == NULL || !s->is_common || s->type != AS_SYM_TYPE_COMMON || s->bind != AS_SYM_BIND_GLOBAL ||
        s->common_size != 16 || s->common_align != 4) {
        fail("csym common symbol mismatch");
    }

    s = find_sym(&tab, "lcsym");
    if (s == NULL || !s->is_common || s->bind != AS_SYM_BIND_LOCAL || s->common_size != 8 || s->common_align != 2) {
        fail("lcsym local common symbol mismatch");
    }

    s = find_sym(&tab, "fsym");
    if (s == NULL || !s->defined || s->forward_ref_count == 0 || s->unresolved) {
        fail("forward reference resolution mismatch");
    }

    s = find_sym(&tab, "ext_missing");
    if (s == NULL || s->reference_count == 0 || !s->unresolved) {
        fail("unresolved symbol tracking mismatch");
    }

    as_symtab_free(&tab);
    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);

    puts("ok");
    return 0;
}
