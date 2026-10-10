/*
 * as_relax.c, the pass that finds the branches of a source and says how
 * long each must be: held to the branches it finds, to its fixed point,
 * and to the distances the instructions themselves make -- a `nop` is
 * one byte on x86 and every ARM instruction is four, so a jump over 127
 * of the one is short and over 128 is not, whatever the pass supposes.
 *
 * Each check that fails is reported and the rest are still made.
 */
#include "as_lexer.h"
#include "as_parser.h"
#include "as_relax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static const char *dir;

static void fail(const char *what, const char *detail) {
    fprintf(stderr, "FAIL: %s: %s\n", what, detail);
    failures++;
}

/* A source file of the lines given, and its statements. */
static int parse_source(const char *text, as_parser_arch_t arch, as_parse_result_t *parsed) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    char path[1024];
    char err[256];
    FILE *f;

    snprintf(path, sizeof(path), "%s/relax.s", dir);
    f = fopen(path, "w");
    if (f == NULL || fputs(text, f) == EOF || fclose(f) != 0) {
        fail("the source cannot be written", path);
        return -1;
    }
    memset(&lcfg, 0, sizeof(lcfg));
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.arch = arch;
    as_token_vec_init(&toks);
    as_parse_result_init(parsed);
    if (as_lex_file(path, &lcfg, &toks, err, sizeof(err)) != 0 ||
        as_parse_tokens(&toks, &pcfg, parsed, err, sizeof(err)) != 0) {
        fail("the source is not read", err);
        as_token_vec_free(&toks);
        return -1;
    }
    as_token_vec_free(&toks);
    return 0;
}

/*
 * BRANCH to a label with COUNT lines of `nop` between: after the branch
 * if FORWARD, before it if not.
 */
static char *branch_over(const char *branch, int count, int forward) {
    size_t cap = (size_t)count * 5 + 128;
    char *text = malloc(cap);
    size_t at = 0;
    int i;

    if (text == NULL) {
        return NULL;
    }
    if (forward) {
        at += (size_t)snprintf(text + at, cap - at, "%s target\n", branch);
    } else {
        at += (size_t)snprintf(text + at, cap - at, "target:\n");
    }
    for (i = 0; i < count; i++) {
        at += (size_t)snprintf(text + at, cap - at, "nop\n");
    }
    if (forward) {
        at += (size_t)snprintf(text + at, cap - at, "target:\nnop\n");
    } else {
        at += (size_t)snprintf(text + at, cap - at, "%s target\nnop\n", branch);
    }
    return text;
}

/* The one branch of such a source, relaxed under CFG. */
static int relax_one(const char *branch, int count, int forward, const as_relax_cfg_t *cfg, as_relax_result_t *rr) {
    as_parse_result_t parsed;
    char err[256];
    char *text = branch_over(branch, count, forward);
    int rc = -1;

    as_relax_result_init(rr);
    if (text != NULL && parse_source(text, cfg->arch, &parsed) == 0) {
        if (as_relax_branches(&parsed, cfg, rr, err, sizeof(err)) != 0) {
            fail("the pass fails", err);
        } else if (rr->branch_count != 1) {
            fail("one branch is written and not one is found", branch);
        } else {
            rc = 0;
        }
        as_parse_result_free(&parsed);
    }
    free(text);
    return rc;
}

/* An x86 branch over COUNT one-byte instructions is KIND, DISP away. */
static void x86_is(const char *branch, int count, int forward, as_branch_kind_t kind, long disp) {
    as_relax_cfg_t cfg;
    as_relax_result_t rr;
    char what[128];

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_X86;
    snprintf(what, sizeof(what), "%s %s over %d bytes", branch, forward ? "forward" : "back", count);
    if (relax_one(branch, count, forward, &cfg, &rr) == 0) {
        if (rr.branches[0].kind != kind) {
            fail(what, kind == AS_BRANCH_KIND_SHORT ? "is not short, and the label is in reach"
                                                    : "is not long, and the label is out of reach");
        } else if (rr.branches[0].displacement != disp) {
            char detail[96];

            snprintf(detail, sizeof(detail), "the displacement is %ld and not %ld", rr.branches[0].displacement, disp);
            fail(what, detail);
        }
        if (!rr.stabilized) {
            fail(what, "the sizes did not settle");
        }
    }
    as_relax_result_free(&rr);
}

