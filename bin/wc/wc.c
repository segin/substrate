#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h> // for O_RDONLY

int main(int argc, char *argv[]) {
    int fd = 0;
    if (argc > 1) {
        fd = open(argv[1], O_RDONLY, 0);
        if (fd < 0) {
            printf("wc: %s: No such file or directory\n", argv[1]);
            return 1;
        }
    }
    
    long lines = 0, words = 0, bytes = 0;
    int in_word = 0;
    char buf[1024];
    int n;
    
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        bytes += n;
        for (int i = 0; i < n; i++) {
            if (buf[i] == '\n') lines++;
            if (isspace(buf[i])) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                words++;
            }
        }
    }
    
    printf(" %ld %ld %ld", lines, words, bytes);
    if (argc > 1) printf(" %s", argv[1]);
    printf("\n");
    
    if (fd != 0) close(fd);
    return 0;
}