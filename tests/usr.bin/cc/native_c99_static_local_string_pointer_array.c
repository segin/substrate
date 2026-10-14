#include <string.h>

int main(void) {
    static char const *const tests[][3] = {
        {"a", "b", "a/b"},
        {"/", "/b", "/./b"},
        {"", "a", "a"}
    };

    if (strcmp(tests[0][0], "a") != 0) return 1;
    if (strcmp(tests[0][1], "b") != 0) return 2;
    if (strcmp(tests[0][2], "a/b") != 0) return 3;
    if (strcmp(tests[1][0], "/") != 0) return 4;
    if (strcmp(tests[1][1], "/b") != 0) return 5;
    if (strcmp(tests[1][2], "/./b") != 0) return 6;
    if (strcmp(tests[2][0], "") != 0) return 7;
    if (strcmp(tests[2][1], "a") != 0) return 8;
    if (strcmp(tests[2][2], "a") != 0) return 9;
    return 0;
}
