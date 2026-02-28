#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 1000
#define MAX_LEN 256

// Simple in-memory sort
int compare(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

char lines[MAX_LINES][MAX_LEN];

int main(int argc, char *argv[]) {
    (void)argc; (void)argv; // TODO: file args
    
    int count = 0;
    while (fgets(lines[count], MAX_LEN, stdin) && count < MAX_LINES) {
        count++;
    }
    
    qsort(lines, count, MAX_LEN, compare);
    
    for (int i = 0; i < count; i++) {
        printf("%s", lines[i]);
    }
    return 0;
}
