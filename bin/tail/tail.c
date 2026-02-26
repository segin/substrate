#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Simple head/tail logic
int main(int argc, char *argv[]) {
    int n = 10;
    FILE *fp = stdin;
    
    // Naive arg parse
    int idx = 1;
    if (argc > 1 && argv[1][0] == '-') {
        n = atoi(argv[1] + 1);
        idx++;
    }
    if (idx < argc) {
        fp = fopen(argv[idx], "r");
        if (!fp) return 1;
    }
    
    // Tail needs to buffer
    // Circular buffer of lines?
    // For simplicity, read all lines (limited)
    char **lines = malloc(10000 * sizeof(char*)); // Max 10k lines
    int count = 0;
    char buf[1024];
    
    while (fgets(buf, sizeof(buf), fp)) {
        if (count < 10000) {
            lines[count++] = strdup(buf);
        }
    }
    
    int start = count - n;
    if (start < 0) start = 0;
    
    for (int i = start; i < count; i++) {
        printf("%s", lines[i]);
        free(lines[i]);
    }
    free(lines);
    if (fp != stdin) fclose(fp);
    return 0;
}
