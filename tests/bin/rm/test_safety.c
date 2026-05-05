#include "rm_safety.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static int
test_dot_detection(void)
{
    CHECK(rm_operand_is_dot_or_dotdot(".") == true);
    CHECK(rm_operand_is_dot_or_dotdot("./") == true);
    CHECK(rm_operand_is_dot_or_dotdot("foo/..") == true);
    CHECK(rm_operand_is_dot_or_dotdot("foo") == false);
    return 0;
}

static int
test_normalize_path(void)
{
    char *normalized;

    normalized = rm_normalize_path("./alpha//beta/../gamma");
    CHECK(normalized != NULL);
    CHECK(strstr(normalized, "/alpha/gamma") != NULL);
    free(normalized);
    return 0;
}

static int
test_split_path(void)
{
    char *display_path;
    char *name;
    char *parent;
    bool had_trailing_slash;

    parent = NULL;
    name = NULL;
    display_path = NULL;
    had_trailing_slash = false;

    CHECK(rm_split_path("/tmp/example/", &parent, &name, &display_path,
        &had_trailing_slash) == 0);
    CHECK(strcmp(parent, "/tmp") == 0);
    CHECK(strcmp(name, "example") == 0);
    CHECK(strcmp(display_path, "/tmp/example") == 0);
    CHECK(had_trailing_slash == true);
    free(parent);
    free(name);
    free(display_path);
    return 0;
}

static int
test_prompt_parsing(void)
{
    FILE *input;

    input = tmpfile();
    CHECK(input != NULL);
    fputs("y\n", input);
    rewind(input);
    CHECK(rm_prompt_string(input, "question? ") == 1);
    fclose(input);

    input = tmpfile();
    CHECK(input != NULL);
    fputs("n\n", input);
    rewind(input);
    CHECK(rm_prompt_removal(input, false, "file", "sample") == 0);
    fclose(input);
    return 0;
}

int
main(void)
{
    if (test_dot_detection() != 0 || test_normalize_path() != 0 ||
        test_split_path() != 0 || test_prompt_parsing() != 0) {
        return 1;
    }
    puts("test_safety: ok");
    return 0;
}