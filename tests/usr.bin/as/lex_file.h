#ifndef SUBSTRATE_TESTS_AS_LEX_FILE_H
#define SUBSTRATE_TESTS_AS_LEX_FILE_H

/*
 * The tokens of a file, for the unit tests that begin from one.
 *
 * The lexer had a function for this, as_lex_file(), that the assembler
 * itself never called -- it reads its sources, expands them and hands
 * the text to as_lex_text() -- so it was the tests' alone, and is here
 * with them: the file read, and its text handed over as the assembler
 * hands it.
 */

#include "as_lexer.h"

#include <stdio.h>
#include <stdlib.h>

static int test_lex_file(const char *path, const as_lexer_cfg_t *cfg, as_token_vec_t *out, char *errbuf,
                         size_t errbuf_sz) {
    FILE *fp = fopen(path, "rb");
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    int rc;

    if (fp == NULL) {
        snprintf(errbuf, errbuf_sz, "%s: cannot be opened", path);
        return -1;
    }
    for (;;) {
        size_t n;

        if (len + 4096 > cap) {
            char *next = (char *)realloc(text, cap + 65536);

            if (next == NULL) {
                free(text);
                fclose(fp);
                snprintf(errbuf, errbuf_sz, "%s: out of memory", path);
                return -1;
            }
            text = next;
            cap += 65536;
        }
        n = fread(text + len, 1, 4096, fp);
        len += n;
        if (n < 4096) {
            break;
        }
    }
    fclose(fp);
    rc = as_lex_text(path, text, len, 0, cfg, out, errbuf, errbuf_sz);
    free(text);
    return rc;
}

#endif
