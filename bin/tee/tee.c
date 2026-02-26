#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main(int argc, char *argv[]) {
    FILE *files[10];
    int num_files = 0;
    int append = 0;
    
    int i = 1;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        append = 1;
        i++;
    }
    
    for (; i < argc && num_files < 10; i++) {
        files[num_files++] = fopen(argv[i], append ? "a" : "w");
    }
    
    char buf[1024];
    int n;
    while ((n = read(0, buf, sizeof(buf))) > 0) {
        write(1, buf, n);
        for (int j = 0; j < num_files; j++) {
            if (files[j]) fwrite(buf, 1, n, files[j]);
        }
    }
    
    for (int j = 0; j < num_files; j++) {
        if (files[j]) fclose(files[j]);
    }
    return 0;
}
