#include "as_lexer.h"
#include "as_parser.h"
#include "as_relax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void load_parse(const char *path, as_parser_arch_t arch, as_parse_result_t *parsed) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    char err[256];

    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.arch = arch;

    as_token_vec_init(&toks);
    as_parse_result_init(parsed);

    if (as_lex_file(path, &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("parse failed");
    }

    as_token_vec_free(&toks);
}

int main(int argc, char **argv) {
    as_parse_result_t x86;
    as_parse_result_t arm;
    as_relax_cfg_t cfg;
    as_relax_result_t rr;
    char err[256];

    if (argc != 3) {
        fprintf(stderr, "usage: %s <x86.s> <arm.s>\n", argv[0]);
        return 2;
    }

    load_parse(argv[1], AS_PARSER_ARCH_X86, &x86);

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_X86;

    as_relax_result_init(&rr);
    if (as_relax_branches(&x86, &cfg, &rr, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("x86 relax failed");
    }
    if (rr.branch_count == 0 || rr.branches[0].kind != AS_BRANCH_KIND_NEAR || rr.passes < 2 || !rr.stabilized) {
        fail("x86 short->near relaxation failed");
    }
    as_relax_result_free(&rr);

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_X86;
    cfg.x86_near_min = -64;
    cfg.x86_near_max = 64;

    as_relax_result_init(&rr);
    if (as_relax_branches(&x86, &cfg, &rr, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("x86 far relax failed");
    }
    if (rr.branch_count == 0 || rr.branches[0].kind != AS_BRANCH_KIND_FAR || !rr.stabilized) {
        fail("x86 near->far promotion failed");
    }
    as_relax_result_free(&rr);

    load_parse(argv[2], AS_PARSER_ARCH_ARM, &arm);

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_ARM;
    cfg.arm_branch_abs_range = 16;

    as_relax_result_init(&rr);
    if (as_relax_branches(&arm, &cfg, &rr, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("arm relax failed");
    }
    if (rr.branch_count == 0 || !rr.branches[0].out_of_range || !rr.branches[0].veneer_needed) {
        fail("arm range/veneer handling failed");
    }
    as_relax_result_free(&rr);

    as_parse_result_free(&x86);
    as_parse_result_free(&arm);

    puts("ok");
    return 0;
}
