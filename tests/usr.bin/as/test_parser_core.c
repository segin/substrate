#include "as_lexer.h"
#include "as_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static as_stmt_t *find_instr(as_parse_result_t *r, unsigned line, const char *mnemonic) {
    size_t i;

    for (i = 0; i < r->count; ++i) {
        as_stmt_t *st = &r->items[i];
        if (st->kind == AS_STMT_INSTRUCTION && st->line == line && strcmp(st->u.instr.mnemonic, mnemonic) == 0) {
            return st;
        }
    }
    return NULL;
}

static int expr_contains_symbol(const as_expr_t *e, const char *sym) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL && strcmp(e->symbol, sym) == 0) {
        return 1;
    }
    return expr_contains_symbol(e->lhs, sym) || expr_contains_symbol(e->rhs, sym);
}

static void verify_x86(const char *path) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    as_parse_result_t parsed;
    char err[256];
    as_stmt_t *st;

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.arch = AS_PARSER_ARCH_X86;

    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);

    if (as_lex_file(path, &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("x86 lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("x86 parse failed");
    }

    st = find_instr(&parsed, 1, "mov");
    if (st == NULL) {
        fail("missing prefixed x86 instruction");
    }
    if ((st->u.instr.prefixes & AS_PREFIX_LOCK) == 0 || (st->u.instr.prefixes & AS_PREFIX_REPNE) == 0 ||
        (st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) == 0 || (st->u.instr.prefixes & AS_PREFIX_REX) == 0) {
        fail("x86 prefixes were not parsed");
    }
    if (st->u.instr.operand_count != 2 || st->u.instr.operands[0].kind != AS_OPERAND_MEMORY ||
        st->u.instr.operands[1].kind != AS_OPERAND_REGISTER) {
        fail("x86 operand typing failed");
    }
    if (st->u.instr.operands[0].u.mem.base_reg == NULL || strcmp(st->u.instr.operands[0].u.mem.base_reg, "ebx") != 0 ||
        st->u.instr.operands[0].u.mem.index_reg == NULL || strcmp(st->u.instr.operands[0].u.mem.index_reg, "ecx") != 0 ||
        st->u.instr.operands[0].u.mem.scale != 4 || st->u.instr.operands[0].u.mem.disp == NULL ||
        st->u.instr.operands[0].u.mem.disp->kind != AS_EXPR_CONST || st->u.instr.operands[0].u.mem.disp->value != 8) {
        fail("x86 memory parse failed");
    }

    st = find_instr(&parsed, 2, "mov");
    if (st == NULL || st->u.instr.operand_count < 1 || st->u.instr.operands[0].kind != AS_OPERAND_IMMEDIATE ||
        st->u.instr.operands[0].u.expr == NULL || st->u.instr.operands[0].u.expr->kind != AS_EXPR_BINARY) {
        fail("x86 expression parse failed");
    }

    st = find_instr(&parsed, 3, "jmp");
    if (st == NULL || st->u.instr.operand_count != 1 || st->u.instr.operands[0].kind != AS_OPERAND_LABEL_REF ||
        st->u.instr.operands[0].u.expr == NULL || st->u.instr.operands[0].u.expr->kind != AS_EXPR_LOCAL_REF ||
        !st->u.instr.operands[0].u.expr->local_forward || !st->u.instr.operands[0].u.expr->local_resolved ||
        st->u.instr.operands[0].u.expr->local_target_line != 5) {
        fail("local forward label resolution failed");
    }

    st = find_instr(&parsed, 4, "mov");
    if (st == NULL || st->u.instr.operand_count < 1 || st->u.instr.operands[0].kind != AS_OPERAND_LABEL_REF ||
        st->u.instr.operands[0].u.expr == NULL || st->u.instr.operands[0].u.expr->kind != AS_EXPR_SYMBOL ||
        st->u.instr.operands[0].u.expr->symbol == NULL || strcmp(st->u.instr.operands[0].u.expr->symbol, "target") != 0) {
        fail("symbol label reference parse failed");
    }

    st = find_instr(&parsed, 6, "mov");
    if (st == NULL || st->u.instr.operand_count < 1 || st->u.instr.operands[0].kind != AS_OPERAND_LABEL_REF ||
        st->u.instr.operands[0].u.expr == NULL || st->u.instr.operands[0].u.expr->kind != AS_EXPR_LOCAL_REF ||
        st->u.instr.operands[0].u.expr->local_forward || !st->u.instr.operands[0].u.expr->local_resolved ||
        st->u.instr.operands[0].u.expr->local_target_line != 5) {
        fail("local backward label resolution failed");
    }

    st = find_instr(&parsed, 8, "mov");
    if (st == NULL || st->u.instr.operand_count < 1 || st->u.instr.operands[0].kind != AS_OPERAND_IMMEDIATE ||
        !expr_contains_symbol(st->u.instr.operands[0].u.expr, "target")) {
        fail("symbol-in-expression parse failed");
    }

    st = find_instr(&parsed, 10, "mov");
    if (st == NULL || st->u.instr.syntax_intel == 0 || st->u.instr.operand_count != 2 ||
        st->u.instr.operands[0].kind != AS_OPERAND_REGISTER || st->u.instr.operands[1].kind != AS_OPERAND_MEMORY ||
        st->u.instr.operands[1].u.mem.base_reg == NULL || strcmp(st->u.instr.operands[1].u.mem.base_reg, "ebx") != 0 ||
        st->u.instr.operands[1].u.mem.size_bits != 32 ||
        st->u.instr.operands[1].u.mem.disp == NULL || st->u.instr.operands[1].u.mem.disp->kind != AS_EXPR_CONST ||
        st->u.instr.operands[1].u.mem.disp->value != 8) {
        fail("intel size-qualifier memory parse failed");
    }

    st = find_instr(&parsed, 11, "mov");
    if (st == NULL || st->u.instr.syntax_intel == 0 || st->u.instr.operand_count != 2 ||
        st->u.instr.operands[1].kind != AS_OPERAND_MEMORY ||
        st->u.instr.operands[1].u.mem.base_reg == NULL || strcmp(st->u.instr.operands[1].u.mem.base_reg, "ebx") != 0 ||
        st->u.instr.operands[1].u.mem.index_reg == NULL || strcmp(st->u.instr.operands[1].u.mem.index_reg, "esi") != 0 ||
        st->u.instr.operands[1].u.mem.scale != 4 || st->u.instr.operands[1].u.mem.disp == NULL ||
        st->u.instr.operands[1].u.mem.disp->kind != AS_EXPR_CONST ||
        st->u.instr.operands[1].u.mem.disp->value != 32) {
        fail("intel base/index/scale/disp parse failed");
    }

    st = find_instr(&parsed, 12, "mov");
    if (st == NULL || st->u.instr.syntax_intel == 0 || st->u.instr.operand_count != 2 ||
        st->u.instr.operands[1].kind != AS_OPERAND_MEMORY ||
        st->u.instr.operands[1].u.mem.segment_reg == NULL ||
        strcmp(st->u.instr.operands[1].u.mem.segment_reg, "gs") != 0 ||
        st->u.instr.operands[1].u.mem.base_reg == NULL || strcmp(st->u.instr.operands[1].u.mem.base_reg, "ebx") != 0 ||
        st->u.instr.operands[1].u.mem.disp == NULL || st->u.instr.operands[1].u.mem.disp->kind != AS_EXPR_CONST ||
        st->u.instr.operands[1].u.mem.disp->value != 4) {
        fail("intel segment-override memory parse failed");
    }

    st = find_instr(&parsed, 14, "mov");
    if (st == NULL || st->u.instr.syntax_intel != 0 || st->u.instr.operand_count != 2 ||
        st->u.instr.operands[0].kind != AS_OPERAND_MEMORY ||
        st->u.instr.operands[0].u.mem.segment_reg == NULL ||
        strcmp(st->u.instr.operands[0].u.mem.segment_reg, "gs") != 0 ||
        st->u.instr.operands[0].u.mem.base_reg == NULL || strcmp(st->u.instr.operands[0].u.mem.base_reg, "ebx") != 0 ||
        st->u.instr.operands[0].u.mem.disp == NULL || st->u.instr.operands[0].u.mem.disp->kind != AS_EXPR_CONST ||
        st->u.instr.operands[0].u.mem.disp->value != 4) {
        fail("att segment-override memory parse failed");
    }

    st = find_instr(&parsed, 16, "mov");
    if (st == NULL || st->u.instr.syntax_intel == 0 || st->u.instr.operand_count != 2 ||
        st->u.instr.operands[0].kind != AS_OPERAND_REGISTER || st->u.instr.operands[1].kind != AS_OPERAND_MEMORY ||
        st->u.instr.operands[1].u.mem.size_bits != 16 || st->u.instr.operands[1].u.mem.disp == NULL ||
        st->u.instr.operands[1].u.mem.disp->kind != AS_EXPR_CONST || st->u.instr.operands[1].u.mem.disp->value != 2) {
        fail("intel word ptr qualifier parse failed");
    }

    st = find_instr(&parsed, 17, "mov");
    if (st == NULL || st->u.instr.syntax_intel == 0 || st->u.instr.operand_count != 2 ||
        st->u.instr.operands[1].kind != AS_OPERAND_MEMORY || st->u.instr.operands[1].u.mem.size_bits != 64 ||
        st->u.instr.operands[1].u.mem.disp == NULL || st->u.instr.operands[1].u.mem.disp->kind != AS_EXPR_CONST ||
        st->u.instr.operands[1].u.mem.disp->value != 16) {
        fail("intel qword ptr qualifier parse failed");
    }

    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);
}