/* An ARM branch over COUNT instructions is in a range of RANGE, or not. */
static void arm_is(int count, int forward, long range, int out_of_range, long disp) {
    as_relax_cfg_t cfg;
    as_relax_result_t rr;
    char what[128];

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_ARM;
    cfg.arm_branch_abs_range = range;
    snprintf(what, sizeof(what), "ARM b %s over %d instructions, range %ld", forward ? "forward" : "back", count,
             range);
    if (relax_one("b", count, forward, &cfg, &rr) == 0) {
        if (rr.branches[0].displacement != disp) {
            char detail[96];

            snprintf(detail, sizeof(detail), "the displacement is %ld and not %ld", rr.branches[0].displacement, disp);
            fail(what, detail);
        }
        if ((rr.branches[0].out_of_range != 0) != out_of_range) {
            fail(what, out_of_range ? "is taken to be in range" : "is taken to be out of range");
        }
        if ((rr.branches[0].veneer_needed != 0) != out_of_range) {
            fail(what, out_of_range ? "is given no veneer" : "is given a veneer");
        }
    }
    as_relax_result_free(&rr);
}

/* Which statements are branches, and what each is recorded as. */
static void check_collection(void) {
    static const char text[] =
        "start:\n"
        "jmp first\n"
        "je second\n"
        "nop\n"
        "call start\n"
        "first:\n"
        "jne start\n"
        "second:\n"
        "ret\n";
    static const char *const mnemonic[3] = { "jmp", "je", "jne" };
    static const char *const target[3] = { "first", "second", "start" };
    as_parse_result_t parsed;
    as_relax_cfg_t cfg;
    as_relax_result_t rr;
    char err[256];
    size_t i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_X86;
    if (parse_source(text, AS_PARSER_ARCH_X86, &parsed) != 0) {
        return;
    }
    as_relax_result_init(&rr);
    if (as_relax_branches(&parsed, &cfg, &rr, err, sizeof(err)) != 0) {
        fail("the pass fails on a source of three branches", err);
    } else if (rr.branch_count != 3) {
        fail("three jumps, a nop, a call and a ret", "not three branches are found");
    } else {
        for (i = 0; i < 3; i++) {
            const as_relax_branch_t *b = &rr.branches[i];

            if (b->mnemonic == NULL || strcmp(b->mnemonic, mnemonic[i]) != 0) {
                fail("a branch is recorded under another's mnemonic", mnemonic[i]);
            }
            if (b->target_name == NULL || strcmp(b->target_name, target[i]) != 0) {
                fail("a branch is recorded with another's label", target[i]);
            }
            if (b->stmt_index >= parsed.count || parsed.items[b->stmt_index].kind != AS_STMT_INSTRUCTION ||
                strcmp(parsed.items[b->stmt_index].u.instr.mnemonic, mnemonic[i]) != 0) {
                fail("a branch does not point at its statement", mnemonic[i]);
            }
            if (b->kind != AS_BRANCH_KIND_SHORT) {
                fail("a branch of a few bytes is not short", mnemonic[i]);
            }
        }
        if (rr.passes != 1 || !rr.stabilized) {
            fail("nothing grew", "and the sizes are not settled in one pass");
        }
    }
    as_relax_result_free(&rr);
    as_parse_result_free(&parsed);
}

