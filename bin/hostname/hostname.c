#include <stdio.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc > 1) {
        // Set hostname
        if (sethostname(argv[1], strlen(argv[1])) < 0) {
            perror("hostname: sethostname");
            return 1;
        }
    } else {
        // Get hostname
        char buf[256];
        if (gethostname(buf, sizeof(buf)) < 0) {
            perror("hostname: gethostname");
            return 1;
        }
        printf("%s\n", buf);
    }
    return 0;
}
