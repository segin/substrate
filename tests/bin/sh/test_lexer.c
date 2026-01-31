#include "../lexer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_simple(void) {
    lexer_t l;
    lexer_init(&l, "ls -la");
    
    token_t *t = lexer_next(&l);
    assert(t->type == TOKEN_WORD);
    assert(strcmp(t->value, "ls") == 0);
    token_free(t);
    
    t = lexer_next(&l);
    assert(t->type == TOKEN_WORD);
    assert(strcmp(t->value, "-la") == 0);
    token_free(t);
    
    t = lexer_next(&l);
    assert(t->type == TOKEN_EOF);
    token_free(t);
    printf("PASS: test_simple\n");
}

void test_quotes(void) {
    lexer_t l;
    lexer_init(&l, "echo \"hello world\" 'foo bar'");
    
    token_t *t = lexer_next(&l);
    assert(t->type == TOKEN_WORD);
    assert(strcmp(t->value, "echo") == 0);
    token_free(t);
    
    t = lexer_next(&l);
    assert(t->type == TOKEN_WORD);
    // Quotes kept by lexer logic
    assert(strcmp(t->value, "\"hello world\"") == 0);
    token_free(t);
    
    t = lexer_next(&l);
    assert(t->type == TOKEN_WORD);
    assert(strcmp(t->value, "'foo bar'") == 0);
    token_free(t);
    
    printf("PASS: test_quotes\n");
}

void test_operators(void) {
    lexer_t l;
    lexer_init(&l, "a && b || c >> d");
    
    token_t *t;
    
    t = lexer_next(&l); assert(strcmp(t->value, "a") == 0); token_free(t);
    t = lexer_next(&l); assert(t->type == TOKEN_OPERATOR); assert(strcmp(t->value, "&&") == 0); token_free(t);
    t = lexer_next(&l); assert(strcmp(t->value, "b") == 0); token_free(t);
    t = lexer_next(&l); assert(t->type == TOKEN_OPERATOR); assert(strcmp(t->value, "||") == 0); token_free(t);
    t = lexer_next(&l); assert(strcmp(t->value, "c") == 0); token_free(t);
    t = lexer_next(&l); assert(t->type == TOKEN_OPERATOR); assert(strcmp(t->value, ">>") == 0); token_free(t);
    t = lexer_next(&l); assert(strcmp(t->value, "d") == 0); token_free(t);
    
    printf("PASS: test_operators\n");
}

void test_io_number(void) {
    lexer_t l;
    lexer_init(&l, "2> err");
    
    token_t *t = lexer_next(&l);
    assert(t->type == TOKEN_IO_NUMBER);
    assert(strcmp(t->value, "2") == 0);
    token_free(t);
    
    t = lexer_next(&l);
    assert(t->type == TOKEN_OPERATOR);
    assert(strcmp(t->value, ">") == 0);
    token_free(t);
    
    t = lexer_next(&l);
    assert(t->type == TOKEN_WORD);
    assert(strcmp(t->value, "err") == 0);
    token_free(t);
    
    printf("PASS: test_io_number\n");
}

void test_variable(void) {
    lexer_t l;
    lexer_init(&l, "echo $VAR ${VAR}");
    
    token_t *t;
    t = lexer_next(&l); assert(strcmp(t->value, "echo") == 0); token_free(t);
    t = lexer_next(&l); assert(strcmp(t->value, "$VAR") == 0); token_free(t);
    t = lexer_next(&l); assert(strcmp(t->value, "${VAR}") == 0); token_free(t);
    
    printf("PASS: test_variable\n");
}

int main(void) {
    test_simple();
    test_quotes();
    test_operators();
    test_io_number();
    test_variable();
    return 0;
}
