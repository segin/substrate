#include "../exec.h"
#include "../parser.h"
#include "../lexer.h"
#include "../shell_var.h"
#include "../job.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int shell_promptvars = 0;

// Mock dependencies
int execute_line(char *line) {
    // Dummy for eval/source dependencies
    (void)line;
    return 0;
}

void test_builtin_true(void) {
    lexer_t l;
    lexer_init(&l, "true");
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);

    int status = execute_ast(node);
    assert(status == 0);

    ast_free(node);
    printf("PASS: test_builtin_true\n");
}

void test_builtin_false(void) {
    lexer_t l;
    lexer_init(&l, "false");
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);

    int status = execute_ast(node);
    assert(status == 1);

    ast_free(node);
    printf("PASS: test_builtin_false\n");
}

void test_simple_assignment(void) {
    lexer_t l;
    lexer_init(&l, "TEST_VAR=hello");
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);

    int status = execute_ast(node);
    assert(status == 0);
    
    assert(strcmp(shell_var_get("TEST_VAR"), "hello") == 0);

    ast_free(node);
    printf("PASS: test_simple_assignment\n");
}

int main(void) {
    shell_var_init(NULL);
    job_init();
    
    test_builtin_true();
    test_builtin_false();
    test_simple_assignment();
    
    shell_var_destroy();
    return 0;
}
