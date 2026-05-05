#include <stdio.h>
#include <string.h>

int main() {
    char buf[1024], prev[1024];
    int first = 1;
    while (fgets(buf, sizeof(buf), stdin)) {
        if (first || strcmp(buf, prev) != 0) {
            printf("%s", buf);
            strcpy(prev, buf);
            first = 0;
        }
    }
    return 0;
}
