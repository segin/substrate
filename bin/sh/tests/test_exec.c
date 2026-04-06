#include "../exec.h"
#include "../parser.h"
#include "../lexer.h"
#include "../shell_var.h"
#include "../job.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int shell_promptvars = 0;
static int mock_execute_status = 0;
static int mock_execute_calls = 0;

int execute_line(char *line) {
    (void)line;
    mock_execute_calls++;
    return mock_execute_status;
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
    mock_execute_status = 0;
    mock_execute_calls = 0;
}

static void free_envp(char **envp) {
    for (int i = 0; envp[i]; i++) {
        free(envp[i]);
    }
    free(envp);
}

static void test_builtin_true(void) {
    reset_shell_state();
    assert(run_command("true") == 0);
    printf("PASS: test_builtin_true\n");
}

static void test_builtin_false(void) {
    reset_shell_state();
    assert(run_command("false") == 1);
    printf("PASS: test_builtin_false\n");
}

static void test_simple_assignment(void) {
    reset_shell_state();
    assert(run_command("TEST_VAR=hello") == 0);
    assert(strcmp(shell_var_get("TEST_VAR"), "hello") == 0);
    printf("PASS: test_simple_assignment\n");
}

static void test_empty_expansion_is_safe(void) {
    reset_shell_state();
    shell_var_unset("EMPTY");
    assert(run_command("$EMPTY") == 0);
    assert(run_command("NEW=value $EMPTY") == 0);
    assert(strcmp(shell_var_get("NEW"), "value") == 0);
    printf("PASS: test_empty_expansion_is_safe\n");
}

static void test_export_preserves_values(void) {
    char **envp;
    int saw_a = 0;
    int saw_b = 0;

    reset_shell_state();
    shell_var_set("A", "one");
    shell_var_set("B", "two");
    assert(run_command("export A B") == 0);

    envp = shell_var_get_envp();
    for (int i = 0; envp[i]; i++) {
        if (strcmp(envp[i], "A=one") == 0) {
            saw_a = 1;
        }
        if (strcmp(envp[i], "B=two") == 0) {
            saw_b = 1;
        }
    }
    free_envp(envp);
    assert(saw_a);
    assert(saw_b);
    printf("PASS: test_export_preserves_values\n");
}

static void test_builtin_assignment_prefix_is_temporary(void) {
    reset_shell_state();
    shell_var_set("TMPVAR", "outer");
    assert(run_command("TMPVAR=inner true") == 0);
    assert(strcmp(shell_var_get("TMPVAR"), "outer") == 0);
    printf("PASS: test_builtin_assignment_prefix_is_temporary\n");
}

static void test_function_assignment_prefix_is_temporary(void) {
    reset_shell_state();
    shell_var_set("TMPVAR", "outer");
    assert(run_command("fn() { export OBSERVED=$TMPVAR; }") == 0);
    assert(run_command("TMPVAR=inner fn") == 0);
    assert(strcmp(shell_var_get("TMPVAR"), "outer") == 0);
    assert(strcmp(shell_var_get("OBSERVED"), "inner") == 0);
    printf("PASS: test_function_assignment_prefix_is_temporary\n");
}

static void test_source_returns_child_status(void) {
    char path1[] = "/tmp/sh-source-a-XXXXXX";
    char path2[] = "/tmp/sh-source-b-XXXXXX";
    int fd = mkstemp(path1);

    reset_shell_state();
    assert(fd >= 0);
    assert(write(fd, "false\n", 6) == 6);
    close(fd);
    {
        char command[128];
        mock_execute_status = 7;
        snprintf(command, sizeof(command), ". %s", path1);
        assert(run_command(command) == 7);
    }
    unlink(path1);

    reset_shell_state();
    fd = mkstemp(path2);
    assert(fd >= 0);
    assert(write(fd, "dummy\n", 6) == 6);
    close(fd);
    mock_execute_status = 9;
    {
        char command[128];
        snprintf(command, sizeof(command), ". %s", path2);
        assert(run_command(command) == 9);
    }
    unlink(path2);
    printf("PASS: test_source_returns_child_status\n");
}

static void test_assignment_only_cmdsub_status(void) {
    reset_shell_state();
    mock_execute_status = 7;
    assert(run_command("VALUE=$(echo hi)") == 7);
    printf("PASS: test_assignment_only_cmdsub_status\n");
}

static void test_parameter_error_aborts_command(void) {
    reset_shell_state();
    shell_var_set("TMPVAR", "outer");
    assert(run_command("TMPVAR=inner true ${MISSING:?boom}") == 1);
    assert(strcmp(shell_var_get("TMPVAR"), "outer") == 0);
    printf("PASS: test_parameter_error_aborts_command\n");
}

int main(void) {
    test_builtin_true();
    test_builtin_false();
    test_simple_assignment();
    test_empty_expansion_is_safe();
    test_export_preserves_values();
    test_builtin_assignment_prefix_is_temporary();
    test_function_assignment_prefix_is_temporary();
    test_source_returns_child_status();
    test_assignment_only_cmdsub_status();
    test_parameter_error_aborts_command();
    shell_var_destroy();
    return 0;
}
