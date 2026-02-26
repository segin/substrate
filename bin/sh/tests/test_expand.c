#include "../expand.h"
#include "../shell_var.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void test_tilde(void) {
    shell_var_export("HOME", "/home/testuser");
    char *res = expand_word("~/foo");
    assert(res != NULL);
    assert(strcmp(res, "/home/testuser/foo") == 0);
    free(res);
    printf("PASS: test_tilde\n");
}

void test_parameter(void) {
    shell_var_set("VAR", "hello");
    char *res = expand_word("$VAR world");
    assert(res != NULL);
    assert(strcmp(res, "hello world") == 0);
    free(res);

    res = expand_word("${VAR}world");
    assert(res != NULL);
    assert(strcmp(res, "helloworld") == 0);
    free(res);
    printf("PASS: test_parameter\n");
}

void test_quotes(void) {
    shell_var_set("VAR", "expanded");
    
    // Single quotes
    char *res = expand_word("'$VAR'");
    assert(res != NULL);
    assert(strcmp(res, "$VAR") == 0);
    free(res);

    // Double quotes
    res = expand_word("\"$VAR\"");
    assert(res != NULL);
    assert(strcmp(res, "expanded") == 0);
    free(res);

    // Mixed
    res = expand_word("foo'bar'\"baz$VAR\"");
    assert(res != NULL);
    assert(strcmp(res, "foobarbazexpanded") == 0);
    free(res);
    printf("PASS: test_quotes\n");
}

void test_ps1_expansion(void) {
    shell_var_set("VAR", "hello world");
    
    // Test that expand_word (used for PS1) does NOT split on spaces
    char *res = expand_word("$VAR");
    assert(res != NULL);
    // If splitting happened, we'd get "hello" (first word)
    // If correct (no splitting), we get "hello world"
    if (strcmp(res, "hello world") != 0) {
        fprintf(stderr, "FAIL: test_ps1_expansion: expected 'hello world', got '%s'\n", res);
        exit(1);
    }
    free(res);
    printf("PASS: test_ps1_expansion\n");
}

void test_escape(void) {
    char *res = expand_word("\\$VAR");
    assert(res != NULL);
    assert(strcmp(res, "$VAR") == 0);
    free(res);

    res = expand_word("\"\\$VAR\"");
    assert(res != NULL);
    assert(strcmp(res, "$VAR") == 0);
    free(res);
    printf("PASS: test_escape\n");
}

void test_advanced_expansion(void) {
    shell_var_set("SET_VAR", "value");
    shell_var_unset("UNSET_VAR");
    shell_var_set("EMPTY_VAR", "");
    shell_var_set("PATH_VAR", "/usr/bin/local");

    // ${VAR:-default}
    char *res = expand_word("${SET_VAR:-default}");
    assert(strcmp(res, "value") == 0); free(res);

    res = expand_word("${UNSET_VAR:-default}");
    assert(strcmp(res, "default") == 0); free(res);

    res = expand_word("${EMPTY_VAR:-default}");
    assert(strcmp(res, "default") == 0); free(res);

    // ${VAR#pattern}
    res = expand_word("${PATH_VAR#*/}"); // pattern */ matches /
    // shortest match of */ from start of /usr/bin/local is /
    // wait, */ matches /usr/ too? No, */ matches / or /u or /usr/
    // pattern */ matches string ending in /.
    // / matches */? yes.
    // /u matches */? no.
    // /usr/ matches */? yes.
    // Shortest match is /.
    // Remaining string: usr/bin/local
    assert(strcmp(res, "usr/bin/local") == 0); free(res);
    
    res = expand_word("${PATH_VAR#*usr}"); 
    // shortest match of *usr against /usr/bin/local
    // matches /usr
    // remaining: /bin/local
    assert(strcmp(res, "/bin/local") == 0); free(res);
    
    printf("PASS: test_advanced_expansion\n");
}

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void create_file(const char *path) {
    int fd = open(path, O_CREAT | O_WRONLY, 0644);
    if (fd >= 0) close(fd);
}

void test_glob(void) {
    // Setup test environment
    mkdir("test_glob_dir", 0755);
    create_file("test_glob_dir/a.c");
    create_file("test_glob_dir/b.c");
    create_file("test_glob_dir/readme.txt");
    create_file("test_glob_dir/.hidden");
    
    // Explicitly change to directory to test simple globbing (since our implementation is basic)
    char cwd[1024];
    getcwd(cwd, sizeof(cwd));
    chdir("test_glob_dir");

    // Test *.c
    char **res = expand_list((char*[]){"*.c", NULL});
    assert(res != NULL);
    assert(res[0] != NULL && strcmp(res[0], "a.c") == 0);
    assert(res[1] != NULL && strcmp(res[1], "b.c") == 0);
    assert(res[2] == NULL);
    // Cleanup list? expand_list returns alloc'd array of alloc'd strings.
    // Ideally we free them.

    // Test *
    res = expand_list((char*[]){"*", NULL});
    // Expect a.c, b.c, readme.txt in alphabetical order
    assert(res != NULL);
    assert(strcmp(res[0], "a.c") == 0);
    assert(strcmp(res[1], "b.c") == 0);
    assert(strcmp(res[2], "readme.txt") == 0);
    assert(res[3] == NULL);

    // Test ?
    res = expand_list((char*[]){"?.c", NULL});
    assert(strcmp(res[0], "a.c") == 0);
    assert(strcmp(res[1], "b.c") == 0);
    assert(res[2] == NULL);

    // Test [ab].c
    res = expand_list((char*[]){"[ab].c", NULL});
    assert(strcmp(res[0], "a.c") == 0);
    assert(strcmp(res[1], "b.c") == 0);
    assert(res[2] == NULL);
    
    // Test non-matching
    res = expand_list((char*[]){"*.xyz", NULL});
    assert(strcmp(res[0], "*.xyz") == 0); // Returns pattern literal
    assert(res[1] == NULL);

    // Cleanup
    chdir(cwd);
    unlink("test_glob_dir/a.c");
    unlink("test_glob_dir/b.c");
    unlink("test_glob_dir/readme.txt");
    unlink("test_glob_dir/.hidden");
    rmdir("test_glob_dir");

    printf("PASS: test_glob\n");
}

int main(void) {
    test_tilde();
    test_parameter();
    test_quotes();
    test_escape();
    test_advanced_expansion();
    test_ps1_expansion();
    test_glob();
    return 0;
}

int execute_line(char *line) {
    (void)line;
    return 0;
}
