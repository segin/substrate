#ifndef AST_H
#define AST_H

#include <stdlib.h>

typedef enum {
    NODE_SIMPLE_COMMAND,
    NODE_PIPELINE,
    NODE_BINARY_OP,
    NODE_IF,
    NODE_WHILE,
    NODE_FOR,
    NODE_FUNCTION,
    NODE_SUBSHELL, // (list)
    NODE_CASE      // case
} node_type_t;

typedef struct ast_node {
    node_type_t type;
    struct ast_redirection *redirections;
} ast_node_t;

typedef enum {
    REDIR_IN,           // <
    REDIR_OUT,          // >
    REDIR_APPEND,       // >>
    REDIR_DUP_IN,       // <&
    REDIR_DUP_OUT,      // >&
    REDIR_HERE_DOC      // <<
} redir_type_t;

typedef struct ast_redirection {
    int fd;                 // File descriptor (e.g., 0, 1, 2)
    char *filename;         // Target filename or word
    char *heredoc_content;  // Content for HERE_DOC
    int quoted;             // True if delimiter was quoted (for <<)
    redir_type_t type;
    struct ast_redirection *next;
} ast_redirection_t;

typedef struct ast_simple_command {
    ast_node_t base;
    char **args;            // NULL-terminated argv
    int arg_count;
    int arg_capacity;
    char **assignments;     // VAR=VAL strings
    int assign_count;
    int assign_capacity;
} ast_simple_command_t;

typedef struct ast_pipeline {
    ast_node_t base;
    ast_node_t **commands;  // Array of SIMPLE_COMMANDs
    int command_count;
    int command_capacity;
} ast_pipeline_t;

typedef enum {
    OP_AND, // &&
    OP_OR,  // ||
    OP_SEQ, // ;
    OP_BACKGROUND // &
} list_op_t;

typedef struct ast_binary_op {
    ast_node_t base;
    list_op_t op;
    ast_node_t *left;
    ast_node_t *right;
} ast_binary_op_t;

typedef struct ast_if {
    ast_node_t base;
    ast_node_t *condition; // list
    ast_node_t *then_body; // list
    ast_node_t *else_body; // list (optional)
} ast_if_t;

typedef struct ast_while {
    ast_node_t base;
    ast_node_t *condition; // list
    ast_node_t *body;      // list
} ast_while_t;

typedef struct ast_for {
    ast_node_t base;
    char *var_name;
    char **elements;       // "in a b c", NULL terminated
    int element_count;
    int element_capacity;
    ast_node_t *body;      // list
} ast_for_t;

typedef struct ast_function {
    ast_node_t base;
    char *name;
    ast_node_t *body; // compound command
} ast_function_t;

typedef struct ast_subshell {
    ast_node_t base;
    ast_node_t *list;
} ast_subshell_t;

// Helper to free AST nodes
void ast_free(ast_node_t *node);

typedef struct ast_case_item {
    char *pattern;
    ast_node_t *body; // list
    struct ast_case_item *next;
} ast_case_item_t;

typedef struct ast_case {
    ast_node_t base;
    char *word;             // word to switch on
    ast_case_item_t *items; // list of Case Items
} ast_case_t;

#endif
