#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: ln <target> <link_name>\n");
        return 1;
    }
    if (link(argv[1], argv[2]) != 0) {
        printf("ln: failed to create link\n");
        return 1;
    }
    return 0;
}

