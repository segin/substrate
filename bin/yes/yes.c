#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    char *s = "y";
    if (argc > 1) s = argv[1];
    while (1) {
        printf("%s\n", s);
    }
    return 0;
}

