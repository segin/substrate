#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 1024
char *lines[MAX_LINES];
int n_lines = 0;
int cur_line = 0;

void ed_append() {
    char buf[1024];
    while (fgets(buf, sizeof(buf), stdin)) {
        if (strcmp(buf, ".\n") == 0) break;
        if (n_lines < MAX_LINES) {
            lines[n_lines++] = strdup(buf);
            cur_line = n_lines;
        }
    }
}

void ed_print() {
    for (int i = 0; i < n_lines; i++) {
        printf("%s", lines[i]);
    }
}

int main() {
    char cmd[256];
    while (1) {
        if (!fgets(cmd, sizeof(cmd), stdin)) break;
        if (cmd[0] == 'q') break;
        if (cmd[0] == 'a') ed_append();
        if (cmd[0] == 'p') ed_print();
        if (cmd[0] == 'w') printf("written (mock)\n");
    }
    return 0;
}

