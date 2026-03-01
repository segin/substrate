#include "as_lexer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_token(const as_token_vec_t *v, as_token_kind_t kind, const char *text) {
    size_t i;
    if (v == NULL || text == NULL) {
        return 0;
    }
    for (i = 0; i < v->count; ++i) {
        if (v->items[i].kind == kind && strcmp(v->items[i].text, text) == 0) {
            return 1;
        }
    }
    return 0;
}

static void fail(const char *msg, const as_token_vec_t *v) {
    size_t i;
    fprintf(stderr, "FAIL: %s\n", msg);
    if (v != NULL) {
        for (i = 0; i < v->count; ++i) {
            fprintf(stderr, "  tok[%zu] kind=%d text='%s' @ %s:%u:%u\n", i,
                    (int)v->items[i].kind,
                    v->items[i].text != NULL ? v->items[i].text : "<null>",
                    v->items[i].file != NULL ? v->items[i].file : "<null>",
                    v->items[i].line,
                    v->items[i].col);
        }
    }
}

int main(int argc, char **argv) {
    as_lexer_cfg_t cfg;
    as_token_vec_t toks;
    char err[512];
    const char *inc_dirs[1];

    if (argc != 6) {
        fprintf(stderr, "usage: %s <att.s> <intel.s> <include.s> <preproc.s> <incdir>\n", argv[0]);
        return 2;
    }

    as_token_vec_init(&toks);
    memset(&cfg, 0, sizeof(cfg));
    cfg.intel_syntax = 0;
    cfg.max_include_depth = 16;

    if (as_lex_file(argv[1], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex att failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    if (!has_token(&toks, AS_TOK_LABEL, "start") ||
        !has_token(&toks, AS_TOK_MNEMONIC, "mov") ||
        !has_token(&toks, AS_TOK_IMMEDIATE, "$0x10") ||
        !has_token(&toks, AS_TOK_REGISTER, "%eax") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".ascii") ||
        !has_token(&toks, AS_TOK_STRING, "A\nBC")) {
        fail("AT&T tokenization failed", &toks);
        as_token_vec_free(&toks);
        return 1;
    }
    as_token_vec_free(&toks);

    as_token_vec_init(&toks);
    memset(&cfg, 0, sizeof(cfg));
    cfg.intel_syntax = 1;
    cfg.max_include_depth = 16;
    if (as_lex_file(argv[2], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex intel failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    if (!has_token(&toks, AS_TOK_MNEMONIC, "mov") ||
        !has_token(&toks, AS_TOK_REGISTER, "eax") ||
        !has_token(&toks, AS_TOK_IMMEDIATE, "42")) {
        fail("Intel tokenization failed", &toks);
        as_token_vec_free(&toks);
        return 1;
    }
    as_token_vec_free(&toks);

    as_token_vec_init(&toks);
    memset(&cfg, 0, sizeof(cfg));
    inc_dirs[0] = argv[5];
    cfg.include_dirs = inc_dirs;
    cfg.include_dir_count = 1;
    cfg.max_include_depth = 16;
    if (as_lex_file(argv[3], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex include failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    if (!has_token(&toks, AS_TOK_DIRECTIVE, ".include") ||
        !has_token(&toks, AS_TOK_LABEL, "inc_label") ||
        !has_token(&toks, AS_TOK_MNEMONIC, "add") ||
        !has_token(&toks, AS_TOK_LABEL, "main_label")) {
        fail("Include path expansion failed", &toks);
        as_token_vec_free(&toks);
        return 1;
    }
    as_token_vec_free(&toks);

    as_token_vec_init(&toks);
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_include_depth = 16;
    if (as_lex_file(argv[4], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex preproc failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    if (!has_token(&toks, AS_TOK_DIRECTIVE, ".if") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".ifdef") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".ifndef") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".else") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".endif") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".macro") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".endm") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".rept") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".endr") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".irp") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".irpc")) {
        fail("Preprocessor directive tokenization failed", &toks);
        as_token_vec_free(&toks);
        return 1;
    }
    as_token_vec_free(&toks);

    return 0;
}
