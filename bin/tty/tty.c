#include <stdio.h>
#include <unistd.h>

int main() {
    if (isatty(0)) {
        printf("%s\n", ttyname(0)); // Need ttyname impl
    } else {
        printf("not a tty\n");
    }
    return 0;
}

