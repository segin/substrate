#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"
#include "ast.h"

// Parse input from lexer into an AST
ast_node_t *parser_parse(lexer_t *l);
char *ast_to_string(ast_node_t *node);

#endif
