#include <unistd.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    char buf[1024];
    if (getcwd(buf, sizeof(buf))) {
        printf("%s\n", buf);
    } else {
        printf("pwd: error\n");
    }
    return 0;
}

