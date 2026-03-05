#include "bas.h"
#include <getopt.h>

/* Global Storage */
Instruction space[SZ_SPACE];
int space_idx = 0;

Line lintab[SZ_LINTAB];
int lintab_size = 0;

Symbol symtab[SZ_SYMTAB];
int symtab_size = 0;

int cur_line = 0;

int find_symbol(const char *name) {
    for (int i = 0; i < symtab_size; i++) {
        if (strcmp(symtab[i].name, name) == 0) return i;
    }
    return -1;
}

int add_symbol(const char *name) {
    int idx = find_symbol(name);
    if (idx >= 0) return idx;
    
    if (symtab_size >= SZ_SYMTAB) {
        bas_error("Symbol table full");
        return -1;
    }
    
    strncpy(symtab[symtab_size].name, name, 3);
    symtab[symtab_size].name[3] = 0;
    symtab[symtab_size].value = 0.0;
    symtab[symtab_size].defined = 0;
    return symtab_size++;
}

/* Helper to Insert/Update Line */
int find_line(int lineno) {
    for (int i = 0; i < lintab_size; i++) {
        if (lintab[i].lineno == lineno) return i;
    }
    return -1;
}

/* Helper to insert/update line */
void add_line(int lineno, int offset, const char *text) {
    int idx = find_line(lineno);
    if (idx >= 0) {
        lintab[idx].offset = offset;
        free(lintab[idx].text);
        lintab[idx].text = strdup(text);
    } else {
        /* Insert sorted */
        int i = lintab_size - 1;
        while (i >= 0 && lintab[i].lineno > lineno) {
            lintab[i+1] = lintab[i];
            i--;
        }
        lintab[i+1].lineno = lineno;
        lintab[i+1].offset = offset;
        lintab[i+1].text = strdup(text);
        lintab_size++;
    }
}

void bas_init() {
    space_idx = 0;
    lintab_size = 0;
    symtab_size = 0;
    memset(symtab, 0, sizeof(symtab));
}

void bas_error(const char *msg) {
    if (cur_line > 0)
        printf("Error at %d: %s\n", cur_line, msg);
    else
        printf("Error: %s\n", msg);
}

void process_line(char *buf) {
    /* Trim newline */
    buf[strcspn(buf, "\n")] = 0;
    if (strlen(buf) == 0) return;

    if (isdigit((unsigned char)buf[0])) {
        int num = atoi(buf);
        char *text = strchr(buf, ' ');
        if (text) {
            while (*text == ' ') text++;

            /* Compile line into space */
            int start_offset = space_idx;
            if (compile_line(num, text, &space_idx)) {
                add_line(num, start_offset, text);
            }
        } else {
            /* Line number only -> delete? or empty? */
            /* V7: empty line replaces? */
        }
    } else {
        /* Immediate execution */
        if (strcmp(buf, "list") == 0) {
            list_program();
        } else if (strcmp(buf, "run") == 0) {
            execute_program();
        } else if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) {
            exit(0);
        } else {
            /* Try to compile as immediate statement */
            execute_immediate(buf);
        }
    }
}

void bas_load(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        process_line(buf);
    }

    fclose(f);
}

void bas_loop() {
    char buf[1024];
    while (1) {
        printf("> ");
        fflush(stdout);
        if (!fgets(buf, sizeof(buf), stdin)) break;
        process_line(buf);
    }
}

int main(int argc, char *argv[]) {
    int opt;
    struct option long_options[] = {
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
        switch (opt) {
            default:
                fprintf(stderr, "Usage: %s [script.bas]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    bas_init();

    if (optind < argc) {
        bas_load(argv[optind]);
    }

    bas_loop();
    return 0;
}