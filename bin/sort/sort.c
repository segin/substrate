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

    if (argc == 1) {
        while (count < MAX_LINES && fgets(lines[count], MAX_LEN, stdin)) {
            count++;
        }
    } else {
        for (int i = 1; i < argc; i++) {
            FILE *f;
            if (strcmp(argv[i], "-") == 0) {
                f = stdin;
            } else {
                f = fopen(argv[i], "r");
                if (!f) {
                    fprintf(stderr, "sort: cannot open %s\n", argv[i]);
                    continue;
                }
            }

            while (count < MAX_LINES && fgets(lines[count], MAX_LEN, f)) {
                count++;
            }

            if (f != stdin) {
                fclose(f);
            }

            if (count >= MAX_LINES) {
                break;
            }
        }
    }
    
    qsort(lines, count, MAX_LEN, compare);
    
    for (int i = 0; i < count; i++) {
        printf("%s", lines[i]);
    }
    return 0;
}
