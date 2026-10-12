#include "as_lexer.h"
#include "lex_file.h"

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

/*
 * The tokens of one statement, written as a letter for the kind, a
 * colon and the text, a space between two and an underscore before one
 * that had white space before it:
 *
 *   L label  D directive  M mnemonic  R register  N number
 *   I identifier  S string  O operator  P punctuation
 */
static int statement_is(const char *src, int intel, const char *want) {
    static const char letters[] = "?MRNLDSIOP";
    as_lexer_cfg_t cfg;
    as_token_vec_t toks;
    char err[256];
    char got[1024];
    size_t at = 0;
    size_t i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.intel_syntax = intel;
    as_token_vec_init(&toks);
    if (as_lex_text("t.s", src, strlen(src), 0, &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "FAIL: [%s] is not tokenized: %s\n", src, err);
        as_token_vec_free(&toks);
        return 1;
    }
    got[0] = '\0';
    for (i = 0; i < toks.count && at + strlen(toks.items[i].text) + 8 < sizeof(got); ++i) {
        at += (size_t)snprintf(got + at, sizeof(got) - at, "%s%s%c:%s", i != 0 ? " " : "",
                               toks.items[i].space_before ? "_" : "", letters[toks.items[i].kind],
                               toks.items[i].text);
    }
    as_token_vec_free(&toks);
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL: [%s]\n   is %s\n  not %s\n", src, got, want);
        return 1;
    }
    return 0;
}

/* A token for each operator, punctuation mark, register, number, name
 * and string of a statement. */
static int every_token(void) {
    int bad = 0;

    /* The six of the requirement. */
    bad |= statement_is(".long a+b", 0, "D:.long _I:a O:+ I:b");
    bad |= statement_is("movl %fs:0, %eax", 0, "M:movl _R:%fs P:: N:0 P:, _R:%eax");
    bad |= statement_is("x=5", 0, "I:x P:= N:5");
    bad |= statement_is("jmp *%eax", 0, "M:jmp _O:* R:%eax");
    bad |= statement_is("pushl $sym", 0, "M:pushl _P:$ I:sym");
    bad |= statement_is("movl %gs : 8, %eax", 0, "M:movl _R:%gs _P:: _N:8 P:, _R:%eax");

    /* Labels: a name and its colon, however many, and the colon gone. */
    bad |= statement_is("a: b:c: nop", 0, "L:a _L:b L:c _M:nop");
    bad |= statement_is("1: jmp 1b", 0, "L:1 _M:jmp _N:1b");
    bad |= statement_is("\"x y\": ret", 0, "L:x y _M:ret");
    bad |= statement_is("foo : nop", 0, "L:foo _M:nop");
    bad |= statement_is(".L1:", 0, "L:.L1");

    /* The head: a directive, an instruction, an assignment. */
    bad |= statement_is(".Lq = 5", 0, "I:.Lq _P:= _N:5");
    bad |= statement_is(". = . + 4", 0, "I:. _P:= _I:. _O:+ _N:4");
    bad |= statement_is("lock cmpxchgl %ebx, (%eax)", 0, "M:lock _I:cmpxchgl _R:%ebx P:, _P:( R:%eax P:)");
    bad |= statement_is("xcrypt-ecb", 0, "M:xcrypt-ecb");
    bad |= statement_is("{vex} vaddps %xmm0,%xmm1,%xmm2", 0,
                        "P:{ I:vex P:} _I:vaddps _R:%xmm0 P:, R:%xmm1 P:, R:%xmm2");

    /* Numbers: one token, whatever is in them. */
    bad |= statement_is(".long 0x1F, 0b101, 017, 12abc", 0, "D:.long _N:0x1F P:, _N:0b101 P:, _N:017 P:, _N:12abc");
    bad |= statement_is(".double 1.5e-3, 2.5, 1e10", 0, "D:.double _N:1.5e-3 P:, _N:2.5 P:, _N:1e10");
    bad |= statement_is(".long 0x1e+3", 0, "D:.long _N:0x1e O:+ N:3");
    bad |= statement_is(".byte 'a, 'b', '\\n", 0, "D:.byte _N:97 P:, _N:98 P:, _N:10");
    bad |= statement_is(".long 1b-.", 0, "D:.long _N:1b O:- I:.");

    /* Operators of two characters, and of one. */
    bad |= statement_is(".long a<<2|b>>1", 0, "D:.long _I:a O:<< N:2 O:| I:b O:>> N:1");
    bad |= statement_is(".long a==b, a!=b, a<>b, a<=b, a>=b, a&&b, a||b", 0,
                        "D:.long _I:a O:== I:b P:, _I:a O:!= I:b P:, _I:a O:<> I:b P:, _I:a O:<= I:b P:, "
                        "_I:a O:>= I:b P:, _I:a O:&& I:b P:, _I:a O:|| I:b");
    bad |= statement_is(".long ~a^-b/c", 0, "D:.long _O:~ I:a O:^ O:- I:b O:/ I:c");

    /* A percent sign: a register, or the remainder. */
    bad |= statement_is(".long a%b", 0, "D:.long _I:a O:% I:b");
    bad |= statement_is(".long a % b", 0, "D:.long _I:a _O:% _I:b");
    bad |= statement_is("movl %nosuch, %eax", 0, "M:movl _I:%nosuch P:, _R:%eax");
    bad |= statement_is("fadd %st(1), %st", 0, "M:fadd _R:%st P:( N:1 P:) P:, _R:%st");

    /* Operands. */
    bad |= statement_is("movl foo@GOT(%ebx), %eax", 0, "M:movl _I:foo@GOT P:( R:%ebx P:) P:, _R:%eax");
    bad |= statement_is("movl $(x+1)*2, 8(%eax,%ebx,4)", 0,
                        "M:movl _P:$ P:( I:x O:+ N:1 P:) O:* N:2 P:, _N:8 P:( R:%eax P:, R:%ebx P:, N:4 P:)");
    bad |= statement_is("vaddps %zmm0, %zmm1, %zmm2{%k1}{z}", 0,
                        "M:vaddps _R:%zmm0 P:, _R:%zmm1 P:, _R:%zmm2 P:{ R:%k1 P:} P:{ I:z P:}");
    bad |= statement_is("jmp \"x.y\"", 0, "M:jmp _I:x.y");
    bad |= statement_is(".section .note.GNU-stack,\"\",@progbits", 0,
                        "D:.section _I:.note.GNU O:- I:stack P:, S: P:, I:@progbits");
    bad |= statement_is(".ascii \"a\\\"b\" \"c\"", 0, "D:.ascii _S:a\\\"b _S:c");

    /* Intel syntax: registers are written bare. */
    bad |= statement_is("mov eax, dword ptr [ebx+ecx*4+8]", 1,
                        "M:mov _R:eax P:, _I:dword _I:ptr _P:[ R:ebx O:+ R:ecx O:* N:4 O:+ N:8 P:]");
    bad |= statement_is("mov eax, fs:[ebx]", 1, "M:mov _R:eax P:, _R:fs P:: P:[ R:ebx P:]");
    return bad;
}

