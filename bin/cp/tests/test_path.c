#include "cp_path.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "CHECK failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    return 1; \
} } while (0)

int main(void)
{
    char *joined = cp_path_join("/tmp/root", "child");
    char *dir = cp_path_dirname("/tmp/root/file");

    CHECK(joined != NULL);
    CHECK(strcmp(joined, "/tmp/root/child") == 0);
    CHECK(strcmp(cp_path_basename("/tmp/root/file"), "file") == 0);
    CHECK(strcmp(cp_path_basename("abc"), "abc") == 0);
    CHECK(cp_path_is_dot_or_dotdot(".") == 1);
    CHECK(cp_path_is_dot_or_dotdot("..") == 1);
    CHECK(cp_path_is_dot_or_dotdot("x") == 0);
    CHECK(dir != NULL);
    CHECK(strcmp(dir, "/tmp/root") == 0);

    free(joined);
    free(dir);
    return 0;
}
