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
    int count = 0;

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            FILE *f = fopen(argv[i], "r");
            if (!f) {
                fprintf(stderr, "sort: cannot read: %s\n", argv[i]);
                continue;
            }
            while (count < MAX_LINES && fgets(lines[count], MAX_LEN, f)) {
                count++;
            }
            fclose(f);
        }
    } else {
        while (count < MAX_LINES && fgets(lines[count], MAX_LEN, stdin)) {
            count++;
        }
    }
    
    qsort(lines, count, MAX_LEN, compare);
    
    for (int i = 0; i < count; i++) {
        printf("%s", lines[i]);
    }
    return 0;
}
