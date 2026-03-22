#include "../shell_var.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void test_basic_set_get(void) {
    shell_var_init(NULL);
    shell_var_set("FOO", "bar");
    assert(strcmp(shell_var_get("FOO"), "bar") == 0);
    
    shell_var_set("FOO", "baz");
    assert(strcmp(shell_var_get("FOO"), "baz") == 0);
    
    shell_var_unset("FOO");
    assert(shell_var_get("FOO") == NULL);
    printf("PASS: test_basic_set_get\n");
}

void test_readonly(void) {
    shell_var_init(NULL);
    shell_var_set("CONST", "fixed");
    shell_var_set_readonly("CONST");
    assert(shell_var_is_readonly("CONST") == 1);
    
    // Attempt to change - should fail (value should remain "fixed")
    shell_var_set("CONST", "broken");
    assert(strcmp(shell_var_get("CONST"), "fixed") == 0);
    
    // Attempt to unset - should fail
    shell_var_unset("CONST");
    assert(shell_var_get("CONST") != NULL);
    assert(strcmp(shell_var_get("CONST"), "fixed") == 0);
    
    printf("PASS: test_readonly\n");
}

void test_scoping(void) {
    shell_var_init(NULL);
    shell_var_set("GLOBAL", "gvalue");
    
    shell_var_push_scope();
    shell_var_set_local("LOCAL", "lvalue");
    shell_var_set("GLOBAL", "gmodified");
    
    assert(strcmp(shell_var_get("LOCAL"), "lvalue") == 0);
    assert(strcmp(shell_var_get("GLOBAL"), "gmodified") == 0);
    
    shell_var_pop_scope();
    assert(shell_var_get("LOCAL") == NULL);
    assert(strcmp(shell_var_get("GLOBAL"), "gmodified") == 0);
    
    printf("PASS: test_scoping\n");
}

void test_positional_params(void) {
    shell_var_init(NULL);
    char *argv[] = {"sh", "arg1", "arg2", NULL};
    shell_var_set_args(3, argv);
    
    assert(shell_var_get_argc() == 2);
    assert(strcmp(shell_var_get_arg(1), "arg1") == 0);
    assert(strcmp(shell_var_get_arg(2), "arg2") == 0);
    assert(strcmp(shell_var_get_name(), "sh") == 0);
    
    shell_var_shift(1);
    assert(shell_var_get_argc() == 1);
    assert(strcmp(shell_var_get_arg(1), "arg2") == 0);
    
    printf("PASS: test_positional_params\n");
}

void test_envp(void) {
    shell_var_init(NULL);
    shell_var_export("VAR1", "val1");
    shell_var_export("VAR2", "val2");
    shell_var_set("VAR3", "val3"); // Not exported

    char **envp = shell_var_get_envp();
    assert(envp != NULL);

    int found1 = 0, found2 = 0, found3 = 0, count = 0;
    for (int i = 0; envp[i]; i++) {
        if (strcmp(envp[i], "VAR1=val1") == 0) found1 = 1;
        if (strcmp(envp[i], "VAR2=val2") == 0) found2 = 1;
        if (strcmp(envp[i], "VAR3=val3") == 0) found3 = 1;
        count++;
    }

    assert(found1 == 1);
    assert(found2 == 1);
    assert(found3 == 0);
    assert(count == 2);

    for (int i = 0; envp[i]; i++) free(envp[i]);
    free(envp);

    printf("PASS: test_envp\n");
}

int main(void) {
    test_basic_set_get();
    test_readonly();
    test_scoping();
    test_positional_params();
    test_envp();
    return 0;
}
