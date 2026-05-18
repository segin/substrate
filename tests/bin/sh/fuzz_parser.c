#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../lexer.h"
#include "../parser.h"
#include "../shell_var.h"
#include "../ast.h"

int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0) return 0;

    char *buffer = malloc(Size + 1);
    memcpy(buffer, Data, Size);
    buffer[Size] = '\0';

    shell_var_init(NULL);

    lexer_t l;
    lexer_init(&l, buffer);

    while (1) {
        token_t *t = lexer_peek(&l);
        if (!t || t->type == TOKEN_EOF) break;
        
        ast_node_t *node = parser_parse(&l);
        if (node) {
            ast_free(node);
        } else {
            break;
        }
    }

    lexer_clear_lookahead(&l);
    shell_var_destroy();

    free(buffer);
    return 0;
}
