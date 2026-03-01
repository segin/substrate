#include "as_data.h"
#include "as_lexer.h"
#include "as_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static const as_data_op_t *find_op(const as_data_program_t *p, unsigned line, const char *directive) {
    size_t i;

    for (i = 0; i < p->count; ++i) {
        if (p->items[i].line == line && p->items[i].directive != NULL && strcmp(p->items[i].directive, directive) == 0) {
            return &p->items[i];
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    as_parse_result_t parsed;
    as_data_program_t prog;
    const as_data_op_t *op;
    char err[256];

    if (argc != 2) {
        fprintf(stderr, "usage: %s <input.s>\n", argv[0]);
        return 2;
    }

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.arch = AS_PARSER_ARCH_X86;

    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);
    as_data_program_init(&prog);

    if (as_lex_file(argv[1], &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("parse failed");
    }
    if (as_data_build(&parsed, &prog, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("data build failed");
    }

    if (prog.count != 18) {
        fail("unexpected data op count");
    }

    op = find_op(&prog, 1, ".byte");
    if (op == NULL || op->kind != AS_DATA_INT || op->u.ints.width != 1 || op->u.ints.count != 2 ||
        op->u.ints.values[0] != 1 || op->u.ints.values[1] != 2) {
        fail(".byte parse mismatch");
    }

    op = find_op(&prog, 7, ".8byte");
    if (op == NULL || op->kind != AS_DATA_INT || op->u.ints.width != 8 || op->u.ints.values[0] != 8) {
        fail(".8byte parse mismatch");
    }

    op = find_op(&prog, 8, ".float");
    if (op == NULL || op->kind != AS_DATA_FLOAT || op->u.floats.is_double != 0 || op->u.floats.values[0] != 1.5) {
        fail(".float parse mismatch");
    }

    op = find_op(&prog, 9, ".double");
    if (op == NULL || op->kind != AS_DATA_FLOAT || op->u.floats.is_double == 0 || op->u.floats.values[0] != 2.5) {
        fail(".double parse mismatch");
    }

    op = find_op(&prog, 10, ".ascii");
    if (op == NULL || op->kind != AS_DATA_STRING || op->u.str.nul_terminated || strcmp(op->u.str.bytes, "AB") != 0) {
        fail(".ascii parse mismatch");
    }

    op = find_op(&prog, 11, ".asciz");
    if (op == NULL || op->kind != AS_DATA_STRING || !op->u.str.nul_terminated || strcmp(op->u.str.bytes, "CD") != 0) {
        fail(".asciz parse mismatch");
    }

    op = find_op(&prog, 12, ".string");
    if (op == NULL || op->kind != AS_DATA_STRING || !op->u.str.nul_terminated || strcmp(op->u.str.bytes, "EF") != 0) {
        fail(".string parse mismatch");
    }

    op = find_op(&prog, 13, ".zero");
    if (op == NULL || op->kind != AS_DATA_ZERO || op->u.zero.count != 16) {
        fail(".zero parse mismatch");
    }

    op = find_op(&prog, 15, ".fill");
    if (op == NULL || op->kind != AS_DATA_FILL || op->u.fill.repeat != 3 || op->u.fill.size != 2 || op->u.fill.value != 0x41) {
        fail(".fill parse mismatch");
    }

    op = find_op(&prog, 17, ".org");
    if (op == NULL || op->kind != AS_DATA_ORG || op->u.org.offset != 128) {
        fail(".org parse mismatch");
    }

    op = find_op(&prog, 18, ".incbin");
    if (op == NULL || op->kind != AS_DATA_INCBIN || op->u.incbin.path == NULL || op->u.incbin.skip != 1 ||
        !op->u.incbin.has_count || op->u.incbin.count != 2) {
        fail(".incbin parse mismatch");
    }

    as_data_program_free(&prog);
    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);

    puts("ok");
    return 0;
}
