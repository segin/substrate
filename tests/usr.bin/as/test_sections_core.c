#include "as_lexer.h"
#include "as_parser.h"
#include "as_sections.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SHF_WRITE
#define SHF_WRITE 0x1
#endif
#ifndef SHF_ALLOC
#define SHF_ALLOC 0x2
#endif
#ifndef SHF_EXECINSTR
#define SHF_EXECINSTR 0x4
#endif
#ifndef SHF_GROUP
#define SHF_GROUP 0x200
#endif
#ifndef SHT_PROGBITS
#define SHT_PROGBITS 1
#endif
#ifndef SHT_NOBITS
#define SHT_NOBITS 8
#endif

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

int main(int argc, char **argv) {
    as_lexer_cfg_t lcfg;
    as_parser_cfg_t pcfg;
    as_token_vec_t toks;
    as_parse_result_t parsed;
    as_section_state_t secs;
    const as_section_t *s;
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
    as_section_state_init(&secs);

    if (as_lex_file(argv[1], &lcfg, &toks, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("lex failed");
    }
    if (as_parse_tokens(&toks, &pcfg, &parsed, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("parse failed");
    }
    if (as_sections_build(&parsed, &secs, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        fail("sections build failed");
    }

    s = as_sections_find(&secs, ".text", 0);
    if (s == NULL || s->type != SHT_PROGBITS || (s->flags & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR)) {
        fail("missing built-in .text");
    }

    s = as_sections_find(&secs, ".text", 1);
    if (s == NULL || s->subsection != 1) {
        fail("subsection .text,1 missing");
    }

    s = as_sections_find(&secs, ".foo", 0);
    if (s == NULL || (s->flags & (SHF_ALLOC | SHF_EXECINSTR)) != (SHF_ALLOC | SHF_EXECINSTR) ||
        (s->flags & SHF_GROUP) == 0 || s->type != SHT_PROGBITS || s->align != 32 || !s->comdat ||
        s->group == NULL || strcmp(s->group, "grp1") != 0) {
        fail(".foo section metadata mismatch");
    }

    s = as_sections_find(&secs, ".bar", 0);
    if (s == NULL || (s->flags & (SHF_ALLOC | SHF_WRITE)) != (SHF_ALLOC | SHF_WRITE) || s->type != SHT_NOBITS || s->align != 16) {
        fail(".bar section metadata mismatch");
    }

    s = as_sections_find(&secs, ".data", 0);
    if (s == NULL || s->align != 8) {
        fail(".data align directive missing");
    }

    if (secs.current_index >= secs.count || strcmp(secs.items[secs.current_index].name, ".bss") != 0) {
        fail("final section tracking mismatch");
    }

    as_section_state_free(&secs);
    as_parse_result_free(&parsed);
    as_token_vec_free(&toks);

    puts("ok");
    return 0;
}
