#ifndef LEX_CODEGEN_H
#define LEX_CODEGEN_H

#include "dfa.h"

/* Generate lex.yy.c scanner */
void generate_scanner(struct dfa *d, const char *def_code, const char *sub_code, int to_stdout);

#endif