static void verify_arm(const char *path) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    as_parse_result_t parsed;
    char err[256];
    as_stmt_t *st;

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.arch = AS_PARSER_ARCH_ARM;

    as_token_vec_init(&toks);
    as_parse_result_init(&parsed);

    if (as_lex_file(path, &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("arm lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("arm parse failed");
    }

    st = find_instr(&parsed, 1, "add");
    if (st == NULL || st->u.instr.arm_condition == NULL || strcmp(st->u.instr.arm_condition, "eq") != 0 ||
        st->u.instr.operand_count != 3 || st->u.instr.operands[2].kind != AS_OPERAND_SHIFTED_REGISTER ||
        st->u.instr.operands[2].u.shifted.shift != AS_SHIFT_LSL || st->u.instr.operands[2].u.shifted.amount_reg == NULL ||
        strcmp(st->u.instr.operands[2].u.shifted.amount_reg, "r3") != 0) {
        fail("arm condition/shift parse failed");
    }

    st = find_instr(&parsed, 2, "stmia");
    if (st == NULL || st->u.instr.operand_count != 2 || st->u.instr.operands[1].kind != AS_OPERAND_REGISTER_LIST ||
        st->u.instr.operands[1].u.reg_list.count != 3) {
        fail("arm register-list parse failed");
    }

    st = find_instr(&parsed, 3, "mrc");
    if (st == NULL || st->u.instr.operand_count != 6 || st->u.instr.operands[0].kind != AS_OPERAND_COPROCESSOR ||
        st->u.instr.operands[3].kind != AS_OPERAND_COPROCESSOR) {
        fail("arm coprocessor parse failed");
    }

    st = find_instr(&parsed, 4, "b");
    if (st == NULL || st->u.instr.arm_condition == NULL || strcmp(st->u.instr.arm_condition, "ne") != 0 ||
        st->u.instr.operand_count != 1 || st->u.instr.operands[0].kind != AS_OPERAND_LABEL_REF) {
        fail("arm branch condition parse failed");
    }

    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s <x86.s> <arm.s>\n", argv[0]);
        return 2;
    }

    verify_x86(argv[1]);
    verify_arm(argv[2]);
    puts("ok");
    return 0;
}
