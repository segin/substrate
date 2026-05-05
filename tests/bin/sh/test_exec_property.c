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

int execute_line(char *line) {
    (void)line;
    return 0;
}

static int run_command(const char *command) {
    lexer_t l;
    ast_node_t *node;
    int status;

    lexer_init(&l, command);
    node = parser_parse(&l);
    assert(node != NULL);
    status = execute_ast(node, NULL);
    ast_free(node);
    return status;
}

static void reset_shell_state(void) {
    shell_var_destroy();
    shell_var_init(NULL);
    job_init();
}

static void test_assignment_prefix_properties(void) {
    static const char *old_values[] = { "", "outer", "old_value", "12345", NULL };
    static const char *new_values[] = { "", "inner", "new_value", "98765", NULL };

    reset_shell_state();
    assert(run_command("propfn() { export OBSERVED=$TMPVAR; }") == 0);

    for (int i = 0; old_values[i]; i++) {
        for (int j = 0; new_values[j]; j++) {
            char command[256];

            shell_var_set("TMPVAR", old_values[i]);
            shell_var_unset("OBSERVED");
            snprintf(command, sizeof(command), "TMPVAR=%s true", new_values[j]);
            assert(run_command(command) == 0);
            assert(strcmp(shell_var_get("TMPVAR"), old_values[i]) == 0);

            shell_var_set("TMPVAR", old_values[i]);
            shell_var_unset("OBSERVED");
            snprintf(command, sizeof(command), "TMPVAR=%s propfn", new_values[j]);
            assert(run_command(command) == 0);
            assert(strcmp(shell_var_get("TMPVAR"), old_values[i]) == 0);
            assert(strcmp(shell_var_get("OBSERVED"), new_values[j]) == 0);
        }
    }

    printf("PASS: test_assignment_prefix_properties\n");
}

int main(void) {
    test_assignment_prefix_properties();
    shell_var_destroy();
    return 0;
}
