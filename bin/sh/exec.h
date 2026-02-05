#ifndef EXEC_H
#define EXEC_H

#include "ast.h"

/**
 * execute_ast - Entry point for executing a parsed AST.
 * @node: The root node of the AST to execute.
 * 
 * Returns the exit status of the command.
 */
int execute_ast(ast_node_t *node);
int execute_line(char *line);
void check_traps(void);
void run_exit_trap(void);

#include <sys/types.h>
#include <termios.h>

extern int shell_is_interactive;
extern int shell_errexit;
extern int errexit_disabled;
extern pid_t shell_pgid;
extern struct termios shell_tmodes;

#endif
