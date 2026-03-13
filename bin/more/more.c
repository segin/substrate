#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(int argc, char *argv[]) {
    int fd = 0;
    if (argc > 1) {
        fd = open(argv[1], O_RDONLY, 0);
        if (fd < 0) {
            printf("more: cannot open %s\n", argv[1]);
            return 1;
        }
    }
    
    char buf[1];
    int lines = 0;
    
    while (read(fd, buf, 1) > 0) {
        putchar(buf[0]);
        if (buf[0] == '\n') {
            lines++;
        }
        
        if (lines >= 23) {
            printf("--More--");
            fflush(stdout);
            char c;
            read(0, &c, 1); // Wait for input on stdin
            if (c == 'q') break;
            printf("\r        \r"); // Clear --More--
            lines = 0;
        }
    }
    
    if (fd != 0) close(fd);
    return 0;
}