/* What the pass does when there is nothing to settle, or no leave to. */
static void check_passes(void) {
    as_parse_result_t parsed;
    as_relax_cfg_t cfg;
    as_relax_result_t rr;
    char err[256];
    char *text;

    memset(&cfg, 0, sizeof(cfg));
    cfg.arch = AS_PARSER_ARCH_X86;

    /* A label that is nowhere: the branch is kept, short, and settled. */
    as_relax_result_init(&rr);
    if (parse_source("jmp nowhere\nnop\n", AS_PARSER_ARCH_X86, &parsed) == 0) {
        if (as_relax_branches(&parsed, &cfg, &rr, err, sizeof(err)) != 0 || rr.branch_count != 1) {
            fail("a branch to a label that is not defined", "is not kept as a branch");
        } else if (rr.branches[0].kind != AS_BRANCH_KIND_SHORT || !rr.stabilized || rr.passes != 1) {
            fail("a branch to a label that is not defined", "is not left as it was, in one pass");
        }
        as_parse_result_free(&parsed);
    }
    as_relax_result_free(&rr);

    /* A branch that must grow takes a second pass, to see that it settled. */
    text = branch_over("jmp", 300, 1);
    as_relax_result_init(&rr);
    if (text != NULL && parse_source(text, AS_PARSER_ARCH_X86, &parsed) == 0) {
        if (as_relax_branches(&parsed, &cfg, &rr, err, sizeof(err)) != 0 || rr.branch_count != 1) {
            fail("a jump over 300 bytes", "is not relaxed");
        } else if (rr.passes != 2 || !rr.stabilized || rr.branches[0].kind != AS_BRANCH_KIND_NEAR) {
            fail("a jump over 300 bytes", "is not long and settled after two passes");
        }
        as_relax_result_free(&rr);

        /* Allowed one pass, it has grown and is not known to be settled. */
        cfg.max_passes = 1;
        as_relax_result_init(&rr);
        if (as_relax_branches(&parsed, &cfg, &rr, err, sizeof(err)) != 0 || rr.branch_count != 1) {
            fail("a jump over 300 bytes, in one pass", "is not relaxed");
        } else if (rr.passes != 1 || rr.stabilized) {
            fail("a jump over 300 bytes, in one pass", "is called settled");
        }
        cfg.max_passes = 0;

        /* Nothing to read, nothing to read it by, nowhere to put it. */
        if (as_relax_branches(NULL, &cfg, &rr, err, sizeof(err)) == 0 ||
            as_relax_branches(&parsed, NULL, &rr, err, sizeof(err)) == 0 ||
            as_relax_branches(&parsed, &cfg, NULL, err, sizeof(err)) == 0) {
            fail("a missing argument", "is accepted");
        }
        as_parse_result_free(&parsed);
    }
    as_relax_result_free(&rr);
    free(text);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s directory\n", argv[0]);
        return 2;
    }
    dir = argv[1];

    check_collection();
    check_passes();

    /*
     * x86.  The displacement is from the end of the branch: forward it
     * is the bytes crossed, back it is those and the branch's own -- two
     * short, five long for jmp and six for a conditional jump.
     */
    x86_is("jmp", 0, 1, AS_BRANCH_KIND_SHORT, 0);
    x86_is("jmp", 100, 1, AS_BRANCH_KIND_SHORT, 100);
    x86_is("jmp", 127, 1, AS_BRANCH_KIND_SHORT, 127);
    x86_is("jmp", 128, 1, AS_BRANCH_KIND_NEAR, 128);
    x86_is("je", 127, 1, AS_BRANCH_KIND_SHORT, 127);
    x86_is("je", 128, 1, AS_BRANCH_KIND_NEAR, 128);
    x86_is("jmp", 0, 0, AS_BRANCH_KIND_SHORT, -2);
    x86_is("jmp", 126, 0, AS_BRANCH_KIND_SHORT, -128);
    x86_is("jmp", 127, 0, AS_BRANCH_KIND_NEAR, -132);
    x86_is("jne", 126, 0, AS_BRANCH_KIND_SHORT, -128);
    x86_is("jne", 127, 0, AS_BRANCH_KIND_NEAR, -133);

    /*
     * ARM.  Every instruction is four bytes, the branch too; within the
     * range allowed it needs no veneer, and one instruction past it does.
     */
    arm_is(4, 1, 16, 0, 16);
    arm_is(5, 1, 16, 1, 20);
    arm_is(3, 0, 16, 0, -16);
    arm_is(4, 0, 16, 1, -20);

    if (failures == 0) {
        puts("ok");
    }
    return failures == 0 ? 0 : 1;
}
