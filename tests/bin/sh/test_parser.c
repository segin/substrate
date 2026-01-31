#include "../parser.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void test_simple_command(void) {
    lexer_t l;
    lexer_init(&l, "ls -la /tmp");
    
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);
    assert(node->type == NODE_SIMPLE_COMMAND);
    
    ast_simple_command_t *cmd = (ast_simple_command_t*)node;
    assert(cmd->arg_count == 3);
    assert(strcmp(cmd->args[0], "ls") == 0);
    assert(strcmp(cmd->args[1], "-la") == 0);
    assert(strcmp(cmd->args[2], "/tmp") == 0);
    
    ast_free(node);
    printf("PASS: test_simple_command\n");
}

void test_redirection_out(void) {
    lexer_t l;
    lexer_init(&l, "echo hello > world");
    
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);
    ast_simple_command_t *cmd = (ast_simple_command_t*)node;
    
    assert(cmd->arg_count == 2);
    assert(strcmp(cmd->args[0], "echo") == 0);
    
    assert(cmd->redirections != NULL);
    assert(cmd->redirections->type == REDIR_OUT);
    assert(cmd->redirections->fd == 1);
    assert(strcmp(cmd->redirections->filename, "world") == 0);
    
    ast_free(node);
    printf("PASS: test_redirection_out\n");
}

void test_redirection_numeric(void) {
    lexer_t l;
    lexer_init(&l, "cmd 2> err.log");
    
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);
    ast_simple_command_t *cmd = (ast_simple_command_t*)node;
    
    assert(cmd->arg_count == 1);
    assert(strcmp(cmd->args[0], "cmd") == 0);
    
    assert(cmd->redirections != NULL);
    assert(cmd->redirections->type == REDIR_OUT);
    assert(cmd->redirections->fd == 2);
    assert(strcmp(cmd->redirections->filename, "err.log") == 0);
    
    ast_free(node);
    printf("PASS: test_redirection_numeric\n");
}

void test_pipeline(void) {
    lexer_t l;
    lexer_init(&l, "ls -la | grep foo | wc -l");
    
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);
    assert(node->type == NODE_PIPELINE);
    
    ast_pipeline_t *pipe = (ast_pipeline_t*)node;
    assert(pipe->command_count == 3);
    
    ast_simple_command_t *cmd1 = (ast_simple_command_t*)pipe->commands[0];
    assert(strcmp(cmd1->args[0], "ls") == 0);
    
    ast_simple_command_t *cmd2 = (ast_simple_command_t*)pipe->commands[1];
    assert(strcmp(cmd2->args[0], "grep") == 0);
    
    ast_simple_command_t *cmd3 = (ast_simple_command_t*)pipe->commands[2];
    assert(strcmp(cmd3->args[0], "wc") == 0);
    
    ast_free(node);
    printf("PASS: test_pipeline\n");
}

void test_list_logic(void) {
    lexer_t l;
    
    // Test &&
    lexer_init(&l, "cmd1 && cmd2");
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);
    assert(node->type == NODE_BINARY_OP);
    ast_binary_op_t *bin = (ast_binary_op_t*)node;
    assert(bin->op == OP_AND);
    assert(bin->left->type == NODE_SIMPLE_COMMAND);
    assert(bin->right->type == NODE_SIMPLE_COMMAND);
    ast_free(node);
    
    // Test ||
    lexer_init(&l, "cmd1 || cmd2");
    node = parser_parse(&l);
    assert(node->type == NODE_BINARY_OP);
    bin = (ast_binary_op_t*)node;
    assert(bin->op == OP_OR);
    ast_free(node);
    
    // Test ;
    lexer_init(&l, "cmd1 ; cmd2");
    node = parser_parse(&l);
    assert(node->type == NODE_BINARY_OP);
    bin = (ast_binary_op_t*)node;
    assert(bin->op == OP_SEQ);
    ast_free(node);

    // Test Precedence: cmd1 && cmd2 || cmd3 -> (cmd1 && cmd2) || cmd3
    lexer_init(&l, "cmd1 && cmd2 || cmd3");
    node = parser_parse(&l);
    assert(node->type == NODE_BINARY_OP);
    bin = (ast_binary_op_t*)node;
    assert(bin->op == OP_OR); // Top level is OR
    assert(bin->left->type == NODE_BINARY_OP); // Left is AND
    assert(((ast_binary_op_t*)bin->left)->op == OP_AND);
    ast_free(node);
    
    // Test Sequence Logic: cmd1 ; cmd2 && cmd3 -> cmd1 ; (cmd2 && cmd3)
    lexer_init(&l, "cmd1 ; cmd2 && cmd3");
    node = parser_parse(&l);
    assert(node->type == NODE_BINARY_OP);
    bin = (ast_binary_op_t*)node;
    assert(bin->op == OP_SEQ); // Top level is SEQ
    assert(bin->right->type == NODE_BINARY_OP); // Right is AND
    ast_free(node);

    printf("PASS: test_list_logic\n");
}

void test_compound(void) {
    lexer_t l;

    // Test IF
    lexer_init(&l, "if ls; then echo yes; fi");
    ast_node_t *node = parser_parse(&l);
    assert(node != NULL);
    assert(node->type == NODE_IF);
    ast_if_t *if_node = (ast_if_t*)node;
    assert(if_node->condition != NULL);
    assert(if_node->then_body != NULL);
    ast_free(node);

    // Test WHILE
    lexer_init(&l, "while true; do echo loop; done");
    node = parser_parse(&l);
    assert(node != NULL);
    assert(node->type == NODE_WHILE);
    ast_free(node);

    // Test FOR
    lexer_init(&l, "for i in a b c; do echo $i; done");
    node = parser_parse(&l);
    assert(node != NULL);
    assert(node->type == NODE_FOR);
    ast_for_t *for_node = (ast_for_t*)node;
    assert(strcmp(for_node->var_name, "i") == 0);
    assert(for_node->element_count == 3);
    ast_free(node);
    
    printf("PASS: test_compound\n");
}

int main(void) {
    test_simple_command();
    test_redirection_out();
    test_redirection_numeric();
    test_pipeline();
    test_list_logic();
    test_compound();
    return 0;
}
