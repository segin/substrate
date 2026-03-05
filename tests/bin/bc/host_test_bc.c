#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>

char mock_stdout[1024];
char mock_stderr[1024];
int mock_stdout_pos = 0;
int mock_stderr_pos = 0;

int mock_fprintf(FILE *stream, const char *format, ...) {
    if (stream == stderr) {
        va_list args;
        va_start(args, format);
        int ret = vsnprintf(mock_stderr + mock_stderr_pos, sizeof(mock_stderr) - mock_stderr_pos, format, args);
        mock_stderr_pos += ret;
        va_end(args);
        return ret;
    } else {
        va_list args;
        va_start(args, format);
        int ret = vfprintf(stream, format, args);
        va_end(args);
        return ret;
    }
}

#define fprintf mock_fprintf

#define main bc_main
#include "../../../bin/bc/bc.c"
#undef main

void reset_mocks() {
    mock_stdout_pos = 0;
    mock_stderr_pos = 0;
    mock_stdout[0] = '\0';
    mock_stderr[0] = '\0';

    // Clear vars
    while (vars) {
        variable_t *v = vars;
        vars = vars->next;
        free(v->name);
        bc_free(v->val);
        free(v);
    }
}

void test_get_var_undefined() {
    reset_mocks();

    bc_num *val = get_var("nonexistent_var");
    assert(val == NULL);
    assert(strstr(mock_stderr, "Error: undefined variable 'nonexistent_var'") != NULL);
}

void test_get_var_defined() {
    reset_mocks();

    get_or_create_var("my_var");

    bc_num *val = get_var("my_var");
    assert(val != NULL);
    assert(mock_stderr_pos == 0); // No error
}

int main() {
    test_get_var_undefined();
    test_get_var_defined();
    printf("All bc tests passed!\n");
    return 0;
}
