#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *line1 = NULL;
static char *line2 = NULL;
static size_t len1 = 0;
static size_t len2 = 0;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "usage: join file1 file2\n");
        return 1;
    }

    FILE *f1 = fopen(argv[1], "r");
    FILE *f2 = fopen(argv[2], "r");
    if (!f1 || !f2) { perror("fopen"); return 1; }

    while (getline(&line1, &len1, f1) != -1) {
        char *key1 = strtok(line1, " \t\n");
        if (!key1) continue;
        
        rewind(f2);
        while (getline(&line2, &len2, f2) != -1) {
            char *line2_copy = strdup(line2);
            char *key2 = strtok(line2_copy, " \t\n");
            if (key2 && strcmp(key1, key2) == 0) {
                printf("%s", key1);
                char *rest1 = line1 + strlen(key1) + 1;
                char *rest2 = line2 + strlen(key2) + 1;
                // Trim trailing newlines
                if (rest1[strlen(rest1)-1] == '\n') rest1[strlen(rest1)-1] = 0;
                if (rest2[strlen(rest2)-1] == '\n') rest2[strlen(rest2)-1] = 0;
                if (*rest1) printf(" %s", rest1);
                if (*rest2) printf(" %s", rest2);
                printf("\n");
            }
            free(line2_copy);
        }
    }

    fclose(f1);
    fclose(f2);
    return 0;
}