int main(int argc, char **argv) {
    as_lexer_cfg_t cfg;
    as_token_vec_t toks;
    char err[512];
    const char *inc_dirs[1];

    if (every_token() != 0) {
        return 1;
    }

    if (argc != 6) {
        fprintf(stderr, "usage: %s <att.s> <intel.s> <include.s> <preproc.s> <incdir>\n", argv[0]);
        return 2;
    }

    as_token_vec_init(&toks);
    memset(&cfg, 0, sizeof(cfg));
    cfg.intel_syntax = 0;
    cfg.max_include_depth = 16;

    if (test_lex_file(argv[1], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex att failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    if (!has_token(&toks, AS_TOK_LABEL, "start") ||
        !has_token(&toks, AS_TOK_MNEMONIC, "mov") ||
        !has_token(&toks, AS_TOK_PUNCT, "$") ||
        !has_token(&toks, AS_TOK_NUMBER, "0x10") ||
        !has_token(&toks, AS_TOK_REGISTER, "%eax") ||
        !has_token(&toks, AS_TOK_DIRECTIVE, ".ascii") ||
        /* A string token is the text between the quotes as written; the
         * escapes are decoded where the string is used
         * (as_decode_string_literal). */
        !has_token(&toks, AS_TOK_STRING, "A\\nB\\x43")) {
        fail("AT&T tokenization failed", &toks);
        as_token_vec_free(&toks);
        return 1;
    }
    as_token_vec_free(&toks);

    as_token_vec_init(&toks);
    memset(&cfg, 0, sizeof(cfg));
    cfg.intel_syntax = 1;
    cfg.max_include_depth = 16;
    if (test_lex_file(argv[2], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex intel failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    if (!has_token(&toks, AS_TOK_MNEMONIC, "mov") ||
        !has_token(&toks, AS_TOK_REGISTER, "eax") ||
        !has_token(&toks, AS_TOK_NUMBER, "42")) {
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
    if (test_lex_file(argv[3], &cfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "lex include failed: %s\n", err);
        as_token_vec_free(&toks);
        return 1;
    }
    /* The file's lines stand in the directive's place: its tokens are
     * there and the directive is not.  (This asked for the directive to
     * be there, which was all that ever was.) */
    if (has_token(&toks, AS_TOK_DIRECTIVE, ".include") ||
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
    if (test_lex_file(argv[4], &cfg, &toks, err, sizeof(err)) != 0) {
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
