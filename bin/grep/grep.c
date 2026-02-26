#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// Simple grep implementation
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: grep [PATTERN] [FILE...]\n");
        return 1;
    }
    
    char *pattern = argv[1];
    int file_idx = 2;
    
    char buf[1024];
    
    if (file_idx >= argc) {
        // Read from stdin
        while (fgets(buf, sizeof(buf), stdin)) {
            if (strstr(buf, pattern)) {
                printf("%s", buf);
            }
        }
    } else {
        for (; file_idx < argc; file_idx++) {
            FILE *fp = fopen(argv[file_idx], "r");
            if (!fp) {
                printf("grep: %s: No such file or directory\n", argv[file_idx]);
                continue;
            }
            while (fgets(buf, sizeof(buf), fp)) {
                if (strstr(buf, pattern)) {
                    if (argc > 3) printf("%s:", argv[file_idx]); // Print filename if multiple files
                    printf("%s", buf);
                }
            }
            fclose(fp);
        }
    }
    return 0;
}