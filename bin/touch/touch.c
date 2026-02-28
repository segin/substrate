#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_WRONLY | O_CREAT, 0666);
        if (fd >= 0) {
            // Update time to now (stub, needs utime syscall)
            close(fd);
        } else {
            perror(argv[i]);
        }
    }
    return 0;
}
